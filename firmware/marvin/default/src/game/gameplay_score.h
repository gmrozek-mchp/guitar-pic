#ifndef GAMEPLAY_SCORE_H
#define GAMEPLAY_SCORE_H

#include <stdint.h>

#include "game/gameplay_metadata.h"

/* In-song score reader, ported from the host prototype (tools/gameplay/score.py)
 * and driven by game/gameplay_metadata.h. The training font is proportional, so
 * digits are segmented by their ink gaps (not a fixed grid), each resized to a
 * canonical cell + per-glyph normalized, then 1-NN-matched against the mode's
 * 0-9 exemplars. Pure (no FreeRTOS/video), so it compiles + cross-checks on a
 * host. Not reentrant (static scratch); only the single observer task calls it.
 *
 * `mode` selects the glyph set / digit band (GP_SCORE_MODE_*); training is the
 * only mode with data today (career green font appends later, no API change). */

typedef struct
{
    int32_t value;    /* assembled score, or -1 if no digits were found */
    uint8_t ndigits;  /* number of segmented digits */
    int32_t dist;     /* worst per-digit L1 to its template (diagnostic) */
    int32_t margin;   /* weakest per-digit runner-up L1 gap (confidence) */
} gp_score_t;

int gp_read_score(const uint8_t *frame, int width, int height,
                  uint8_t mode, gp_score_t *out);

/* Classify the score multiplier (1..4) from the medallion glyph's colour
 * (2x gold / 3x green / 4x purple; 1x = no digit). Colour-count over a small
 * fixed ROI — no segmentation, mode-independent. Returns 1 on a bad frame size. */
int gp_read_multiplier(const uint8_t *frame, int width, int height);

#endif
