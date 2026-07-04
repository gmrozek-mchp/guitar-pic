#ifndef TIMING_PIPELINE_H
#define TIMING_PIPELINE_H

#include <stdint.h>
#include <stdbool.h>

/* Timing pipeline — spec §4.4. Consumes detector_state_t records from the
 * detector-state bus (filtered by Detector_GetActive()), runs the chord-
 * window + strum-scheduling state machine ported from
 * tools/fret-tuner/actuator.py, and submits a 7-bit GPIO bitmask via
 * FretboardLink_Send. One of several possible producers — game menu
 * control (spec §4.8) and manual test inputs share the same submit API.
 *
 * Bit layout matches firmware/fretboard/cmd_receive.h:
 *   bit 0 = green   bit 1 = red    bit 2 = yellow
 *   bit 3 = blue    bit 4 = orange bit 5 = strum-down bit 6 = strum-up
 *
 * Time base is detector frame timestamps, not wall clock — replay-
 * deterministic and frees the task to advance only when state changes
 * (with a periodic timeout for between-frame strum-pulse expiries). */

#define TIMING_BIT_GREEN       (1u << 0)
#define TIMING_BIT_RED         (1u << 1)
#define TIMING_BIT_YELLOW      (1u << 2)
#define TIMING_BIT_BLUE        (1u << 3)
#define TIMING_BIT_ORANGE      (1u << 4)
#define TIMING_BIT_STRUM_DOWN  (1u << 5)
#define TIMING_BIT_STRUM_UP    (1u << 6)
#define TIMING_BIT_FRET_MASK   0x1Fu
#define TIMING_BIT_VALID_MASK  0x7Fu

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
