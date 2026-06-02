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
#include "usb/usb_host.h"
#include "usb/usb_host_cdc.h"
#include "usb/usb_cdc.h"
#include "timing_pipeline.h"
#include "perf_log/perf_log.h"

#define FBL_TASK_STACK_WORDS    768u
#define FBL_TASK_PRIORITY       5u

#define FBL_CMD_QUEUE_DEPTH     1u   /* latest-wins via xQueueOverwrite */

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

static USB_HOST_CDC_EVENT_RESPONSE cdc_event_handler(USB_HOST_CDC_HANDLE handle,
                                                    USB_HOST_CDC_EVENT event,
                                                    void *eventData,
                                                    uintptr_t context)
{
    (void)handle;
    (void)context;

    switch (event)
    {
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
