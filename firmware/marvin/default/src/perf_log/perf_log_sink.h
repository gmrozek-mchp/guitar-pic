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

#endif
