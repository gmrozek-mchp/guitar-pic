#include "perf_log_sink.h"
#include "perf_log.h"
#include "perf_log_rx.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "definitions.h"
#include "configuration.h"
#include "usb/usb_device.h"
#include "usb/usb_device_cdc.h"

#include "log.h"

/* USB-device CDC ACM sink. The MCC config provides USB_DEVICE_Initialize
 * + the USB_DEVICE_Tasks worker; we own the application-side state
 * machine: open the device handle, register event handlers, attach
 * on VBUS, and provide a blocking write API for the perf-log drain
 * task.
 *
 * Single producer (the drain task). Not thread-safe by design: serialize
 * one write at a time, wait for WRITE_COMPLETE, then post the next. A
 * second writer would overlap on the staging buffer. */

/* Header(6) + max payload (sizeof perf_rec_strip_t) + CRC(2), padded up
 * to a cache-line multiple so UDPHS DMA can't share a line with whatever
 * sits next to us in BSS. */
#define SINK_FRAME_BYTES_MAX  23104u

#define SINK_OPEN_RETRY_MS    50u
#define SINK_CONFIG_WAIT_MS   100u
#define SINK_WRITE_TIMEOUT_MS 100u

static USB_DEVICE_HANDLE       s_dev_handle = USB_DEVICE_HANDLE_INVALID;
static volatile bool           s_is_configured;
static volatile USB_CDC_LINE_CODING s_line_coding =
{
    .dwDTERate   = 921600u,
    .bCharFormat = 0u,
    .bParityType = 0u,
    .bDataBits   = 8u,
};
static volatile USB_CDC_CONTROL_LINE_STATE s_cls;

static SemaphoreHandle_t s_write_done;
static StaticSemaphore_t s_write_done_buf;

/* Staging buffer for one outgoing frame. UDPHS DMAs from this address;
 * keep it cache-aligned so the driver's cache-maintenance ops don't
 * collide with neighboring data. */
static uint8_t CACHE_ALIGN s_tx_frame[SINK_FRAME_BYTES_MAX];

/* RX staging — one bulk-OUT max-packet at HS (512 B). Must be ≥ MPS or
 * the UDPHS driver rejects the IRP / drops the packet. Cache-aligned for
 * the same reason as s_tx_frame. */
#define SINK_RX_BUF_BYTES  512u
static uint8_t CACHE_ALIGN s_rx_buf[SINK_RX_BUF_BYTES];

static void prime_rx_read(void)
{
    USB_DEVICE_CDC_TRANSFER_HANDLE th = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
    (void)USB_DEVICE_CDC_Read(USB_DEVICE_CDC_INDEX_0, &th,
                              s_rx_buf, SINK_RX_BUF_BYTES);
}

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, no xorout). */
static uint16_t crc16_ccitt(uint16_t crc, const uint8_t *p, uint16_t len)
{
    while (len--)
    {
        crc ^= (uint16_t)(*p++) << 8;
        for (uint8_t i = 0u; i < 8u; i++)
        {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ─── CDC class event handler ────────────────────────────────────────────── */

static USB_DEVICE_CDC_EVENT_RESPONSE cdc_event_handler(
    USB_DEVICE_CDC_INDEX index,
    USB_DEVICE_CDC_EVENT event,
    void *pData,
    uintptr_t userData)
{
    (void)index;
    (void)userData;

    switch (event)
    {
        case USB_DEVICE_CDC_EVENT_GET_LINE_CODING:
            (void)USB_DEVICE_ControlSend(s_dev_handle,
                                         (void *)&s_line_coding,
                                         sizeof(USB_CDC_LINE_CODING));
            break;

        case USB_DEVICE_CDC_EVENT_SET_LINE_CODING:
            (void)USB_DEVICE_ControlReceive(s_dev_handle,
                                            (void *)&s_line_coding,
                                            sizeof(USB_CDC_LINE_CODING));
            break;

        case USB_DEVICE_CDC_EVENT_SET_CONTROL_LINE_STATE:
        {
            const USB_CDC_CONTROL_LINE_STATE *cls = pData;
            s_cls.dtr     = cls->dtr;
            s_cls.carrier = cls->carrier;
            (void)USB_DEVICE_ControlStatus(s_dev_handle,
                                           USB_DEVICE_CONTROL_STATUS_OK);
            break;
        }

        case USB_DEVICE_CDC_EVENT_SEND_BREAK:
            (void)USB_DEVICE_ControlStatus(s_dev_handle,
                                           USB_DEVICE_CONTROL_STATUS_OK);
            break;

        case USB_DEVICE_CDC_EVENT_CONTROL_TRANSFER_DATA_RECEIVED:
            (void)USB_DEVICE_ControlStatus(s_dev_handle,
                                           USB_DEVICE_CONTROL_STATUS_OK);
            break;

        case USB_DEVICE_CDC_EVENT_WRITE_COMPLETE:
        {
            BaseType_t hpw = pdFALSE;
            (void)xSemaphoreGiveFromISR(s_write_done, &hpw);
            portYIELD_FROM_ISR(hpw);
            break;
        }

        case USB_DEVICE_CDC_EVENT_READ_COMPLETE:
        {
            const USB_DEVICE_CDC_EVENT_DATA_READ_COMPLETE *rd = pData;
            if (rd != NULL && rd->status == USB_DEVICE_CDC_RESULT_OK
                && rd->length > 0u)
            {
                PerfLogRx_Feed(s_rx_buf, (uint32_t)rd->length);
            }
            prime_rx_read();
            break;
        }

        case USB_DEVICE_CDC_EVENT_CONTROL_TRANSFER_DATA_SENT:
        default:
            break;
    }
    return USB_DEVICE_CDC_EVENT_RESPONSE_NONE;
}

/* ─── Device-layer event handler ─────────────────────────────────────────── */

static void device_event_handler(USB_DEVICE_EVENT event, void *eventData,
                                 uintptr_t context)
{
    (void)context;

    switch (event)
    {
        case USB_DEVICE_EVENT_RESET:
        case USB_DEVICE_EVENT_DECONFIGURED:
            s_is_configured = false;
            break;

        case USB_DEVICE_EVENT_CONFIGURED:
        {
            const USB_DEVICE_EVENT_DATA_CONFIGURED *cd = eventData;
            if (cd->configurationValue == 1u)
            {
                (void)USB_DEVICE_CDC_EventHandlerSet(USB_DEVICE_CDC_INDEX_0,
                                                    cdc_event_handler, 0u);
                s_is_configured = true;
                LOG_INFO("PERF: USB device CDC configured\r\n");
                prime_rx_read();
            }
            break;
        }

        case USB_DEVICE_EVENT_POWER_DETECTED:
            (void)USB_DEVICE_Attach(s_dev_handle);
            break;

        case USB_DEVICE_EVENT_POWER_REMOVED:
            (void)USB_DEVICE_Detach(s_dev_handle);
            s_is_configured = false;
            break;

        case USB_DEVICE_EVENT_SUSPENDED:
        case USB_DEVICE_EVENT_RESUMED:
        case USB_DEVICE_EVENT_SOF:
        case USB_DEVICE_EVENT_ERROR:
        default:
            break;
    }
}

/* ─── Public sink interface ──────────────────────────────────────────────── */

void PerfLogSinkCdc_Initialize(void)
{
    s_write_done = xSemaphoreCreateBinaryStatic(&s_write_done_buf);
    configASSERT(s_write_done != NULL);

    PerfLogRx_Initialize();

    /* Open may fail until USB_DEVICE_Initialize finishes its own first
     * task tick — retry until success. Drain task is alive at this
     * point so vTaskDelay is fine. */
    while (s_dev_handle == USB_DEVICE_HANDLE_INVALID)
    {
        s_dev_handle = USB_DEVICE_Open(USB_DEVICE_INDEX_0,
                                       DRV_IO_INTENT_READWRITE);
        if (s_dev_handle == USB_DEVICE_HANDLE_INVALID)
        {
            vTaskDelay(pdMS_TO_TICKS(SINK_OPEN_RETRY_MS));
        }
    }

    USB_DEVICE_EventHandlerSet(s_dev_handle, device_event_handler, 0u);

    /* Cold-boot-with-cable case: by the time we register the handler the
     * UDPHS driver may have already raced past the VBUS edge it needed
     * to fire POWER_DETECTED, or the host may have seen a brief pull-up
     * assertion during early boot and given up retrying. Force a clean
     * detach→short delay→attach edge so the host sees an unambiguous
     * device-arrival on the bus. Hot-plug after this point is unaffected
     * because POWER_REMOVED/POWER_DETECTED still drive Detach/Attach. */
    (void)USB_DEVICE_Detach(s_dev_handle);
    vTaskDelay(pdMS_TO_TICKS(100));
    (void)USB_DEVICE_Attach(s_dev_handle);

    LOG_INFO("PERF: USB device opened, waiting for host enumeration\r\n");
}

bool PerfLogSinkCdc_IsConnected(void)
{
    return s_is_configured && s_cls.dtr;
}

void PerfLogSinkCdc_WriteFramed(const void *payload, uint16_t len)
{
    if (!s_is_configured || !s_cls.dtr)
    {
        PerfLog_NoteSinkDrop((uint32_t)len);
        return;
    }

    uint16_t total = (uint16_t)(6u + len + 2u);
    if (total > SINK_FRAME_BYTES_MAX)
    {
        PerfLog_NoteSinkDrop((uint32_t)len);
        return;
    }

    s_tx_frame[0] = 0x55u;
    s_tx_frame[1] = 0x4Du;
    s_tx_frame[2] = 0x52u;
    s_tx_frame[3] = 0x56u;
    s_tx_frame[4] = (uint8_t)(len & 0xFFu);
    s_tx_frame[5] = (uint8_t)((len >> 8) & 0xFFu);
    memcpy(&s_tx_frame[6], payload, len);

    uint16_t crc = 0xFFFFu;
    crc = crc16_ccitt(crc, &s_tx_frame[4], (uint16_t)(2u + len));
    s_tx_frame[6u + len]      = (uint8_t)(crc & 0xFFu);
    s_tx_frame[6u + len + 1u] = (uint8_t)((crc >> 8) & 0xFFu);

    /* Drain any prior signal so we wait for *this* write's completion. */
    (void)xSemaphoreTake(s_write_done, 0);

    USB_DEVICE_CDC_TRANSFER_HANDLE th = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
    USB_DEVICE_CDC_RESULT r = USB_DEVICE_CDC_Write(USB_DEVICE_CDC_INDEX_0,
                                                  &th,
                                                  s_tx_frame, total,
                                                  USB_DEVICE_CDC_TRANSFER_FLAGS_DATA_COMPLETE);
    if (r != USB_DEVICE_CDC_RESULT_OK)
    {
        PerfLog_NoteSinkDrop((uint32_t)len);
        return;
    }

    if (xSemaphoreTake(s_write_done,
                       pdMS_TO_TICKS(SINK_WRITE_TIMEOUT_MS)) != pdTRUE)
    {
        /* Host went away mid-write or stalled. Mark dropped and let
         * the next state-machine pass observe DECONFIGURED. */
        PerfLog_NoteSinkDrop((uint32_t)len);
    }
}
