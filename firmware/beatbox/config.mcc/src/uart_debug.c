#include "uart_debug.h"

#include <stdio.h>
#include <string.h>

#include "beat_detect.h"
#include "../mcc_generated_files/uart/uart1.h"

/* Constant frame period reported to the GUI, in 10 us units: the detector runs
 * at 48 kHz / 4 / 512 = 23.4375 Hz -> 42.67 ms. The GUI flags the link healthy
 * when this reads 40-46 ms. beatbox has no phase/BPM layer yet, so those two
 * fields are sent as 0 (the GUI hides a 0 BPM and parks the puppet head). */
#define DBG_FRAME_TIME_10US   (4267u)

/* Bytes pushed to the wire per Tasks pass. UART1 is polled, so IsTxReady gates
 * the actual rate; this just bounds worst-case time in the loop. */
#define DBG_DRAIN_PER_PASS    (48u)

#define TXRING_SIZE           (1024u)

static uint8_t  s_ring[TXRING_SIZE];
static uint16_t s_head;      /* next write index */
static uint16_t s_tail;      /* next read index  */
static uint16_t s_count;     /* bytes queued     */

static bool s_enabled = true;
static uint8_t s_band_select;

static char    s_rx[16];
static uint8_t s_rx_len;

/* Queue a full line or nothing: telemetry that doesn't fit is dropped rather
 * than truncated, so the GUI never sees a half-line. */
static void ring_push_line(const char *s, uint16_t len)
{
    if (len > (uint16_t)(TXRING_SIZE - s_count))
    {
        return;
    }
    for (uint16_t i = 0u; i < len; i++)
    {
        s_ring[s_head] = (uint8_t)s[i];
        s_head = (uint16_t)((s_head + 1u) % TXRING_SIZE);
    }
    s_count = (uint16_t)(s_count + len);
}

static void parse_rx_line(void)
{
    /* Only "F,n" (band select, 0-2) is meaningful; ignore anything else. */
    if ((s_rx_len >= 3u) && (s_rx[0] == 'F') && (s_rx[1] == ','))
    {
        char c = s_rx[2];
        if ((c >= '0') && (c <= '2'))
        {
            s_band_select = (uint8_t)(c - '0');
        }
    }
}

void UART_Debug_Initialize(void)
{
    s_head = 0u;
    s_tail = 0u;
    s_count = 0u;
    s_rx_len = 0u;
    s_band_select = 0u;
    s_enabled = true;
}

void UART_Debug_Publish(const BeatFrame *f)
{
    if (!s_enabled || (f == NULL))
    {
        return;
    }

    char line[64];
    int n = snprintf(line, sizeof(line), "D,%u,%u,%u,%u,0,%u,0,%u,%u,%u\n",
                     (unsigned)BeatDetect_GetEnvelopeValue(),
                     (unsigned)BeatDetect_GetFluxValue(),
                     (unsigned)f->bass_beat,
                     (unsigned)f->full_beat,
                     (unsigned)DBG_FRAME_TIME_10US,
                     (unsigned)BeatDetect_GetBassFluxValue(),
                     (unsigned)BeatDetect_GetBassPeakBin(),
                     (unsigned)BeatDetect_GetFullPeakBin());
    if ((n > 0) && (n < (int)sizeof(line)))
    {
        ring_push_line(line, (uint16_t)n);
    }

    if (BeatDetect_HasSpectrumFrame())
    {
        uint8_t bins[BEAT_SPEC_BINS];
        BeatDetect_GetSpectrumData(bins);

        char spec[300];
        int off = snprintf(spec, sizeof(spec), "S");
        for (uint8_t i = 0u; (i < BEAT_SPEC_BINS) && (off > 0) && (off < (int)sizeof(spec)); i++)
        {
            off += snprintf(&spec[off], sizeof(spec) - (size_t)off, ",%u", (unsigned)bins[i]);
        }
        if ((off > 0) && (off < (int)(sizeof(spec) - 1)))
        {
            spec[off++] = '\n';
            ring_push_line(spec, (uint16_t)off);
        }
    }
}

void UART_Debug_Tasks(void)
{
    /* Drain queued telemetry to the wire, bounded and gated so the loop never
     * blocks on UART1. */
    for (uint16_t i = 0u; (i < DBG_DRAIN_PER_PASS) && (s_count > 0u) && UART1_IsTxReady(); i++)
    {
        UART1_Write(s_ring[s_tail]);
        s_tail = (uint16_t)((s_tail + 1u) % TXRING_SIZE);
        s_count--;
    }

    /* Accumulate GUI commands a line at a time. */
    while (UART1_IsRxReady())
    {
        char c = (char)UART1_Read();
        if ((c == '\n') || (c == '\r'))
        {
            parse_rx_line();
            s_rx_len = 0u;
        }
        else if (s_rx_len < (uint8_t)(sizeof(s_rx) - 1u))
        {
            s_rx[s_rx_len++] = c;
        }
        else
        {
            s_rx_len = 0u;   /* overlong line: drop it */
        }
    }
}

void    UART_Debug_SetEnabled(bool en) { s_enabled = en; }
bool    UART_Debug_IsEnabled(void)     { return s_enabled; }
uint8_t UART_Debug_BandSelect(void)    { return s_band_select; }
