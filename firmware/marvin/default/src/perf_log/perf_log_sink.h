#ifndef PERF_LOG_SINK_H
#define PERF_LOG_SINK_H

#include <stdint.h>
#include <stdbool.h>

/* Internal sink interface, implemented by perf_log_sink_cdc.c.
 * Called only from the perf-log drain task — the sink is not thread-
 * safe and does not need to be. The sink frames the payload itself
 * (SOF magic + uint16_t length + payload + CRC-16/CCITT). */

void PerfLogSinkCdc_Initialize(void);

void PerfLogSinkCdc_WriteFramed(const void *payload, uint16_t len);

/* True once the host has enumerated and the terminal has asserted DTR.
 * Drain task uses the rising edge to re-emit a SESSION record so a
 * mid-session reconnect always sees the schema. */
bool PerfLogSinkCdc_IsConnected(void);

/* Free TX ring credits (0..SINK_TX_RING_DEPTH). Exposed for the `perf` console
 * command: a persistent 0 here means the sink is stalled and no record can reach
 * the host, which looks identical to "device not responding" from the outside. */
uint32_t PerfLogSinkCdc_TxCredits(void);

/* How many times leaked TX credits have been reclaimed. Non-zero means writes
 * were abandoned without completing — the stream recovered, but something is
 * aborting USB transfers (typically bus contention from a display burst). */
uint32_t PerfLogSinkCdc_TxReclaims(void);

/* USB link state, split so a bus reset / deconfigure is distinguishable from the
 * host never asserting DTR — "not connected" alone cannot tell those apart. */
typedef struct
{
    bool     configured;
    bool     dtr;
    uint32_t n_configured;   /* CONFIGURED events   */
    uint32_t n_decfg;        /* DECONFIGURED events */
    uint32_t n_reset;        /* bus RESET events    */
    uint32_t n_cls;          /* SET_CONTROL_LINE_STATE requests */
} perf_sink_link_t;

void PerfLogSinkCdc_GetLinkState(perf_sink_link_t *out);

/* Re-arm the command-channel read if it is not queued. Call periodically from a
 * task: arming can fail silently, and nothing else retries it. */
void PerfLogSinkCdc_ServiceRx(void);

void PerfLogSinkCdc_GetRxArm(bool *armed, uint32_t *fails);

/* Submitted vs completed IN transfers. A widening gap means writes are being
 * accepted but never completing — the observable signature of the USB transfer
 * state having been corrupted, which no memory canary can catch (see
 * health/nocache_guard.h for why the endpoint objects cannot be guarded). */
uint32_t PerfLogSinkCdc_WritesSubmitted(void);
uint32_t PerfLogSinkCdc_WritesCompleted(void);

#endif
