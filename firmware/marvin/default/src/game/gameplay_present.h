#ifndef MARVIN_GAMEPLAY_PRESENT_H
#define MARVIN_GAMEPLAY_PRESENT_H

#include <stdint.h>

/* Gameplay-screen detector by static scoreboard-chrome presence, ported from the
 * host prototype (tools/gameplay/gameplay/present.py) and driven by the generated
 * game/gameplay_metadata.h probe tables. A gameplay frame is mostly dynamic (note
 * highways, crowd, changing digits), so a whole-frame centroid is flaky; the
 * scoring chrome is the one static element and its layout is the 1p/2p signature
 * (one bottom-left score block vs two top amp panels). Pure math (no FreeRTOS /
 * video deps) so it cross-checks against Python on a host, then links unchanged
 * into the game_task. Not reentrant — uses a static block scratch; only the single
 * observer task calls it (see gameplay_classify.h). */

/* Classify a BGR888 frame (GP_CANON_W × GP_CANON_H, 3 B/pixel) as a gameplay screen
 * by scoreboard chrome. Returns GP_SCREEN_in_song (1p), GP_SCREEN_in_song_2p (2p),
 * or GP_SCREEN_UNKNOWN when no chrome registers (caller falls through to the
 * whole-frame gp_classify). Fixed nominal offset, no search — the capture is
 * pixel-locked. When non-NULL, out_sad_milli[GP_N_PROBES] receives each probe's
 * L1-per-masked-pixel ×1000 (diagnostics / host cross-check). The gameplay-screen
 * counterpart to gp_classify (the whole-frame centroid classifier for the static
 * screens); the observer runs this first and falls back to gp_classify. */
uint8_t gp_present(const uint8_t *frame, int width, int height, int32_t *out_sad_milli);

#endif
