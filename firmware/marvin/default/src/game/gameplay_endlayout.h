#ifndef MARVIN_GAMEPLAY_ENDLAYOUT_H
#define MARVIN_GAMEPLAY_ENDLAYOUT_H

#include <stdint.h>

/* Which end screen is this — practice_end_menu or faceoff_end_menu? Ported from
 * tools/gameplay/gameplay/endlayout.py, driven by the gp_end_lay_* tables in
 * game/gameplay_metadata.h.
 *
 * gp_classify cannot answer this. Both end layouts are a magazine spread over a
 * collage, and the collage, the cover art and the decoration column down the
 * right page are all per-song — a new magazine put a real faceoff_end_menu 5192
 * from song_select, without faceoff_end_menu in its top four, and
 * nav_to_main_menu then had no branch to take. So this reads the *layout*: the
 * practice-only "N OUT OF M" box against the faceoff-only player-1 streak block.
 * Their difference cancels the page's per-song grading.
 *
 * PRECONDITION: only meaningful once an end screen is known to be up. It is not a
 * screen classifier — speed_select reads +145 and multiplayer_menu +45, both
 * inside faceoff territory — and gp_end_probe does not close that gap either
 * (it fires on most menus by design). The caller with the missing context is the
 * controller: it chose the mode, and it only asks in the window just after a run
 * it started.
 *
 * Pure math, no FreeRTOS or video dependency, reentrant, so it cross-checks
 * against Python. */

/* Name the end layout on one BGR888 frame (GP_CANON_W × GP_CANON_H, 3 B/pixel).
 *
 * Returns GP_END_LAY_PRACTICE, GP_END_LAY_FACEOFF, or GP_END_LAY_UNCERTAIN when
 * the contrast lands in the dead band — refusing is recoverable (the caller
 * re-reads a later frame), naming the wrong one sends the navigator down the
 * wrong branch. Returns GP_END_LAY_UNCERTAIN for a non-canonical frame too.
 * When non-NULL, *out_contrast receives the signed A-B contrast. */
int gp_end_layout(const uint8_t *frame, int width, int height, int16_t *out_contrast);

#endif
