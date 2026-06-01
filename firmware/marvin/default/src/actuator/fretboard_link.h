#ifndef FRETBOARD_LINK_H
#define FRETBOARD_LINK_H

#include <stdint.h>
#include <stdbool.h>

#include "perf_log/perf_log_records.h"  /* perf_actuator_producer_t */

/* Fretboard link — USB CDC host writer that ferries a 7-bit GPIO bitmask
 * to the fretboard MCU as a stream of single-byte messages. Spec §4.4
 * actuator transport. Initialize first among the actuator modules so the
 * submit queue exists by the time any producer task starts running. Also
 * enables the USB host bus, so call after SYS_Initialize.
 *
 * Producers — timing_pipeline today, manual_control during operator UI
 * mode, future game menu controller (spec §4.8) — all submit through
 * FretboardLink_Send and tag themselves with a perf_actuator_producer_t
 * id. The id rides on the PERF_REC_ACTUATOR record emitted per Send so
 * the host can see which producer owns the wire at any moment (mode-level
 * arbitration is invisible from the wire byte alone).
 *
 * Latest-wins semantics: a newer mask overwrites an unsent older one,
 * so a stalled USB write can't accumulate stale state. Mode-level
 * arbitration (which producer is allowed to submit when) lives one
 * layer up; the link stays dumb. */

void FretboardLink_Initialize(void);
void FretboardLink_Send(uint8_t mask, uint8_t producer_id);
bool FretboardLink_IsConnected(void);

#endif
