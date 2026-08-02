#ifndef FRETBOARD_LINK_H
#define FRETBOARD_LINK_H

#include <stdint.h>
#include <stdbool.h>

#include "perf_log/perf_log_records.h"  /* perf_actuator_producer_t */

/* Transport selection: the fretboard link rides either the FLEXCOM1 USART or
 * the 10BASE-T1S link (LAN8651, net/t1s). Override at build time with
 * -DMARVIN_FRETBOARD_TRANSPORT=FRETBOARD_TRANSPORT_T1S (see user.cmake).
 * Producers and perf-log records are identical above the transport. */
#define FRETBOARD_TRANSPORT_UART 0
#define FRETBOARD_TRANSPORT_T1S  1
#ifndef MARVIN_FRETBOARD_TRANSPORT
#define MARVIN_FRETBOARD_TRANSPORT FRETBOARD_TRANSPORT_UART
#endif

/* Mechanical actuation advance (ms): how far ahead of the intended physical
 * transition the active actuator node must be commanded so its own latency
 * lands the effect on time. A property of the node marvin drives — the
 * open-drain GPIO guitar node is effectively instant (0); a future solenoid
 * rig would set its rise time here. Producers that schedule in strike-line
 * time (timing_pipeline) subtract this when deciding when to emit; the wire
 * byte stays a bare "assert now" mask. Static per-node constant for now. */
#ifndef FRETBOARD_ACTUATOR_ADVANCE_MS
#define FRETBOARD_ACTUATOR_ADVANCE_MS 0u
#endif

/* Fretboard link — FLEXCOM1 USART (ring-buffer) writer that ferries a 7-bit
 * GPIO bitmask to the fretboard MCU as a stream of single-byte messages, and
 * a parse task that drains the fretboard's 17-byte ADC frames off the RX ring
 * into PERF_REC_FRETBOARD_RAW records. Spec §4.4 actuator transport.
 * Initialize first among the actuator modules so the submit queue exists by
 * the time any producer task starts running. Call after SYS_Initialize so the
 * FLEXCOM1 peripheral is up.
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

/* Re-evaluate the fretboard node's arm bit against Detector_FretboardDriving()
 * and push it over the T1S control channel when it changes. The fretboard is
 * armed only while a song is active AND it is the selected detector, so it
 * drives the game solely inside a gameplay window — never during menus or
 * manual control. Call whenever the gameplay window (GameTiming_SetEnabled) or
 * the active-detector selection (console `active`) changes. No-op on the UART
 * transport (no control channel). */
void FretboardLink_UpdateArm(void);

#endif
