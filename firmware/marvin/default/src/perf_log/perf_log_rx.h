#ifndef PERF_LOG_RX_H
#define PERF_LOG_RX_H

#include <stdint.h>

/* Host→device command framer. Symmetric with the TX framer in
 * perf_log_sink_cdc.c: SOF (0x55 0x4D 0x52 0x56) + LEN(u16 LE) + payload
 * + CRC-16/CCITT-FALSE(u16 LE). On a complete, validated frame the
 * framer dispatches the command (currently only PERF_CMD_SET_TYPE_MASK)
 * by calling into perf_log.h. Bad magic / CRC / LEN bumps a drop
 * counter and resyncs on the next SOF byte.
 *
 * Fed from the CDC sink's USB_DEVICE_CDC_EVENT_READ_COMPLETE handler in
 * USB-ISR context. State is small (≤16 B framed payload for the sole
 * command today) and parsing is non-blocking — no task hand-off. */

void     PerfLogRx_Initialize(void);
void     PerfLogRx_Feed(const uint8_t *bytes, uint32_t len);
uint32_t PerfLogRx_GetDropCount(void);

/* Command-channel observability for the `perf` console command. Without this a
 * command that does nothing is indistinguishable from one that never arrived. */
typedef struct
{
    uint32_t bytes;       /* bytes handed to the framer            */
    uint32_t frames;      /* complete frames passed FCS            */
    uint32_t dispatched;  /* frames with a valid command header    */
    uint32_t drops;       /* framing errors / resyncs              */
    uint8_t  last_cmd;    /* last cmd_id seen by dispatch          */
    uint16_t last_len;    /* last payload length seen by dispatch  */
    uint8_t  state;       /* framer state — non-zero = mid-frame   */
} perf_rx_diag_t;

void PerfLogRx_GetDiag(perf_rx_diag_t *out);

#endif /* PERF_LOG_RX_H */
