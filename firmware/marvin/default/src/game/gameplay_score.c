#include "game/gameplay_score.h"
#include "game/gameplay_cov.h"
#include "game/gameplay_metadata.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define GP_BPP             3
#define GP_SCORE_MAX_RUNS  16   /* raw runs before filtering (>= any digit count) */

/* Digit-band scratch (one mode's band), row-major. */
static uint8_t  s_ink[GP_SCORE_BAND_MAX_H * GP_SCORE_BAND_MAX_W];  /* 1 = pixel above ink threshold */
static uint16_t s_col_ink[GP_SCORE_BAND_MAX_W];                   /* ink-pixel count per column */
static uint8_t  s_glyph[GP_SCORE_LEN];                            /* one digit's coverage mask (0-255) */
static uint16_t s_runs[GP_SCORE_MAX_RUNS][2];                     /* [x0, x1] inclusive column runs */

/* Fill s_ink (0/1) over the band rect: integer luma = B*29+G*150+R*77 (no /256 — it
 * cancels in the relative threshold), ink where DEN*(luma-lo) > NUM*(hi-lo) (i.e.
 * luma > lo + NUM/DEN*(hi-lo)). bw/bh out. */
static void band_ink(const uint8_t *frame, int w, int h,
                     const uint16_t band[4], int *bw, int *bh)
{
    int x0 = band[0], y0 = band[1], x1 = band[2], y1 = band[3];
    int W = x1 - x0, H = y1 - y0;
    *bw = W;
    *bh = H;

    static int32_t lum[GP_SCORE_BAND_MAX_H * GP_SCORE_BAND_MAX_W];
    int32_t lo = INT32_MAX, hi = INT32_MIN;
    for (int r = 0; r < H; r++)
    {
        int fy = y0 + r;
        for (int c = 0; c < W; c++)
        {
            int fx = x0 + c;
            int32_t v = 0;
            if (fx >= 0 && fx < w && fy >= 0 && fy < h)
            {
                const uint8_t *p = frame + ((uint32_t)fy * (uint32_t)w + (uint32_t)fx) * GP_BPP;
                v = (int32_t)p[0] * 29 + (int32_t)p[1] * 150 + (int32_t)p[2] * 77;
            }
            lum[r * W + c] = v;
            if (v < lo) { lo = v; }
            if (v > hi) { hi = v; }
        }
    }
    int32_t rhs = (int32_t)GP_SCORE_INK_NUM * (hi - lo);
    for (int i = 0; i < W * H; i++)
    {
        s_ink[i] = ((int32_t)GP_SCORE_INK_DEN * (lum[i] - lo) > rhs) ? 1u : 0u;
    }
}

/* Per-cell ink coverage (0-255) of one digit run [x0,x1] (inclusive), cropped to
 * its ink rows and resized to GLYPH_ROWS x GLYPH_COLS. Mirrors score.py _glyph_cov. */
static void glyph_cov(int bw, int bh, int x0, int x1)
{
    /* Ink rows spanned by this run. */
    int ry0 = -1, ry1 = -1;
    for (int r = 0; r < bh; r++)
    {
        for (int c = x0; c <= x1; c++)
        {
            if (s_ink[r * bw + c]) { if (ry0 < 0) { ry0 = r; } ry1 = r; break; }
        }
    }
    if (ry0 < 0) { ry0 = 0; ry1 = bh - 1; }
    int rh = ry1 - ry0 + 1, rw = x1 - x0 + 1;

    for (int gr = 0; gr < GP_SCORE_GLYPH_ROWS; gr++)
    {
        int ya = gp_edge(gr, rh, GP_SCORE_GLYPH_ROWS);
        int yb = gp_edge(gr + 1, rh, GP_SCORE_GLYPH_ROWS);
        if (yb <= ya) { yb = ya + 1; }
        if (yb > rh) { yb = rh; }   /* clamp past the bbox → empty cell (matches numpy slice) */
        for (int gc = 0; gc < GP_SCORE_GLYPH_COLS; gc++)
        {
            int xa = gp_edge(gc, rw, GP_SCORE_GLYPH_COLS);
            int xb = gp_edge(gc + 1, rw, GP_SCORE_GLYPH_COLS);
            if (xb <= xa) { xb = xa + 1; }
            if (xb > rw) { xb = rw; }
            uint32_t sum = 0u, n = 0u;
            for (int r = ry0 + ya; r < ry0 + yb; r++)
                for (int c = x0 + xa; c < x0 + xb; c++)
                {
                    sum += s_ink[r * bw + c];
                    n++;
                }
            /* coverage fraction → 0-255, rounded */
            s_glyph[gr * GP_SCORE_GLYPH_COLS + gc] =
                (n != 0u) ? (uint8_t)((sum * 255u + n / 2u) / n) : 0u;
        }
    }
}

int gp_read_score(const uint8_t *frame, int width, int height,
                  uint8_t mode, gp_score_t *out)
{
    out->value = -1; out->ndigits = 0; out->dist = 0; out->margin = 0;
    if (width != GP_CANON_W || height != GP_CANON_H) { return -1; }
    if (mode >= GP_N_SCORE_MODES) { return -1; }
    const gp_score_mode_t *M = &gp_score_modes[mode];

    int bw = 0, bh = 0;
    band_ink(frame, width, height, M->band, &bw, &bh);

    for (int c = 0; c < bw; c++)
    {
        uint16_t cnt = 0;
        for (int r = 0; r < bh; r++) { if (s_ink[r * bw + c]) { cnt++; } }
        s_col_ink[c] = cnt;
    }

    /* Segment: contiguous ink columns split by > MIN_GAP empty columns (mirrors
     * score.py _segment_digits), then drop specks. */
    int nruns = 0;
    int start = -1, gap = 0;
    for (int c = 0; c < bw; c++)
    {
        if (s_col_ink[c] > 0)
        {
            if (start < 0) { start = c; }
            gap = 0;
        }
        else if (start >= 0)
        {
            gap++;
            if (gap > GP_SCORE_MIN_GAP)
            {
                if (nruns < GP_SCORE_MAX_RUNS) { s_runs[nruns][0] = (uint16_t)start; s_runs[nruns][1] = (uint16_t)(c - gap); nruns++; }
                start = -1;
            }
        }
    }
    if (start >= 0 && nruns < GP_SCORE_MAX_RUNS)
    {
        s_runs[nruns][0] = (uint16_t)start; s_runs[nruns][1] = (uint16_t)(bw - 1); nruns++;
    }

    int32_t value = 0;
    int ndigits = 0;
    int32_t worst_dist = 0;
    int32_t min_margin = INT32_MAX;

    for (int ri = 0; ri < nruns && ndigits < GP_SCORE_MAX_DIGITS; ri++)
    {
        int x0 = s_runs[ri][0], x1 = s_runs[ri][1];
        uint32_t ink = 0;
        for (int k = x0; k <= x1; k++) { ink += s_col_ink[k]; }
        if ((x1 - x0 + 1) < GP_SCORE_MIN_WIDTH || ink < (uint32_t)GP_SCORE_MIN_INK) { continue; }

        glyph_cov(bw, bh, x0, x1);

        /* argmin integer L1 over the 10 per-digit coverage templates (row == digit);
         * margin = gap to the runner-up digit. */
        int32_t best = INT32_MAX, second = INT32_MAX;
        int best_digit = 0;
        for (int d = 0; d < GP_SCORE_NDIGITS; d++)
        {
            const uint8_t *tmpl = M->tmpl[d];
            int32_t l1 = 0;
            for (int i = 0; i < GP_SCORE_LEN; i++) { l1 += abs((int)s_glyph[i] - (int)tmpl[i]); }
            if (l1 < best) { second = best; best = l1; best_digit = d; }
            else if (l1 < second) { second = l1; }
        }

        value = value * 10 + best_digit;
        ndigits++;
        if (best > worst_dist) { worst_dist = best; }
        if (second - best < min_margin) { min_margin = second - best; }
    }

    out->ndigits = (uint8_t)ndigits;
    out->value = (ndigits > 0) ? value : -1;
    out->dist = worst_dist;
    out->margin = (ndigits > 0) ? min_margin : 0;
    return 0;
}

/* Multiplier: count bright, saturated purple/green/yellow pixels in the medallion
 * ROI (independent tests, mirroring score.py read_multiplier), argmax → 4/3/2, or
 * 1x if the winner is below the floor. Colour only — no shape/segmentation. */
int gp_read_multiplier(const uint8_t *frame, int width, int height)
{
    if (width != GP_CANON_W || height != GP_CANON_H) { return 1; }
    static const int roi[4] = GP_MULT_ROI;
    int x0 = roi[0], y0 = roi[1], x1 = roi[2], y1 = roi[3];

    int purple = 0, green = 0, yellow = 0;
    for (int y = y0; y < y1; y++)
    {
        const uint8_t *p = frame + ((uint32_t)y * (uint32_t)width + (uint32_t)x0) * GP_BPP;
        for (int x = x0; x < x1; x++, p += GP_BPP)
        {
            int B = p[0], G = p[1], R = p[2];
            int mx = R > G ? R : G; if (B > mx) { mx = B; }
            int mn = R < G ? R : G; if (B < mn) { mn = B; }
            if (mx <= GP_MULT_BRIGHT_MIN || (mx - mn) <= GP_MULT_SAT_MIN) { continue; }
            if (R > G + 25 && B > G + 25) { purple++; }
            if (G > R + 20 && G > B + 20) { green++; }
            if (R > B + 40 && G > B + 40) { yellow++; }
        }
    }
    int best = purple;
    if (green > best) { best = green; }
    if (yellow > best) { best = yellow; }
    if (best < GP_MULT_MIN_COUNT) { return 1; }
    if (purple >= green && purple >= yellow) { return 4; }
    if (green >= yellow) { return 3; }
    return 2;
}

/* ── note-streak counter (odometer OCR + monotonic tracker) ────────────────── */

static uint8_t s_scell_ink[GP_STREAK_CELL_MAX_H * GP_STREAK_CELL_MAX_W];
static uint8_t s_scell_cov[GP_STREAK_LEN];   /* one cell's coverage mask (0-255) */

/* Polarity-aware ink-coverage mask of one odometer cell into s_scell_cov, mirroring
 * score.py _cell_cov: threshold the cell luma by its polarity (bright pixels for a
 * white-on-dark wheel, dark pixels for the light units wheel), crop to the ink
 * bounding box, and resize that box to the canonical grid (uses gp_edge like the
 * score glyph). */
static void streak_cell_cov_roi(const uint8_t *frame, int w, int h,
                                const uint16_t roi[4], int is_light)
{
    int x0 = roi[0], y0 = roi[1], x1 = roi[2], y1 = roi[3];
    int W = x1 - x0, H = y1 - y0;

    static int32_t lum[GP_STREAK_CELL_MAX_H * GP_STREAK_CELL_MAX_W];
    int32_t lo = INT32_MAX, hi = INT32_MIN;
    for (int r = 0; r < H; r++)
    {
        int fy = y0 + r;
        for (int c = 0; c < W; c++)
        {
            int fx = x0 + c;
            int32_t v = 0;
            if (fx >= 0 && fx < w && fy >= 0 && fy < h)
            {
                const uint8_t *p = frame + ((uint32_t)fy * (uint32_t)w + (uint32_t)fx) * GP_BPP;
                v = (int32_t)p[0] * 29 + (int32_t)p[1] * 150 + (int32_t)p[2] * 77;
            }
            lum[r * W + c] = v;
            if (v < lo) { lo = v; }
            if (v > hi) { hi = v; }
        }
    }
    /* Bright ink (dark wheel): DEN*(luma-lo) > NUM*(hi-lo). Dark ink (light units
     * wheel): luma < lo + (1-NUM/DEN)*(hi-lo)  =>  DEN*(luma-lo) < (DEN-NUM)*(hi-lo). */
    int32_t range = hi - lo;
    int32_t bright_rhs = (int32_t)GP_STREAK_INK_NUM * range;
    int32_t dark_rhs = (int32_t)(GP_STREAK_INK_DEN - GP_STREAK_INK_NUM) * range;
    for (int i = 0; i < W * H; i++)
    {
        int32_t lhs = (int32_t)GP_STREAK_INK_DEN * (lum[i] - lo);
        s_scell_ink[i] = is_light ? (lhs < dark_rhs ? 1u : 0u)
                                  : (lhs > bright_rhs ? 1u : 0u);
    }

    /* Ink bounding box (rows ry0..ry1, cols cx0..cx1). */
    int ry0 = -1, ry1 = -1, cx0 = -1, cx1 = -1;
    for (int r = 0; r < H; r++)
        for (int c = 0; c < W; c++)
            if (s_scell_ink[r * W + c])
            {
                if (ry0 < 0) { ry0 = r; }
                ry1 = r;
                if (cx0 < 0 || c < cx0) { cx0 = c; }
                if (c > cx1) { cx1 = c; }
            }
    if (ry0 < 0)
    {
        for (int i = 0; i < GP_STREAK_LEN; i++) { s_scell_cov[i] = 0u; }
        return;
    }
    int rh = ry1 - ry0 + 1, rw = cx1 - cx0 + 1;

    for (int gr = 0; gr < GP_STREAK_GLYPH_ROWS; gr++)
    {
        int ya = gp_edge(gr, rh, GP_STREAK_GLYPH_ROWS);
        int yb = gp_edge(gr + 1, rh, GP_STREAK_GLYPH_ROWS);
        if (yb <= ya) { yb = ya + 1; }
        if (yb > rh) { yb = rh; }   /* clamp past the bbox → empty cell (matches numpy slice) */
        for (int gc = 0; gc < GP_STREAK_GLYPH_COLS; gc++)
        {
            int xa = gp_edge(gc, rw, GP_STREAK_GLYPH_COLS);
            int xb = gp_edge(gc + 1, rw, GP_STREAK_GLYPH_COLS);
            if (xb <= xa) { xb = xa + 1; }
            if (xb > rw) { xb = rw; }
            uint32_t sum = 0u, n = 0u;
            for (int r = ry0 + ya; r < ry0 + yb; r++)
                for (int c = cx0 + xa; c < cx0 + xb; c++)
                {
                    sum += s_scell_ink[r * W + c];
                    n++;
                }
            s_scell_cov[gr * GP_STREAK_GLYPH_COLS + gc] =
                (n != 0u) ? (uint8_t)((sum * 255u + n / 2u) / n) : 0u;
        }
    }
}

int gp_read_streak(const uint8_t *frame, int width, int height, gp_streak_raw_t *out)
{
    out->present = 0u;
    for (int i = 0; i < 3; i++) { out->digit[i] = 0u; out->known[i] = 0u; }
    if (width != GP_CANON_W || height != GP_CANON_H) { return -1; }

    /* Presence = odometer locked at final position: the fixed note-icon glyph's
     * coverage matches its locked template. Rejects both absent (dark) and mid-slide
     * (glyph off-position) frames — digit cells are only aligned when locked. */
    static const uint16_t note_roi[4] = GP_STREAK_NOTE_ROI;
    streak_cell_cov_roi(frame, width, height, note_roi, 0);
    int32_t note_sad = 0;
    for (int k = 0; k < GP_STREAK_LEN; k++) { note_sad += abs((int)s_scell_cov[k] - (int)gp_streak_note_tmpl[k]); }
    if (note_sad >= GP_STREAK_NOTE_MAX_SAD) { return 0; }
    out->present = 1u;

    for (int i = 0; i < GP_STREAK_NCELLS; i++)
    {
        const gp_streak_cell_t *cell = &gp_streak_cells[i];
        streak_cell_cov_roi(frame, width, height, cell->roi, cell->is_light);
        const uint8_t (*bank)[GP_STREAK_LEN] =
            (cell->bank == GP_STREAK_BANK_DL) ? gp_streak_tmpl_dl : gp_streak_tmpl_wd;

        int32_t best = INT32_MAX, second = INT32_MAX;
        int best_digit = 0;
        for (int d = 0; d < GP_STREAK_NDIGITS; d++)
        {
            int32_t l1 = 0;
            for (int k = 0; k < GP_STREAK_LEN; k++) { l1 += abs((int)s_scell_cov[k] - (int)bank[d][k]); }
            if (l1 < best) { second = best; best = l1; best_digit = d; }
            else if (l1 < second) { second = l1; }
        }
        out->digit[i] = (uint8_t)best_digit;
        out->known[i] = (best < GP_STREAK_UNK_DIST && (second - best) > GP_STREAK_UNK_MARGIN) ? 1u : 0u;
    }
    return 0;
}

void gp_streak_reset(gp_streak_state_t *st)
{
    st->val = 0u;
    st->seen = 0u;
    for (int i = 0; i < 3; i++) { st->dg[i] = 0u; st->pend[i] = 0xFFu; st->pend_n[i] = 0u; }
}

uint16_t gp_streak_track(gp_streak_state_t *st, const gp_streak_raw_t *raw)
{
    static const uint8_t debounce[3] = GP_STREAK_DEBOUNCE;

    if (!raw->present)
    {
        gp_streak_reset(st);   /* odometer gone (streak reset / not shown) → 0 */
        return 0u;
    }

    if (!st->seen)
    {
        if (((int)raw->known[0] + raw->known[1] + raw->known[2]) < 2) { return 0u; }  /* need ≥2 to seed */
        for (int i = 0; i < 3; i++)
        {
            st->dg[i] = raw->known[i] ? raw->digit[i] : 0u;
            st->pend[i] = 0xFFu; st->pend_n[i] = 0u;
        }
        st->seen = 1u;
    }
    else
    {
        /* A confident wheel is authoritative for its place, but a *changed* digit
         * must be confirmed by debounce[i] consecutive confident reads before it
         * commits (kills single-frame misreads on the slow wheels without locking;
         * units is immediate). A committed change carries: unreadable lower places
         * reset to 0. An unreadable place otherwise holds its last digit. */
        uint8_t s_dg[3], s_pend[3], s_pn[3];
        for (int i = 0; i < 3; i++) { s_dg[i] = st->dg[i]; s_pend[i] = st->pend[i]; s_pn[i] = st->pend_n[i]; }

        int carry = 0;
        for (int i = 0; i < 3; i++)  /* hundreds → tens → units */
        {
            if (raw->known[i])
            {
                uint8_t r = raw->digit[i];
                if (r == st->dg[i])
                {
                    st->pend[i] = 0xFFu; st->pend_n[i] = 0u;  /* confirms committed */
                }
                else
                {
                    st->pend_n[i] = (r == st->pend[i]) ? (uint8_t)(st->pend_n[i] + 1u) : 1u;
                    st->pend[i] = r;
                    if (st->pend_n[i] >= debounce[i])  /* change confirmed */
                    {
                        st->dg[i] = r; st->pend[i] = 0xFFu; st->pend_n[i] = 0u; carry = 1;
                    }
                }
            }
            else if (carry)
            {
                st->dg[i] = 0u; st->pend[i] = 0xFFu; st->pend_n[i] = 0u;
            }
        }

        /* Units wrap → tens carry (anticipate the tens step): a confident units read
         * that dropped sharply (9→0-ish) means the units wheel wrapped, so step the
         * current (debounced) tens by one, cascading to the hundreds. Applies the
         * carry the instant the ones rolls (49→50) rather than waiting for the tens
         * wheel to be re-read (which lagged: 49→40→50). Works off the debounced
         * digits and fires only on a genuine wrap, so it can't fight a downward tens
         * correction or run away (avoids "stuck 30-high, can't recover"). */
        if (raw->known[2] && (int)s_dg[2] - (int)raw->digit[2] >= GP_STREAK_WRAP_MIN)
        {
            if (++st->dg[1] > 9u) { st->dg[1] = 0u; if (st->dg[0] < 9u) { st->dg[0]++; } }
            st->pend[0] = st->pend[1] = 0xFFu;
            st->pend_n[0] = st->pend_n[1] = 0u;
        }

        /* Reject an implausible per-frame value jump (a misread that cleared
         * debounce, e.g. a hundreds wheel read mid-roll during a carry). Symmetric,
         * so it still corrects downward and never locks. */
        int new_val = st->dg[0] * 100 + st->dg[1] * 10 + st->dg[2];
        int delta = new_val - (int)st->val;
        if (delta < 0) { delta = -delta; }
        if (delta > GP_STREAK_MAX_STEP)
        {
            for (int i = 0; i < 3; i++) { st->dg[i] = s_dg[i]; st->pend[i] = s_pend[i]; st->pend_n[i] = s_pn[i]; }
        }
    }

    st->val = (uint16_t)(st->dg[0] * 100u + st->dg[1] * 10u + st->dg[2]);
    return st->val;
}
