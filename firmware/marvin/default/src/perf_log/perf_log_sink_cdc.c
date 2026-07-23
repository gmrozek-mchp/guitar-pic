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
 * on VBUS, and provide a write API for the perf-log drain task.
 *
 * Single producer (the drain task). Not thread-safe by design: a second
 * writer would race on s_tx_head and could allocate the same ring slot
 * twice. */

/* Header(6) + max payload (sizeof perf_rec_strip_t) + CRC(2), padded up
 * to a cache-line multiple so UDPHS DMA can't share a line with whatever
 * sits next to us in BSS. Derived from PERF_STRIP_MAX_BYTES so the two
 * constants can't drift — a too-small sink buffer silently rejects
 * larger strips at the size check, accumulating dropped_sink. */
#define SINK_FRAME_BYTES_RAW  (6u + PERF_STRIP_HDR_BYTES + PERF_STRIP_MAX_BYTES + 2u)
#define SINK_FRAME_BYTES_MAX  ((SINK_FRAME_BYTES_RAW + 63u) & ~63u)

/* Depth of the staging ring. Three independent capacities must all
 * permit N concurrent in-flight writes:
 *   - this ring depth                                       (here)
 *   - CDC per-instance queueSizeWrite                       (usb_device_init_data.c via MCC yml)
 *   - USB_DEVICE_CDC_QUEUE_DEPTH_COMBINED minus RX/notif use (configuration.h via MCC yml)
 * MCC config has all three sized for N=3; bump together if changed. */
#define SINK_TX_RING_DEPTH    3u

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

/* Counting semaphore: tokens = ring slots free for the producer to fill.
 * Init = SINK_TX_RING_DEPTH (all slots free). Take before claiming a
 * slot; ISR gives one back per WRITE_COMPLETE. */
static SemaphoreHandle_t s_tx_credits;
static StaticSemaphore_t s_tx_credits_buf;

/* Staging ring for outgoing frames. UDPHS DMAs from these addresses;
 * keeping the outer array CACHE_ALIGN with SINK_FRAME_BYTES_MAX a
 * multiple of CACHE_LINE_SIZE keeps every inner buffer line-aligned
 * so the driver's cache-maintenance ops don't collide with neighbors.
 * s_tx_head advances one slot per accepted submission; producer is the
 * single drain task so no lock is needed. */
static uint8_t CACHE_ALIGN s_tx_ring[SINK_TX_RING_DEPTH][SINK_FRAME_BYTES_MAX];
static uint32_t s_tx_head;

/* RX staging — one bulk-OUT max-packet at HS (512 B). Must be ≥ MPS or
 * the UDPHS driver rejects the IRP / drops the packet. Cache-aligned for
 * the same reason as s_tx_ring. */
#define SINK_RX_BUF_BYTES  512u
static uint8_t CACHE_ALIGN s_rx_buf[SINK_RX_BUF_BYTES];

static void prime_rx_read(void)
{
    USB_DEVICE_CDC_TRANSFER_HANDLE th = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
    (void)USB_DEVICE_CDC_Read(USB_DEVICE_CDC_INDEX_0, &th,
                              s_rx_buf, SINK_RX_BUF_BYTES);
}

/* Fletcher-16 (mod 255, init 0xFFFF). USB hardware already CRCs every
 * bulk packet on the wire, so this checksum's job is *framing-layer*
 * detection only — false SOF matches during resync, firmware bugs that
 * write a wrong LEN or a partial frame. Fletcher catches single-byte
 * changes, adjacent swaps, and most non-adjacent swaps with ~1/65536
 * accidental-match rate against random byte streams — plenty for that
 * job, at ~2 cycles/byte vs ~6 for a table-driven CRC-16. Block-mod
 * pattern from RFC 1146 / Wikipedia: defer the modulo until uint32
 * could overflow, which gives ~5800 iterations between reductions. */
static uint16_t fletcher16(uint16_t init, const uint8_t *p, uint16_t len)
{
    uint32_t s1 = (uint32_t)(init & 0xFFu);
    uint32_t s2 = (uint32_t)((init >> 8) & 0xFFu);

    while (len > 0u)
    {
        uint16_t blk = (len > 5802u) ? 5802u : len;
        len = (uint16_t)(len - blk);
        do
        {
            s1 += *p++;
            s2 += s1;
            blk--;
        } while (blk > 0u);
        s1 %= 255u;
        s2 %= 255u;
    }

    return (uint16_t)((s2 << 8) | s1);
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
            /* One ring slot has cleared the wire — return its credit. */
            BaseType_t hpw = pdFALSE;
            (void)xSemaphoreGiveFromISR(s_tx_credits, &hpw);
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
    s_tx_credits = xSemaphoreCreateCountingStatic(SINK_TX_RING_DEPTH,
                                                  SINK_TX_RING_DEPTH,
                                                  &s_tx_credits_buf);
    configASSERT(s_tx_credits != NULL);

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

    /* UDPHS (source 23) is left masked at the AIC during boot (see
     * AIC_INT_Initialize) so a host attached at power-on can't storm the CPU
     * with ENDRESET before the stack has a client. Now that the device is
     * open, has an event handler, and is attached, enable it so enumeration
     * interrupts are serviced. */
    (void) SYS_INT_SourceEnable(UDPHS_IRQn);

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

    /* Wait for a free ring slot. With N=3 against the lower CDC layer's
     * 3-deep transfer queue, this only blocks when the host has stalled
     * — under load the producer pipelines at the rate ISR returns
     * credits. Timeout drops the record and lets the next sink state-
     * machine pass observe DECONFIGURED if the host went away. */
    if (xSemaphoreTake(s_tx_credits,
                       pdMS_TO_TICKS(SINK_WRITE_TIMEOUT_MS)) != pdTRUE)
    {
        PerfLog_NoteSinkDrop((uint32_t)len);
        return;
    }

    uint8_t *frame = s_tx_ring[s_tx_head];

    frame[0] = 0x55u;
    frame[1] = 0x4Du;
    frame[2] = 0x52u;
    frame[3] = 0x56u;
    frame[4] = (uint8_t)(len & 0xFFu);
    frame[5] = (uint8_t)((len >> 8) & 0xFFu);
    memcpy(&frame[6], payload, len);

    uint16_t fcs = fletcher16(0xFFFFu, &frame[4], (uint16_t)(2u + len));
    frame[6u + len]      = (uint8_t)(fcs & 0xFFu);
    frame[6u + len + 1u] = (uint8_t)((fcs >> 8) & 0xFFu);

    USB_DEVICE_CDC_TRANSFER_HANDLE th = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
    USB_DEVICE_CDC_RESULT r = USB_DEVICE_CDC_Write(USB_DEVICE_CDC_INDEX_0,
                                                  &th,
                                                  frame, total,
                                                  USB_DEVICE_CDC_TRANSFER_FLAGS_DATA_COMPLETE);
    if (r != USB_DEVICE_CDC_RESULT_OK)
    {
        /* Submission rejected: ISR won't fire for this slot, so hand
         * the credit back ourselves. Slot stays free; head doesn't
         * advance. */
        (void)xSemaphoreGive(s_tx_credits);
        PerfLog_NoteSinkDrop((uint32_t)len);
        return;
    }

    s_tx_head = (s_tx_head + 1u) % SINK_TX_RING_DEPTH;
}
