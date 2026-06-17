#include "fretboard_link.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "definitions.h"
#include "log.h"
#include "timing_pipeline.h"
#include "perf_log/perf_log.h"
#include "video/video.h"
#include "game/fret.h"

#if (MARVIN_FRETBOARD_TRANSPORT == FRETBOARD_TRANSPORT_T1S)
#include "net/t1s/t1s_link.h"
#include "detector/detector.h"  /* DETECTOR_ADC_FRETBOARD */
#endif

#define FBL_TASK_STACK_WORDS    768u
#define FBL_TASK_PRIORITY       5u

#define FBL_CMD_QUEUE_DEPTH     1u   /* latest-wins via xQueueOverwrite */

#define DS_FRAME_LEN            17u
#define DS_START_BYTE           0x03u
#define DS_END_BYTE             0xFCu

/* Idle heartbeat: re-send last mask if the timing pipeline goes quiet, so
 * a stalled detector or paused game can't leave a stale frets-active
 * pattern stuck on the wire. 50 ms is well below human-perceptible. */
#define FBL_HEARTBEAT_MS        50u

static QueueHandle_t s_cmd_queue;
static StaticQueue_t s_cmd_queue_buf;
static uint8_t       s_cmd_queue_storage[FBL_CMD_QUEUE_DEPTH * sizeof(uint8_t)];

static StackType_t   s_task_stack[FBL_TASK_STACK_WORDS];
static StaticTask_t  s_task_tcb;

/* Asserted-mask snapshot + last send result, snapshotted into the
 * PERF_REC_ACTUATOR record so the host can read which byte went out and when.
 * Transport-independent. */
static volatile uint8_t  s_last_sent_byte;
static volatile int32_t  s_last_ack_result;
static volatile uint64_t s_last_ack_ts_counter;

/* Parse a validated 17-byte fretboard frame and emit a PERF_REC_FRETBOARD_RAW
 * record. Shared by both transports; the caller guarantees the frame markers.
 * Frame: [0x03, 5×ADC_u16 LE, seq_u32 LE, applied_mask_u8, 0xFC]. */
static void emit_fretboard_frame(const uint8_t *frame)
{
    static uint32_t parsed;

    uint16_t adc[FRET_COUNT];
    for (uint8_t i = 0u; i < FRET_COUNT; i++)
    {
        adc[i] = (uint16_t)frame[1u + i * 2u]
               | (uint16_t)((uint16_t)frame[2u + i * 2u] << 8);
    }

    uint32_t fb_seq = (uint32_t)frame[11]
                    | ((uint32_t)frame[12] << 8)
                    | ((uint32_t)frame[13] << 16)
                    | ((uint32_t)frame[14] << 24);
    uint8_t  applied_mask = frame[15];

    Video_FrameInfo info;
    Video_GetFrameInfo(&info);
    PerfLog_EmitFretboardRaw(adc, info.frame_count, fb_seq, applied_mask);

    parsed++;
    if (parsed == 1u || (parsed % 240u) == 0u)
    {
        LOG_DEBUG("FBL: rx parsed=%u G=%u R=%u Y=%u B=%u O=%u\r\n",
                  (unsigned)parsed,
                  (unsigned)adc[0], (unsigned)adc[1], (unsigned)adc[2],
                  (unsigned)adc[3], (unsigned)adc[4]);
    }
}

#if (MARVIN_FRETBOARD_TRANSPORT == FRETBOARD_TRANSPORT_T1S)

/* T1S RX: the fretboard's 17-byte frame arrives as the Ethernet payload,
 * already deframed by the MAC-PHY + TC6, so only the markers are checked. */
static void t1s_frame_handler(uint8_t detector_id, const uint8_t *payload, uint16_t len)
{
    if ((detector_id == (uint8_t)DETECTOR_ADC_FRETBOARD) &&
        (len >= DS_FRAME_LEN) &&
        (payload[0] == DS_START_BYTE) &&
        (payload[DS_FRAME_LEN - 1u] == DS_END_BYTE))
    {
        emit_fretboard_frame(payload);
    }
}

static bool send_one_byte(uint8_t mask)
{
    uint8_t tx_byte = (uint8_t)(mask & 0x7F);
    if (!T1SLink_SendToGuitar(tx_byte))
    {
        return false;
    }
    PerfLog_EmitStamp(PERF_STAGE_FBL_SEND, 0u, (uint32_t)tx_byte);
    s_last_sent_byte      = tx_byte;
    s_last_ack_result     = 0;
    s_last_ack_ts_counter = SYS_TIME_Counter64Get();
    PerfLog_EmitStamp(PERF_STAGE_CDC_WRITE_COMPLETE, 0u, 0u);
    return true;
}

#else  /* FRETBOARD_TRANSPORT_UART */

#define FBL_RX_TASK_STACK_WORDS 512u

/* RX notification threshold: wake the parse task once a full frame's worth of
 * bytes has landed in the FLEXCOM1 RX ring. The ring (sized in MCC) keeps
 * receiving continuously, so there is no per-read re-arm gap to lose bytes
 * across. */
#define FBL_RX_THRESHOLD        DS_FRAME_LEN

/* Bound the RX wait so a missed notification can't wedge the parser; the
 * stream is continuous at 240 Hz so a wake normally arrives every ~4 ms. */
#define FBL_RX_WAIT_MS          100u

/* Link runs at 500 000 baud, 8N1 — configured by the MCC FLEXCOM1 USART
 * component (FLEXCOM1_USART_Initialize). Must match the fretboard SERCOM1
 * setting or every byte arrives corrupt. */

static StackType_t   s_rx_task_stack[FBL_RX_TASK_STACK_WORDS];
static StaticTask_t  s_rx_task_tcb;

/* Given from the FLEXCOM1 read callback (ISR) when the RX ring crosses the
 * frame threshold; woken thread drains and parses. */
static SemaphoreHandle_t s_rx_notify;
static StaticSemaphore_t s_rx_notify_buf;

/* A UART has no enumeration step — the link is up once Initialize has armed
 * the peripheral. Kept so producers and the heartbeat path can gate sends. */
static volatile bool s_link_up;

static void rx_event_handler(FLEXCOM_USART_EVENT event, uintptr_t context)
{
    (void)context;
    BaseType_t hpw = pdFALSE;

    switch (event)
    {
        case FLEXCOM_USART_EVENT_READ_THRESHOLD_REACHED:
        case FLEXCOM_USART_EVENT_READ_BUFFER_FULL:
            (void)xSemaphoreGiveFromISR(s_rx_notify, &hpw);
            break;
        case FLEXCOM_USART_EVENT_READ_ERROR:
            /* Consume + clear the error status; the resync parser recovers
             * frame alignment on the next valid start/end pair. */
            (void)FLEXCOM1_USART_ErrorGet();
            (void)xSemaphoreGiveFromISR(s_rx_notify, &hpw);
            break;
        default:
            break;
    }

    portYIELD_FROM_ISR(hpw);
}

static bool send_one_byte(uint8_t mask)
{
    if (!s_link_up) { return false; }

    uint8_t tx_byte = (uint8_t)(mask & 0x7F);

    /* Ring-buffer Write copies the byte into the TX ring and returns the
     * count accepted; for a single byte this only fails if the TX ring is
     * full, which shouldn't happen at command rates. */
    if (FLEXCOM1_USART_Write(&tx_byte, 1u) != 1u)
    {
        LOG_WARN("FBL: TX ring full, byte dropped\r\n");
        return false;
    }
    PerfLog_EmitStamp(PERF_STAGE_FBL_SEND, 0u, (uint32_t)tx_byte);

    s_last_sent_byte      = tx_byte;
    s_last_ack_result     = 0;
    s_last_ack_ts_counter = SYS_TIME_Counter64Get();
    PerfLog_EmitStamp(PERF_STAGE_CDC_WRITE_COMPLETE, 0u, 0u);
    return true;
}

/* Drain the FLEXCOM1 RX ring and emit one PERF_REC_FRETBOARD_RAW per parsed
 * 17-byte frame. Resync: a frame is valid only when buf[0]==0x03 AND
 * buf[16]==0xFC; otherwise drop the leading byte and retry alignment. */
static void fretboard_rx_task(void *param)
{
    (void)param;

    uint8_t  frame[DS_FRAME_LEN];
    size_t   filled = 0u;
    uint32_t skipped_bytes = 0u;

    for (;;)
    {
        if (FLEXCOM1_USART_ReadCountGet() == 0u)
        {
            (void)xSemaphoreTake(s_rx_notify, pdMS_TO_TICKS(FBL_RX_WAIT_MS));
            continue;
        }

        size_t want = DS_FRAME_LEN - filled;
        size_t got  = FLEXCOM1_USART_Read(&frame[filled], want);
        if (got == 0u) { continue; }
        filled += got;
        if (filled < DS_FRAME_LEN) { continue; }

        if (frame[0] != DS_START_BYTE || frame[DS_FRAME_LEN - 1u] != DS_END_BYTE)
        {
            /* Misaligned: drop one byte and shift, then loop to refill. */
            memmove(&frame[0], &frame[1], DS_FRAME_LEN - 1u);
            filled = DS_FRAME_LEN - 1u;
            skipped_bytes++;
            continue;
        }

        emit_fretboard_frame(frame);
        filled = 0u;
    }
}

#endif  /* MARVIN_FRETBOARD_TRANSPORT */

static void fretboard_link_task(void *param)
{
    (void)param;

    LOG_INFO("FBL: fretboard link started\r\n");

    uint8_t last_mask = 0u;

    for (;;)
    {
        uint8_t mask;
        if (xQueueReceive(s_cmd_queue, &mask, pdMS_TO_TICKS(FBL_HEARTBEAT_MS)) == pdTRUE)
        {
            last_mask = mask;
        }
        else
        {
            mask = last_mask;
        }

        (void)send_one_byte(mask);
    }
}

void FretboardLink_Initialize(void)
{
    s_cmd_queue = xQueueCreateStatic(FBL_CMD_QUEUE_DEPTH,
                                     sizeof(uint8_t),
                                     s_cmd_queue_storage,
                                     &s_cmd_queue_buf);
    configASSERT(s_cmd_queue != NULL);

#if (MARVIN_FRETBOARD_TRANSPORT == FRETBOARD_TRANSPORT_T1S)
    /* T1S transport owns the MAC-PHY/service task; RX frames arrive via the
     * registered handler, TX commands flow through T1SLink_SendToGuitar. */
    T1SLink_Initialize();
    T1SLink_SetFrameHandler(t1s_frame_handler);
#else
    s_rx_notify = xSemaphoreCreateBinaryStatic(&s_rx_notify_buf);
    configASSERT(s_rx_notify != NULL);

    /* Arm continuous RX: the ring fills from the FLEXCOM1 ISR; persistent
     * threshold notification wakes fretboard_rx_task each frame's worth. */
    FLEXCOM1_USART_ReadCallbackRegister(rx_event_handler, 0u);
    FLEXCOM1_USART_ReadThresholdSet(FBL_RX_THRESHOLD);
    (void)FLEXCOM1_USART_ReadNotificationEnable(true, true);

    s_link_up = true;
#endif

    TaskHandle_t h = xTaskCreateStatic(fretboard_link_task,
                                       "FretLink",
                                       FBL_TASK_STACK_WORDS,
                                       NULL,
                                       FBL_TASK_PRIORITY,
                                       s_task_stack,
                                       &s_task_tcb);
    PerfLog_RegisterTaskForHighwater(PERF_TASK_FRETBOARD_LINK, h);

#if (MARVIN_FRETBOARD_TRANSPORT != FRETBOARD_TRANSPORT_T1S)
    TaskHandle_t hr = xTaskCreateStatic(fretboard_rx_task,
                                        "FretRx",
                                        FBL_RX_TASK_STACK_WORDS,
                                        NULL,
                                        FBL_TASK_PRIORITY,
                                        s_rx_task_stack,
                                        &s_rx_task_tcb);
    PerfLog_RegisterTaskForHighwater(PERF_TASK_FRETBOARD_RX, hr);
#endif
}

bool FretboardLink_IsConnected(void)
{
#if (MARVIN_FRETBOARD_TRANSPORT == FRETBOARD_TRANSPORT_T1S)
    return T1SLink_IsConnected();
#else
    return s_link_up;
#endif
}

void FretboardLink_Send(uint8_t mask, uint8_t producer_id)
{
    if (s_cmd_queue == NULL) { return; }
    uint8_t v = (uint8_t)(mask & TIMING_BIT_VALID_MASK);
    /* Overwrite is strictly latest-wins: a newer producer's mask replaces
     * any unsent older one — keeps a stalled write from accumulating
     * stale chord state. */
    (void)xQueueOverwrite(s_cmd_queue, &v);

    uint8_t strum_dir = 0u;
    if      (v & TIMING_BIT_STRUM_DOWN) { strum_dir = 1u; }
    else if (v & TIMING_BIT_STRUM_UP)   { strum_dir = 2u; }

    PerfLog_EmitActuator(v, s_last_sent_byte, strum_dir, producer_id,
                         s_last_ack_result, s_last_ack_ts_counter);
}
