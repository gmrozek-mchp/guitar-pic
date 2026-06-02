#include "fretboard_link.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "stream_buffer.h"

#include "definitions.h"
#include "log.h"
#include "usb/usb_host.h"
#include "usb/usb_host_cdc.h"
#include "usb/usb_cdc.h"
#include "timing_pipeline.h"
#include "perf_log/perf_log.h"
#include "video/video.h"
#include "game/fret.h"

#define FBL_TASK_STACK_WORDS    768u
#define FBL_TASK_PRIORITY       5u

#define FBL_CMD_QUEUE_DEPTH     1u   /* latest-wins via xQueueOverwrite */

/* RX side — sized to absorb a brief task-scheduling stall without losing
 * frames. At 240 Hz × 12 B = 2.88 KB/s, 512 B is ~180 ms of headroom over
 * the producer rate; FBL_RX_READ_BYTES is the per-USB-Read chunk size. */
#define FBL_RX_STREAM_BYTES     512u
#define FBL_RX_READ_BYTES       64u
#define FBL_RX_TASK_STACK_WORDS 512u

#define DS_FRAME_LEN            12u
#define DS_START_BYTE           0x03u
#define DS_END_BYTE             0xFCu

/* Idle heartbeat: re-send last mask if the timing pipeline goes quiet, so
 * a stalled detector or paused game can't leave a stale frets-active
 * pattern stuck on the wire. 50 ms is well below human-perceptible. */
#define FBL_HEARTBEAT_MS        50u

#define FBL_WRITE_TIMEOUT_MS    100u

/* CDC line coding — the link rides the fretboard's on-board EDBG-CDC USB-UART
 * bridge, so this baud is what EDBG actually clocks out to the PIC32 SERCOM1.
 * Must match the PIC32-side setting (firmware/fretboard) or every byte
 * arrives corrupt. */
#define FBL_BAUDRATE            500000u

static QueueHandle_t s_cmd_queue;
static StaticQueue_t s_cmd_queue_buf;
static uint8_t       s_cmd_queue_storage[FBL_CMD_QUEUE_DEPTH * sizeof(uint8_t)];

static StackType_t   s_task_stack[FBL_TASK_STACK_WORDS];
static StaticTask_t  s_task_tcb;

static SemaphoreHandle_t s_write_done;
static StaticSemaphore_t s_write_done_buf;

/* Signaled from cdc_event_handler (ISR context) when a control-pipe
 * request completes. Used to serialize the LineCodingSet ->
 * ControlLineStateSet pair in open_cdc; the host stack will silently
 * drop the second request if it's issued before the first settles. */
static SemaphoreHandle_t s_ctrl_done;
static StaticSemaphore_t s_ctrl_done_buf;

#define FBL_CTRL_TIMEOUT_MS     500u

static volatile USB_HOST_CDC_OBJ    s_cdc_obj_pending = (USB_HOST_CDC_OBJ)0;
static volatile bool                s_cdc_obj_valid;
static USB_HOST_CDC_HANDLE          s_cdc_handle = USB_HOST_CDC_HANDLE_INVALID;
static volatile bool                s_connected;
static volatile USB_HOST_CDC_RESULT s_last_write_result;

/* Asserted-mask snapshot: most recent byte that successfully landed at the
 * USB DMA layer (i.e. a CDC_Write call that returned SUCCESS). Producer-side
 * intent goes out on the same wire byte but may be overwritten before it
 * actually transmits if Send is called repeatedly faster than the link
 * services. The PERF_REC_ACTUATOR record carries both. */
static volatile uint8_t  s_last_sent_byte;

/* Last CDC_WRITE_COMPLETE result + timestamp, captured in the ISR. The
 * ACTUATOR record snapshots these at Send-time so the host can compute
 * Send-to-ack latency without joining FBL_SEND/CDC_WRITE_COMPLETE
 * stamps. SYS_TIME_Counter64Get is ISR-safe (a register read; same
 * thing PerfLog's hdr_fill does from ISR context). */
static volatile int32_t  s_last_ack_result;
static volatile uint64_t s_last_ack_ts_counter;

/* RX path: ISR pushes received bytes into s_rx_stream then re-arms the
 * Read into s_rx_buf; fretboard_rx_task pops bytes and parses 12-byte
 * data frames. */
static StreamBufferHandle_t s_rx_stream;
static StaticStreamBuffer_t s_rx_stream_buf;
static uint8_t              s_rx_stream_storage[FBL_RX_STREAM_BYTES + 1u];
static uint8_t              s_rx_buf[FBL_RX_READ_BYTES];

static StackType_t   s_rx_task_stack[FBL_RX_TASK_STACK_WORDS];
static StaticTask_t  s_rx_task_tcb;

static inline USB_HOST_CDC_RESULT arm_rx_read(USB_HOST_CDC_HANDLE handle)
{
    USB_HOST_CDC_TRANSFER_HANDLE th;
    return USB_HOST_CDC_Read(handle, &th, s_rx_buf, sizeof(s_rx_buf));
}

static USB_HOST_CDC_EVENT_RESPONSE cdc_event_handler(USB_HOST_CDC_HANDLE handle,
                                                    USB_HOST_CDC_EVENT event,
                                                    void *eventData,
                                                    uintptr_t context)
{
    (void)context;

    switch (event)
    {
        case USB_HOST_CDC_EVENT_READ_COMPLETE:
        {
            const USB_HOST_CDC_EVENT_READ_COMPLETE_DATA *d = eventData;
            BaseType_t hpw = pdFALSE;
            uint32_t aux = ((uint32_t)d->result << 24)
                         | ((uint32_t)d->length & 0x00FFFFFFu);
            PerfLog_EmitStampFromISR(PERF_STAGE_FBL_READ_COMPLETE, 0u, aux, &hpw);
            if (d->result == USB_HOST_CDC_RESULT_SUCCESS && d->length > 0u)
            {
                (void)xStreamBufferSendFromISR(s_rx_stream, s_rx_buf,
                                               d->length, &hpw);
            }
            /* Re-arm immediately. If this fails (e.g. detach mid-read) the
             * detach event will reset state — drop silently here. */
            (void)arm_rx_read(handle);
            portYIELD_FROM_ISR(hpw);
            break;
        }
        case USB_HOST_CDC_EVENT_WRITE_COMPLETE:
        {
            const USB_HOST_CDC_EVENT_WRITE_COMPLETE_DATA *d = eventData;
            s_last_write_result    = d->result;
            s_last_ack_result      = (int32_t)d->result;
            s_last_ack_ts_counter  = SYS_TIME_Counter64Get();
            BaseType_t hpw = pdFALSE;
            PerfLog_EmitStampFromISR(PERF_STAGE_CDC_WRITE_COMPLETE, 0u,
                                     (uint32_t)d->result, &hpw);
            (void)xSemaphoreGiveFromISR(s_write_done, &hpw);
            portYIELD_FROM_ISR(hpw);
            break;
        }
        case USB_HOST_CDC_EVENT_ACM_SET_LINE_CODING_COMPLETE:
        case USB_HOST_CDC_EVENT_ACM_SET_CONTROL_LINE_STATE_COMPLETE:
        {
            BaseType_t hpw = pdFALSE;
            (void)xSemaphoreGiveFromISR(s_ctrl_done, &hpw);
            portYIELD_FROM_ISR(hpw);
            break;
        }
        case USB_HOST_CDC_EVENT_DEVICE_DETACHED:
        {
            s_connected = false;
            /* Wake any pending writer so it observes the detach instead
             * of waiting out the timeout. */
            BaseType_t hpw = pdFALSE;
            (void)xSemaphoreGiveFromISR(s_write_done, &hpw);
            portYIELD_FROM_ISR(hpw);
            break;
        }
        default:
            break;
    }
    return USB_HOST_CDC_EVENT_RESPONE_NONE;
}

static void cdc_attach_handler(USB_HOST_CDC_OBJ obj, uintptr_t context)
{
    (void)context;
    /* Hand the object to the link task; opening the device must happen
     * outside the host stack callback. */
    s_cdc_obj_pending = obj;
    s_cdc_obj_valid   = true;
}

static void close_cdc(void)
{
    if (s_cdc_handle != USB_HOST_CDC_HANDLE_INVALID)
    {
        USB_HOST_CDC_Close(s_cdc_handle);
        s_cdc_handle = USB_HOST_CDC_HANDLE_INVALID;
    }
    s_connected = false;
}

static bool open_cdc(USB_HOST_CDC_OBJ obj)
{
    USB_HOST_CDC_HANDLE h = USB_HOST_CDC_Open(obj);
    if (h == USB_HOST_CDC_HANDLE_INVALID) { return false; }

    if (USB_HOST_CDC_EventHandlerSet(h, cdc_event_handler, 0u) != USB_HOST_CDC_RESULT_SUCCESS)
    {
        USB_HOST_CDC_Close(h);
        return false;
    }

    static USB_CDC_LINE_CODING line_coding =
    {
        .dwDTERate   = FBL_BAUDRATE,
        .bCharFormat = USB_CDC_LINE_CODING_STOP_1_BIT,
        .bParityType = USB_CDC_LINE_CODING_PARITY_NONE,
        .bDataBits   = USB_CDC_LINE_CODING_DATA_8_BIT,
    };
    USB_HOST_CDC_REQUEST_HANDLE rh;
    (void)xSemaphoreTake(s_ctrl_done, 0);
    if (USB_HOST_CDC_ACM_LineCodingSet(h, &rh, &line_coding) == USB_HOST_CDC_RESULT_SUCCESS)
    {
        (void)xSemaphoreTake(s_ctrl_done, pdMS_TO_TICKS(FBL_CTRL_TIMEOUT_MS));
    }

    /* Some EDBG-CDC firmwares hold the bridge UART idle until the host
     * raises DTR. Assert DTR + carrier so the bridge actually drives
     * bytes out to the fretboard MCU's SERCOM1 RX. */
    static USB_CDC_CONTROL_LINE_STATE cls = { .dtr = 1u, .carrier = 1u };
    (void)xSemaphoreTake(s_ctrl_done, 0);
    if (USB_HOST_CDC_ACM_ControlLineStateSet(h, &rh, &cls) == USB_HOST_CDC_RESULT_SUCCESS)
    {
        (void)xSemaphoreTake(s_ctrl_done, pdMS_TO_TICKS(FBL_CTRL_TIMEOUT_MS));
    }

    s_cdc_handle = h;
    s_connected  = true;

    /* Drop any stale bytes left from a previous attach so the parser
     * doesn't open mid-frame. Then arm the first Read; the READ_COMPLETE
     * handler keeps re-arming itself from then on. */
    (void)xStreamBufferReset(s_rx_stream);
    USB_HOST_CDC_RESULT rr = arm_rx_read(h);
    if (rr != USB_HOST_CDC_RESULT_SUCCESS)
    {
        LOG_WARN("FBL: initial RX arm rejected, r=%d\r\n", (int)rr);
    }
    LOG_DEBUG("FBL: initial RX arm r=%d\r\n", (int)rr);

    LOG_INFO("FBL: CDC device attached, handle opened\r\n");
    return true;
}

static bool send_one_byte(uint8_t mask)
{
    if (!s_connected) { return false; }

    static uint8_t tx_byte;
    tx_byte = (uint8_t)(mask & 0x7F);

    /* Drain any prior signal so we wait for *this* write's completion. */
    (void)xSemaphoreTake(s_write_done, 0);

    USB_HOST_CDC_TRANSFER_HANDLE th;
    USB_HOST_CDC_RESULT r = USB_HOST_CDC_Write(s_cdc_handle, &th, &tx_byte, 1u);
    if (r != USB_HOST_CDC_RESULT_SUCCESS)
    {
        LOG_WARN("FBL: CDC_Write rejected, r=%d\r\n", (int)r);
        return false;
    }
    PerfLog_EmitStamp(PERF_STAGE_FBL_SEND, 0u, (uint32_t)tx_byte);

    if (xSemaphoreTake(s_write_done, pdMS_TO_TICKS(FBL_WRITE_TIMEOUT_MS)) != pdTRUE)
    {
        LOG_WARN("FBL: write timeout, marking detached\r\n");
        close_cdc();
        return false;
    }
    if (s_last_write_result != USB_HOST_CDC_RESULT_SUCCESS)
    {
        LOG_WARN("FBL: write completion err=%d\r\n", (int)s_last_write_result);
        return false;
    }
    s_last_sent_byte = tx_byte;
    return true;
}

static void fretboard_link_task(void *param)
{
    (void)param;

    LOG_INFO("FBL: fretboard link started\r\n");

    uint8_t last_mask = 0u;

    for (;;)
    {
        if (s_cdc_obj_valid && s_cdc_handle == USB_HOST_CDC_HANDLE_INVALID)
        {
            s_cdc_obj_valid = false;
            (void)open_cdc(s_cdc_obj_pending);
        }

        uint8_t mask;
        if (xQueueReceive(s_cmd_queue, &mask, pdMS_TO_TICKS(FBL_HEARTBEAT_MS)) == pdTRUE)
        {
            last_mask = mask;
        }
        else
        {
            mask = last_mask;
        }

        if (s_connected)
        {
            (void)send_one_byte(mask);
        }
    }
}

/* Pull bytes off the RX stream buffer and emit one PERF_REC_FRETBOARD_RAW
 * per parsed 12-byte frame. Resync logic mirrors tools/ds_monitor.py: a
 * frame is valid only when buf[0]==0x03 AND buf[11]==0xFC; otherwise drop
 * the leading byte and retry alignment. The FSM-free buffered approach is
 * easier to reason about than a state machine and the frame is short. */
static void fretboard_rx_task(void *param)
{
    (void)param;

    uint8_t  frame[DS_FRAME_LEN];
    size_t   filled = 0u;
    uint32_t parsed = 0u;
    uint32_t skipped_bytes = 0u;

    for (;;)
    {
        size_t want = DS_FRAME_LEN - filled;
        size_t got  = xStreamBufferReceive(s_rx_stream, &frame[filled],
                                           want, portMAX_DELAY);
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

        uint16_t adc[FRET_COUNT];
        for (uint8_t i = 0u; i < FRET_COUNT; i++)
        {
            adc[i] = (uint16_t)frame[1u + i * 2u]
                   | (uint16_t)((uint16_t)frame[2u + i * 2u] << 8);
        }

        Video_FrameInfo info;
        Video_GetFrameInfo(&info);
        PerfLog_EmitFretboardRaw(adc, info.frame_count);

        parsed++;
        if (parsed == 1u || (parsed % 240u) == 0u)
        {
            LOG_DEBUG("FBL: rx parsed=%u skipped=%u G=%u R=%u Y=%u B=%u O=%u\r\n",
                      (unsigned)parsed, (unsigned)skipped_bytes,
                      (unsigned)adc[0], (unsigned)adc[1], (unsigned)adc[2],
                      (unsigned)adc[3], (unsigned)adc[4]);
        }

        filled = 0u;
    }
}

void FretboardLink_Initialize(void)
{
    s_cmd_queue = xQueueCreateStatic(FBL_CMD_QUEUE_DEPTH,
                                     sizeof(uint8_t),
                                     s_cmd_queue_storage,
                                     &s_cmd_queue_buf);
    configASSERT(s_cmd_queue != NULL);

    s_write_done = xSemaphoreCreateBinaryStatic(&s_write_done_buf);
    configASSERT(s_write_done != NULL);

    s_ctrl_done = xSemaphoreCreateBinaryStatic(&s_ctrl_done_buf);
    configASSERT(s_ctrl_done != NULL);

    /* Storage array is FBL_RX_STREAM_BYTES + 1 per FreeRTOS — the +1 byte is
     * used by the buffer impl, not application data. Pass the *application*
     * size, not the storage size, as xBufferSizeBytes. */
    s_rx_stream = xStreamBufferCreateStatic(FBL_RX_STREAM_BYTES, 1u,
                                            s_rx_stream_storage,
                                            &s_rx_stream_buf);
    configASSERT(s_rx_stream != NULL);

    /* Register the CDC attach listener before the bus is enabled — the host
     * stack only matches a class driver if its attach handler is in place
     * when enumeration completes. App-level USB_HOST_BusEnable runs right
     * after this init returns. */
    (void)USB_HOST_CDC_AttachEventHandlerSet(cdc_attach_handler, 0u);

    TaskHandle_t h = xTaskCreateStatic(fretboard_link_task,
                                       "FretLink",
                                       FBL_TASK_STACK_WORDS,
                                       NULL,
                                       FBL_TASK_PRIORITY,
                                       s_task_stack,
                                       &s_task_tcb);
    PerfLog_RegisterTaskForHighwater(PERF_TASK_FRETBOARD_LINK, h);

    TaskHandle_t hr = xTaskCreateStatic(fretboard_rx_task,
                                        "FretRx",
                                        FBL_RX_TASK_STACK_WORDS,
                                        NULL,
                                        FBL_TASK_PRIORITY,
                                        s_rx_task_stack,
                                        &s_rx_task_tcb);
    PerfLog_RegisterTaskForHighwater(PERF_TASK_FRETBOARD_RX, hr);
}

bool FretboardLink_IsConnected(void)
{
    return s_connected;
}

void FretboardLink_Send(uint8_t mask, uint8_t producer_id)
{
    if (s_cmd_queue == NULL) { return; }
    uint8_t v = (uint8_t)(mask & TIMING_BIT_VALID_MASK);
    /* Overwrite is strictly latest-wins: a newer producer's mask replaces
     * any unsent older one — keeps a stalled USB write from accumulating
     * stale chord state. */
    (void)xQueueOverwrite(s_cmd_queue, &v);

    uint8_t strum_dir = 0u;
    if      (v & TIMING_BIT_STRUM_DOWN) { strum_dir = 1u; }
    else if (v & TIMING_BIT_STRUM_UP)   { strum_dir = 2u; }

    PerfLog_EmitActuator(v, s_last_sent_byte, strum_dir, producer_id,
                         s_last_ack_result, s_last_ack_ts_counter);
}
