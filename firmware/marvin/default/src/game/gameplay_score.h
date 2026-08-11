#ifndef MARVIN_GAMEPLAY_SCORE_H
#define MARVIN_GAMEPLAY_SCORE_H

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

/* ── note meter (five lamps left of the scoreboard; half a lamp per note hit) ──
 *
 * Stateless per-frame read of the lamp column, the cheap counterpart to the
 * odometer below: five lamps × two halves thresholded on brightness, no
 * segmentation and no glyph matching. `half` is the filtered count (0..10)
 * obtained by walking up from the bottom lamp and stopping at the first dark
 * half, so a bright stage light behind the upper column cannot inflate it.
 * `raw` is the unfiltered per-half result — bit 2k is lamp k's lower half and
 * bit 2k+1 its upper, k = 0 at the bottom — kept so a disagreement between the
 * two is visible offline instead of silently becoming a count.
 *
 * Costs GP_BULB_PIXEL_READS integer samples, so it is cheap enough to run on
 * every frame. Meaningful only on the 1p in-song screen; the caller gates it. */
typedef struct
{
    uint8_t  half;   /* 0..10 half-lamps lit, counted from the bottom */
    uint8_t  valid;  /* 1 = `raw` is a contiguous fill from the bottom */
    uint16_t raw;    /* 10 per-half bits, unfiltered; see above */
} gp_bulbs_t;

int gp_read_bulbs(const uint8_t *frame, int width, int height, gp_bulbs_t *out);

/* ── note-streak counter (3-tumbler odometer) ────────────────────────────────
 *
 * Two-part design mirroring the host prototype (score.py):
 *   gp_read_streak  — STATELESS per-frame read: presence + each wheel's best-match
 *                     digit and whether that read was confident (settled). Presence
 *                     means the odometer is *locked at its final position*, detected
 *                     from the fixed note-icon glyph (the counter slides/bounces in,
 *                     so mid-slide the cells are misaligned) — not from the digits.
 *                     Units wheel is dark-on-light (highlighted), tens/hundreds
 *                     white-on-dark; each is matched against its polarity's bank.
 *   gp_streak_track — STATEFUL reconcile: presence is binary — not present ⇒ the
 *                     streak reset ⇒ 0 (the only reset path). While present, a
 *                     confident wheel read is authoritative for its place (it
 *                     trumps the rollover logic, so a misread self-corrects next
 *                     frame); an unreadable (rolling) wheel is filled — held from
 *                     last, or reset to 0 when a higher place changed (a carry).
 *                     Caller owns the state; reset it at song start.
 * gp_read_streak is pure (host-cross-checkable); the tracker is trivial integer
 * logic layered on top. */

typedef struct
{
    uint8_t present;     /* 1 = odometer locked/settled at final position (streak >= ~25) */
    uint8_t digit[3];    /* per-place best-match digit: [0]=hundreds [1]=tens [2]=units */
    uint8_t known[3];    /* per-place: read was confident (settled wheel), else mid-roll */
} gp_streak_raw_t;

int gp_read_streak(const uint8_t *frame, int width, int height, gp_streak_raw_t *out);

typedef struct
{
    uint16_t val;       /* last committed streak */
    uint8_t  seen;      /* a value has been seeded this run */
    uint8_t  dg[3];     /* per-place committed digits [hundreds, tens, units] */
    uint8_t  pend[3];   /* per-place pending (unconfirmed) digit, 0xFF = none */
    uint8_t  pend_n[3]; /* consecutive confident reads of the pending digit */
} gp_streak_state_t;

void     gp_streak_reset(gp_streak_state_t *st);
uint16_t gp_streak_track(gp_streak_state_t *st, const gp_streak_raw_t *raw);

#endif
