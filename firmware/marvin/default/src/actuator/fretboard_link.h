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

/* As FretboardLink_Send, but with a distinct teacher label for the edge-ai
 * capture: `mask` is driven to the wire (guitar/fauxmote) unchanged, while
 * `teacher_mask` is what the fretboard latches into each frame's commanded_mask
 * (T1S transport only). The timing pipeline uses this to keep playing with the
 * back-to-back fret hold on the wire while logging the released-style per-note
 * command as the training target. FretboardLink_Send forwards mask==teacher. */
void FretboardLink_SendWithTeacher(uint8_t mask, uint8_t teacher_mask,
                                   uint8_t producer_id);

bool FretboardLink_IsConnected(void);

/* Re-evaluate the fretboard node's arm bit against Detector_FretboardDriving()
 * and push it over the T1S control channel when it changes. The fretboard is
 * armed only while a song is active AND it is the selected detector, so it
 * drives the game solely inside a gameplay window — never during menus or
 * manual control. Call whenever the gameplay window (GameTiming_SetEnabled) or
 * the active-detector selection (console `active`) changes. No-op on the UART
 * transport (no control channel). */
void FretboardLink_UpdateArm(void);

/* Play difficulty for the fretboard node's inference-model selection. The
 * index is passed through unchanged: game_difficulty_t 0..3 is the same
 * numbering as the node's MODEL_SEL_EASY..MODEL_SEL_EXPERT (the node's header
 * isn't shared with marvin, so this identity is a contract, not a compile-time
 * check). Set from the committed GameSelection at run start; the console
 * `fretboard model <x>` writes the same channel by hand and is therefore
 * overridden by the next run.
 *
 * FRETBOARD_DIFFICULTY_COUNT bounds the accepted index. It covers the four
 * concrete difficulties only — the node's fifth slot (MODEL_SEL_AUTO) is
 * reachable from the console, not from a play difficulty.
 *
 * SetDifficulty stores the wanted selection and pushes it; UpdateModel re-pushes
 * when it differs from what the node was last told, latching only on a
 * successful send so a link-down attempt is retried. Both no-op on the UART
 * transport (no control channel). */
#define FRETBOARD_DIFFICULTY_COUNT 4u

void FretboardLink_SetDifficulty(uint8_t difficulty);
void FretboardLink_UpdateModel(void);

/* Can the fretboard node legitimately play this selection? Two independent
 * limits, both properties of the node rather than of the UI that asks:
 *
 *   difficulty — only `hard` has trained weights. The node keeps all four slots
 *     selectable by aliasing the untrained ones to the hard model (its
 *     models.h MODEL_BY_DIFFICULTY / MODEL_DIFFICULTY_TRAINED), so playing easy
 *     with the NN does not fail — it silently runs the hard model at easy's
 *     scroll speed. The model *is* the photo->command timing function, and the
 *     lead differs ~420 ms easy vs ~250 ms hard, so that is a wrong answer
 *     delivered confidently. Refuse it instead of shipping it.
 *   mode — the five phototransistors are physically aimed at the 1-player
 *     highway's lanes. The 2-player left highway sits ~115 px left and ~0.71x
 *     the size, so in 2P the sensors are not looking at marvin's lanes at all.
 *
 * FRETBOARD_TRAINED_DIFFICULTY tracks the node's own MODEL_HAVE_* set: widen
 * this when a second model is trained and registered there. Like the
 * SetDifficulty index mapping above this is a contract, not a compile-time
 * check — the node's header isn't shared with marvin.
 *
 * Takes the committed selection's fields spread out rather than the struct, and
 * takes the mode pre-reduced to a bool, so the actuator layer needs no include
 * from game/ and carries no copy of game_mode_t. Callers pass
 * `sel->mode == GAME_MODE_2P`. An invalid selection is not playable by the NN —
 * there is nothing committed to check. */
#define FRETBOARD_TRAINED_DIFFICULTY 2u   /* GAME_DIFF_HARD / MODEL_SEL_HARD */

bool FretboardLink_CanPlay(bool selection_valid, uint8_t difficulty, bool two_player);

#endif
