#include "perf_log_rx.h"
#include "perf_log.h"
#include "perf_log_records.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Largest command frame today is perf_cmd_set_mask_t (8 B payload). The
 * buffer is sized to allow a small amount of growth without re-tuning. */
#define PL_RX_PAYLOAD_MAX  64u

typedef enum
{
    RX_WAIT_SOF_0 = 0,
    RX_WAIT_SOF_1,
    RX_WAIT_SOF_2,
    RX_WAIT_SOF_3,
    RX_READ_LEN_0,
    RX_READ_LEN_1,
    RX_READ_PAYLOAD,
    RX_READ_FCS_0,
    RX_READ_FCS_1,
} rx_state_t;

static rx_state_t s_state;
static uint16_t   s_len;
static uint16_t   s_payload_idx;
static uint8_t    s_payload[PL_RX_PAYLOAD_MAX];
/* Streaming Fletcher-16 (mod 255). init from 0xFFFF: s1 = s2 = 0xFF. */
static uint8_t    s_fcs_s1;
static uint8_t    s_fcs_s2;
static uint16_t   s_fcs_rx;
static uint32_t   s_drops;

static inline void fcs_byte(uint8_t b)
{
    s_fcs_s1 = (uint8_t)(((uint16_t)s_fcs_s1 + b) % 255u);
    s_fcs_s2 = (uint8_t)(((uint16_t)s_fcs_s2 + s_fcs_s1) % 255u);
}

static void reset(void)
{
    s_state       = RX_WAIT_SOF_0;
    s_len         = 0u;
    s_payload_idx = 0u;
    s_fcs_s1      = 0xFFu;
    s_fcs_s2      = 0xFFu;
    s_fcs_rx      = 0u;
}

static void drop_and_reset(void)
{
    s_drops++;
    reset();
}

/* Resync helper: if we're past WAIT_SOF_0 and the byte we just rejected
 * happens to be the first SOF byte of a fresh frame, give it credit
 * rather than waiting another full frame for the sync window. */
static void resync(uint8_t b)
{
    drop_and_reset();
    if (b == PERF_LOG_SOF_0)
    {
        s_state = RX_WAIT_SOF_1;
    }
}

static void dispatch_payload(void)
{
    if (s_len < sizeof(perf_cmd_hdr_t)) { return; }

    perf_cmd_hdr_t hdr;
    memcpy(&hdr, s_payload, sizeof(hdr));
    if (hdr.magic != PERF_CMD_HDR_MAGIC) { return; }

    switch (hdr.cmd_id)
    {
        case PERF_CMD_SET_TYPE_MASK:
            if (s_len == sizeof(perf_cmd_set_mask_t))
            {
                perf_cmd_set_mask_t cmd;
                memcpy(&cmd, s_payload, sizeof(cmd));
                PerfLog_SetEnabledMask(cmd.enabled_mask);
            }
            break;

        case PERF_CMD_SNAPSHOT:
            /* Header-only. Just latch the request; the drain task does the
             * staging copy + banded emit (heavy work stays out of this
             * USB-callback context). */
            PerfLog_RequestSnapshot();
            break;

        case PERF_CMD_SET_OVERLAY:
            if (s_len == sizeof(perf_cmd_set_overlay_t))
            {
                perf_cmd_set_overlay_t cmd;
                memcpy(&cmd, s_payload, sizeof(cmd));
                PerfLog_SetOverlayFlags(cmd.flags);
            }
            break;

        default:
            break;
    }
}

void PerfLogRx_Initialize(void)
{
    reset();
    s_drops = 0u;
}

void PerfLogRx_Feed(const uint8_t *bytes, uint32_t len)
{
    for (uint32_t i = 0u; i < len; i++)
    {
        uint8_t b = bytes[i];
        switch (s_state)
        {
            case RX_WAIT_SOF_0:
                if (b == PERF_LOG_SOF_0) { s_state = RX_WAIT_SOF_1; }
                break;

            case RX_WAIT_SOF_1:
                if (b == PERF_LOG_SOF_1) { s_state = RX_WAIT_SOF_2; }
                else                     { resync(b); }
                break;

            case RX_WAIT_SOF_2:
                if (b == PERF_LOG_SOF_2) { s_state = RX_WAIT_SOF_3; }
                else                     { resync(b); }
                break;

            case RX_WAIT_SOF_3:
                if (b == PERF_LOG_SOF_3) { s_state = RX_READ_LEN_0; }
                else                     { resync(b); }
                break;

            case RX_READ_LEN_0:
                s_len   = b;
                fcs_byte(b);
                s_state = RX_READ_LEN_1;
                break;

            case RX_READ_LEN_1:
                s_len  |= (uint16_t)b << 8;
                fcs_byte(b);
                if (s_len == 0u || s_len > PL_RX_PAYLOAD_MAX) { drop_and_reset(); }
                else                                          { s_state = RX_READ_PAYLOAD; }
                break;

            case RX_READ_PAYLOAD:
                s_payload[s_payload_idx++] = b;
                fcs_byte(b);
                if (s_payload_idx >= s_len) { s_state = RX_READ_FCS_0; }
                break;

            case RX_READ_FCS_0:
                s_fcs_rx = b;
                s_state  = RX_READ_FCS_1;
                break;

            case RX_READ_FCS_1:
                s_fcs_rx |= (uint16_t)b << 8;
                {
                    uint16_t fcs_calc = (uint16_t)(((uint16_t)s_fcs_s2 << 8) | s_fcs_s1);
                    if (s_fcs_rx == fcs_calc) { dispatch_payload(); reset(); }
                    else                      { drop_and_reset(); }
                }
                break;

            default:
                drop_and_reset();
                break;
        }
    }
}

uint32_t PerfLogRx_GetDropCount(void)
{
    return s_drops;
}
