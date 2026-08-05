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

/* USB link event tallies. `sink: no host` only says the AND of configured+dtr is
 * false; these say WHICH and how it got there — a bus reset/deconfigure (link
 * level) looks nothing like the host simply never asserting DTR (control-transfer
 * level), and the two point at completely different causes. */
static volatile uint32_t s_ev_configured;
static volatile uint32_t s_ev_deconfigured;
static volatile uint32_t s_ev_reset;
static volatile uint32_t s_ev_cls;

/* Stall watch. A write that is submitted but never completes is the signature of USB
 * state having been clobbered — the link still reports configured and DTR (those
 * flags are only updated by events that no longer arrive), so from the outside the
 * device merely "stops talking". Watching for submitted-but-never-completing writes
 * catches that regardless of what corrupted it, which a memory canary cannot do:
 * the UDPHS endpoint objects sit below LE_SCRATCH at the base of .region_nocache
 * and no guard can be placed between them. */
#define SINK_STALL_REPORT_MS  2000u
static volatile uint32_t s_writes_submitted;
static volatile uint32_t s_writes_completed;
static volatile bool     s_stall_reported;

/* Counting semaphore: tokens = ring slots free for the producer to fill.
 * Init = SINK_TX_RING_DEPTH (all slots free). Take before claiming a
 * slot; ISR gives one back per WRITE_COMPLETE. */
static SemaphoreHandle_t s_tx_credits;

/* Consecutive credit-wait timeouts, and how many times credits have been
 * reclaimed. A credit is lost whenever an in-flight write is abandoned without a
 * WRITE_COMPLETE — display bursts (layer rebinds, full-surface repaints, image
 * blits) contend for DDR/AHB and can abort a UDPHS transfer. Losing
 * SINK_TX_RING_DEPTH of them stalls the sink for good, so a run of timeouts is
 * treated as evidence the driver has abandoned those transfers. */
#define SINK_TX_STALL_LIMIT  3u
static uint32_t s_tx_stall;
static uint32_t s_tx_reclaims;
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

/* Is an OUT read currently queued with the driver? */
static volatile bool     s_rx_armed;
static volatile uint32_t s_rx_arm_fail;

/* Arm a read for the next command frame.
 *
 * The result matters: if this fails there is no queued read, so READ_COMPLETE can
 * never fire — and since re-arming otherwise only happens from READ_COMPLETE, a
 * single failure leaves the device permanently deaf to commands while TX keeps
 * working perfectly. That failure is plausible at CONFIGURED time (the instance may
 * not be ready yet) and silent, which is exactly the "commands do nothing, records
 * still flow" state. PerfLogSinkCdc_ServiceRx re-tries from the drain task so a lost
 * arming self-heals instead of persisting for the whole session. */
static void prime_rx_read(void)
{
    USB_DEVICE_CDC_TRANSFER_HANDLE th = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;

    USB_DEVICE_CDC_RESULT r = USB_DEVICE_CDC_Read(USB_DEVICE_CDC_INDEX_0, &th,
                                                  s_rx_buf, SINK_RX_BUF_BYTES);
    if (r == USB_DEVICE_CDC_RESULT_OK)
    {
        s_rx_armed = true;
    }
    else
    {
        s_rx_armed = false;
        s_rx_arm_fail++;
    }
}

void PerfLogSinkCdc_ServiceRx(void)
{
    if (s_is_configured && !s_rx_armed)
    {
        prime_rx_read();
    }
}

void PerfLogSinkCdc_GetRxArm(bool *armed, uint32_t *fails)
{
    if (armed != NULL) { *armed = s_rx_armed; }
    if (fails != NULL) { *fails = s_rx_arm_fail; }
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
            s_ev_cls++;
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
            s_writes_completed++;
            BaseType_t hpw = pdFALSE;
            (void)xSemaphoreGiveFromISR(s_tx_credits, &hpw);
            portYIELD_FROM_ISR(hpw);
            break;
        }

        case USB_DEVICE_CDC_EVENT_READ_COMPLETE:
        {
            const USB_DEVICE_CDC_EVENT_DATA_READ_COMPLETE *rd = pData;
            s_rx_armed = false;
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
            s_ev_reset++;
            s_is_configured = false;
            s_rx_armed = false;   /* driver cancels queued transfers */
            break;

        case USB_DEVICE_EVENT_DECONFIGURED:
            s_ev_deconfigured++;
            s_is_configured = false;
            s_rx_armed = false;   /* driver cancels queued transfers */
            break;

        case USB_DEVICE_EVENT_CONFIGURED:
        {
            const USB_DEVICE_EVENT_DATA_CONFIGURED *cd = eventData;
            if (cd->configurationValue == 1u)
            {
                (void)USB_DEVICE_CDC_EventHandlerSet(USB_DEVICE_CDC_INDEX_0,
                                                    cdc_event_handler, 0u);
                s_is_configured = true;
                s_ev_configured++;
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

    LOG_INFO("PERF: USB device opened, waiting for host enumeration\r\n");
}

bool PerfLogSinkCdc_IsConnected(void)
{
    return s_is_configured && s_cls.dtr;
}

/* Reclaim TX credits once the host is gone.
 *
 * A credit is only returned by USB_DEVICE_CDC_EVENT_WRITE_COMPLETE. If the host
 * closes the port with writes still in flight — precisely what a one-shot CLI does
 * when it exits after its last band — the CDC driver cancels those transfers and
 * their completion events never arrive, so those credits are lost for good. After
 * SINK_TX_RING_DEPTH such losses the sink stalls permanently: every later record is
 * dropped and the whole perf stream goes silent until reboot, even though the drain
 * task is healthy and records are still being produced. A long-lived reader never
 * triggers it (its writes always complete), which is why `serve` runs indefinitely
 * while repeated one-shot captures kill the stream.
 *
 * Nothing can legitimately be in flight while disconnected, so topping the counter
 * back up to full is safe. Runs from the drain task on the drop path, so recovery is
 * automatic within one record of the host leaving. */
static void reclaim_tx_credits(void)
{
    uint32_t given = 0u;

    while (uxSemaphoreGetCount(s_tx_credits) < SINK_TX_RING_DEPTH)
    {
        if (xSemaphoreGive(s_tx_credits) != pdTRUE) { break; }
        given++;
    }

    s_tx_stall = 0u;

    /* Only a reclaim that actually returned something counts. Ticking on every
     * dropped record would make the counter read hundreds while merely
     * disconnected and idle, hiding whether credits were ever really lost. */
    if (given != 0u)
    {
        s_tx_head = 0u;
        s_tx_reclaims += given;
    }
}

uint32_t PerfLogSinkCdc_TxCredits(void)
{
    return (s_tx_credits != NULL) ? (uint32_t)uxSemaphoreGetCount(s_tx_credits) : 0u;
}

uint32_t PerfLogSinkCdc_TxReclaims(void)
{
    return s_tx_reclaims;
}

uint32_t PerfLogSinkCdc_WritesSubmitted(void) { return s_writes_submitted; }
uint32_t PerfLogSinkCdc_WritesCompleted(void) { return s_writes_completed; }

void PerfLogSinkCdc_GetLinkState(perf_sink_link_t *out)
{
    if (out == NULL) { return; }

    out->configured   = s_is_configured;
    out->dtr          = s_cls.dtr;
    out->n_configured = s_ev_configured;
    out->n_decfg      = s_ev_deconfigured;
    out->n_reset      = s_ev_reset;
    out->n_cls        = s_ev_cls;
}

void PerfLogSinkCdc_WriteFramed(const void *payload, uint16_t len)
{
    if (!s_is_configured || !s_cls.dtr)
    {
        reclaim_tx_credits();
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
        /* Recover a leak even while the host is still attached — the
         * disconnect path alone is not enough, since a reader that stays
         * connected (the viewer) would otherwise see the stream stop dead and
         * never resume. SINK_TX_STALL_LIMIT consecutive 100 ms waits with no
         * completion means the transfers are gone, not merely slow: genuine
         * backpressure clears in well under that. Reclaiming risks reusing a
         * ring slot if a transfer really was only very slow, which would
         * corrupt that one frame — a bounded, self-correcting cost against
         * losing the whole stream until reboot. */
        if (++s_tx_stall >= SINK_TX_STALL_LIMIT)
        {
            reclaim_tx_credits();
        }
        PerfLog_NoteSinkDrop((uint32_t)len);
        return;
    }

    s_tx_stall = 0u;

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
    if (r == USB_DEVICE_CDC_RESULT_OK)
    {
        s_writes_submitted++;

        /* Submitted writes that never complete mean the transfer machinery is gone.
         * Report once, loudly, naming the likely cause — this exact state has been
         * mistaken for a dead perf-log, a credit leak and a host driver problem. */
        if (!s_stall_reported
            && (uint32_t)(s_writes_submitted - s_writes_completed) > SINK_TX_RING_DEPTH * 4u)
        {
            s_stall_reported = true;
            LOG_WARN("SINK: %lu writes submitted, %lu completed — USB transfers are "
                     "not completing. Suspect .region_nocache corruption clobbering "
                     "the UDPHS endpoint state; check `perf` and the nocache guard.\r\n",
                     (unsigned long)s_writes_submitted,
                     (unsigned long)s_writes_completed);
        }
    }

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
