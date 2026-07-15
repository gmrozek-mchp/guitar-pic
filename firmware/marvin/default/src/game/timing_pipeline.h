#ifndef TIMING_PIPELINE_H
#define TIMING_PIPELINE_H

#include <stdint.h>
#include <stdbool.h>

/* Timing pipeline — spec §4.4. Consumes detector_state_t records from the
 * detector-state bus (already arbitrated to the active source by
 * Detector_Publish), runs the chord-window + strum-scheduling state machine
 * ported from tools/fret-tuner/actuator.py, and submits a guitar command
 * bitmask (actuator/guitar_cmd.h, GUITAR_BTN_*) via FretboardLink_Send. One of
 * several possible producers — game menu control (spec §4.8) and manual test
 * inputs share the same submit API.
 *
 * Time base is the detector-stamped strike-line clock (strike_at_ms), not wall
 * clock — replay-deterministic and frees the task to advance only when state
 * changes (with a periodic timeout for between-frame strum-pulse expiries). */

void TimingPipeline_Initialize(void);

/* Output enable. Default: **false** (see .c — the boot screen is a menu, not a
 * note highway). When false, the pipeline still advances internal state but
 * suppresses actuation so a peer producer (manual_control, future game-state
 * controller §4.8) can own the wire; disabling also releases the wire once.
 * Internal state stays current so the next advance() after re-enable republishes
 * the right mask without a stale frame. */
void TimingPipeline_SetEnabled(bool enabled);
bool TimingPipeline_IsEnabled(void);

#endif
