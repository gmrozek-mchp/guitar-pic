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

#define FBL_TASK_STACK_WORDS    768u
#define FBL_TASK_PRIORITY       2u

#define FBL_CMD_QUEUE_DEPTH     1u   /* latest-wins via xQueueOverwrite */

/* Idle heartbeat: re-send last mask if the timing pipeline goes quiet, so
 * a stalled detector or paused game can't leave a stale frets-active
 * pattern stuck on the wire. 50 ms is well below human-perceptible. */
#define FBL_HEARTBEAT_MS        50u

#define FBL_WRITE_TIMEOUT_MS    100u

/* CDC line coding — fretboard side ignores baud over USB CDC, but supplying
 * a sane default avoids implementation quirks on hosts that gate writes on
 * a successful SET_LINE_CODING. Matches actuator.py. */
#define FBL_BAUDRATE            115200u

static QueueHandle_t s_cmd_queue;
static StaticQueue_t s_cmd_queue_buf;
static uint8_t       s_cmd_queue_storage[FBL_CMD_QUEUE_DEPTH * sizeof(uint8_t)];

static StackType_t   s_task_stack[FBL_TASK_STACK_WORDS];
static StaticTask_t  s_task_tcb;

static SemaphoreHandle_t s_write_done;
static StaticSemaphore_t s_write_done_buf;

static volatile USB_HOST_CDC_OBJ    s_cdc_obj_pending = (USB_HOST_CDC_OBJ)0;
static volatile bool                s_cdc_obj_valid;
static USB_HOST_CDC_HANDLE          s_cdc_handle = USB_HOST_CDC_HANDLE_INVALID;
static volatile bool                s_connected;
static volatile USB_HOST_CDC_RESULT s_last_write_result;

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
            s_last_write_result = d->result;
            BaseType_t hpw = pdFALSE;
            (void)xSemaphoreGiveFromISR(s_write_done, &hpw);
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
    (void)USB_HOST_CDC_ACM_LineCodingSet(h, &rh, &line_coding);

    /* Some EDBG-CDC firmwares hold the bridge UART idle until the host
     * raises DTR. Assert DTR + carrier so the bridge actually drives
     * bytes out to the fretboard MCU's SERCOM1 RX. */
    static USB_CDC_CONTROL_LINE_STATE cls = { .dtr = 1u, .carrier = 1u };
    (void)USB_HOST_CDC_ACM_ControlLineStateSet(h, &rh, &cls);

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

    /* Register the CDC attach listener before the bus is enabled — the host
     * stack only matches a class driver if its attach handler is in place
     * when enumeration completes. App-level USB_HOST_BusEnable runs right
     * after this init returns. */
    (void)USB_HOST_CDC_AttachEventHandlerSet(cdc_attach_handler, 0u);

    (void)xTaskCreateStatic(fretboard_link_task,
                            "FretLink",
                            FBL_TASK_STACK_WORDS,
                            NULL,
                            FBL_TASK_PRIORITY,
                            s_task_stack,
                            &s_task_tcb);
}

bool FretboardLink_IsConnected(void)
{
    return s_connected;
}

void FretboardLink_Send(uint8_t mask)
{
    if (s_cmd_queue == NULL) { return; }
    uint8_t v = (uint8_t)(mask & 0x7F);
    /* Overwrite is strictly latest-wins: a newer producer's mask replaces
     * any unsent older one — keeps a stalled USB write from accumulating
     * stale chord state. */
    (void)xQueueOverwrite(s_cmd_queue, &v);
}
