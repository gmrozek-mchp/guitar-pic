#ifndef MARVIN_GAMEPLAY_ENDPROBE_H
#define MARVIN_GAMEPLAY_ENDPROBE_H

#include <stdint.h>
#include <stdbool.h>

/* Frame-rate end-of-song probe, ported from tools/gameplay/gameplay/endprobe.py
 * and driven by the gp_end_* tables in game/gameplay_metadata.h.
 *
 * Every other reader in this module runs on request, because gp_classify and the
 * chrome presence probes cost milliseconds. This one costs 225 luma samples and
 * integer arithmetic, so it runs on every frame the observer drains — its purpose
 * is to cut actuation the instant the results screen appears, before the timing
 * pipeline strums notes into the results menu. It does not decide that a run
 * ended; gp_classify still owns that (see game_controller's play loop).
 *
 * The decision is a contrast between bright patches on the right of the end
 * screen's sketch collage and the median of dark anchors on its left, so it is
 * offset-invariant and only scales with gain — an absolute threshold does not
 * survive the analog feed's slop out there. Pure math, no FreeRTOS or video
 * dependency, and reentrant (no scratch), so it cross-checks against Python. */

/* Evaluate the probe on one BGR888 frame (GP_CANON_W × GP_CANON_H, 3 B/pixel).
 *
 * Returns the number of bright patches whose contrast cleared its threshold, or
 * -1 if the frame is not the canonical size. `GP_END_K_HITS` or more means the
 * results screen is up. When non-NULL, out_contrast[GP_END_N_BRIGHT] receives
 * each patch's signed contrast and *out_anchor the median anchor level, for
 * diagnostics and the host cross-check. */
int gp_end_probe(const uint8_t *frame, int width, int height,
                 int16_t *out_contrast, uint8_t *out_anchor);

/* Consecutive-frame gate over gp_end_probe.
 *
 * Rise needs GP_END_CONFIRM_FRAMES in a row; the fall is immediate. That
 * asymmetry is deliberate: a false positive costs a fraction of a second of
 * missed notes and self-clears on the next clean frame, whereas a false negative
 * costs errant presses on the results menu. Owned by the caller (game_task), not
 * a static — one instance per actuation window. */
typedef struct
{
    uint8_t count;
    bool    latched;
} gp_end_tracker_t;

void gp_end_tracker_reset(gp_end_tracker_t *t);

/* Feed one probe result (the return of gp_end_probe). Returns the latched state:
 * true once the results screen has been seen on GP_END_CONFIRM_FRAMES successive
 * frames, false as soon as a frame disagrees. A negative `hits` (bad frame size)
 * is treated as disagreement. */
bool gp_end_tracker_update(gp_end_tracker_t *t, int hits);

#endif
