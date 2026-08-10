#include "game/gameplay_amp2p.h"
#include "game/gameplay_cov.h"
#include "game/gameplay_metadata.h"

#include <stdint.h>
#include <stdlib.h>

#define GP_BPP  3

/* Band scratch. The band spans block-local columns [0, GP_AMP2P_BAND_MAX_W) — every
 * column the reader can touch — so a block-local x from cell_bounds() indexes it
 * directly, matching the host's `_band_luma`. */
#define AMP_BAND_N  (GP_AMP2P_BAND_H * GP_AMP2P_BAND_MAX_W)

static int32_t s_lum[AMP_BAND_N];              /* integer luma, B*29+G*150+R*77 */
static uint8_t s_ink[AMP_BAND_N];              /* 1 = above the band ink threshold */
static uint8_t s_cov[GP_AMP2P_LEN];            /* one cell's coverage mask (0-255) */
static uint8_t s_digits[GP_AMP2P_MAX_CELLS + 1];  /* +1: the 6-digit layouts */

/* Cell x spans are derived, not stored: the rightmost cell ends at the side's right
 * edge and each earlier cell steps back one pitch (mirrors amp2p.cell_bounds). */
static int cell_x0(int right_edge, int n, int i, int cw, int pitch)
{
    return right_edge - cw - (n - 1 - i) * pitch;
}

static int side_right_edge(uint8_t side)
{
    return (side == GP_AMP2P_SIDE_LEFT) ? GP_AMP2P_RIGHT_EDGE_L : GP_AMP2P_RIGHT_EDGE_R;
}

/* Fill s_lum over the digit band of `side`'s block. Columns outside the frame read
 * as 0, which only happens if the block itself is off-frame. */
static void band_luma(const uint8_t *frame, int w, int h, uint8_t side)
{
    const gp_probe_t *P = &gp_probes[side];
    int bx = (int)P->block[0], by = (int)P->block[1] + GP_AMP2P_BAND_Y0;

    for (int r = 0; r < GP_AMP2P_BAND_H; r++)
    {
        int fy = by + r;
        for (int c = 0; c < GP_AMP2P_BAND_MAX_W; c++)
        {
            int fx = bx + c;
            int32_t v = 0;
            if (fx >= 0 && fx < w && fy >= 0 && fy < h)
            {
                const uint8_t *p = frame + ((uint32_t)fy * (uint32_t)w + (uint32_t)fx) * GP_BPP;
                v = (int32_t)p[0] * GP_PROBE_LUMA_B
                  + (int32_t)p[1] * GP_PROBE_LUMA_G
                  + (int32_t)p[2] * GP_PROBE_LUMA_R;
            }
            s_lum[r * GP_AMP2P_BAND_MAX_W + c] = v;
        }
    }
}

/* Fill s_ink. The threshold range comes from the *grid span* only — the amp's bright
 * gold chrome sits outside the strip and would otherwise set the range and wash the
 * digits out (mirrors amp2p._band_ink). */
static void band_ink(uint8_t side)
{
    int right = side_right_edge(side);
    int gx0 = cell_x0(right, GP_AMP2P_MAX_CELLS, 0, GP_AMP2P_CELL_W, GP_AMP2P_PITCH);

    int32_t lo = INT32_MAX, hi = INT32_MIN;
    for (int r = 0; r < GP_AMP2P_BAND_H; r++)
        for (int c = gx0; c < right; c++)
        {
            int32_t v = s_lum[r * GP_AMP2P_BAND_MAX_W + c];
            if (v < lo) { lo = v; }
            if (v > hi) { hi = v; }
        }

    int32_t rhs = (int32_t)GP_AMP2P_INK_NUM * (hi - lo);
    for (int i = 0; i < AMP_BAND_N; i++)
    {
        s_ink[i] = ((int32_t)GP_AMP2P_INK_DEN * (s_lum[i] - lo) > rhs) ? 1u : 0u;
    }
}

/* Whether a cell is lit at all: unused leading cells are unpowered, so their luma
 * range is just the panel's dark gradient while a cell holding a digit spans several
 * times that. Independent of the glyph matcher, which is what makes it the
 * digit-count signal (mirrors amp2p.cell_is_powered). */
static int cell_powered(int x0, int cw)
{
    int32_t lo = INT32_MAX, hi = INT32_MIN;
    for (int r = 0; r < GP_AMP2P_BAND_H; r++)
        for (int c = x0; c < x0 + cw; c++)
        {
            int32_t v = s_lum[r * GP_AMP2P_BAND_MAX_W + c];
            if (v < lo) { lo = v; }
            if (v > hi) { hi = v; }
        }
    return (hi - lo) >= GP_AMP2P_BLANK_CONTRAST;
}

static int span_has_ink(int x0, int x1)
{
    for (int r = 0; r < GP_AMP2P_BAND_H; r++)
        for (int c = x0; c < x1; c++)
            if (s_ink[r * GP_AMP2P_BAND_MAX_W + c]) { return 1; }
    return 0;
}

/* Whether the band's ink sits on an `n`-cell grid: ink inside every cell and none in
 * the gaps between them. The layout guard — a strip the game re-laid-out fails both
 * (mirrors amp2p._spans_aligned). */
static int spans_aligned(int right, int n, int cw, int pitch)
{
    for (int i = 0; i < n; i++)
    {
        int x0 = cell_x0(right, n, i, cw, pitch);
        if (!span_has_ink(x0, x0 + cw)) { return 0; }
    }
    for (int i = 0; i + 1 < n; i++)
    {
        int a1 = cell_x0(right, n, i, cw, pitch) + cw;
        int b0 = cell_x0(right, n, i + 1, cw, pitch);
        if (b0 > a1 && span_has_ink(a1, b0)) { return 0; }
    }
    return 1;
}

/* Ink-coverage fingerprint of one cell into s_cov, mirroring amp2p.cell_cov: crop to
 * the ink bounding box in ROWS ONLY and resize that to the canonical grid.
 *
 * Columns are deliberately *not* tightened — unlike the odometer cells in
 * gameplay_score.c. The cell is monospaced, so horizontal position inside it is what
 * separates the glyphs: `1` is a centred bar, and column-cropping it would make it
 * indistinguishable from any other narrow glyph. */
static void amp_cell_cov(int x0, int cw)
{
    int ry0 = -1, ry1 = -1;
    for (int r = 0; r < GP_AMP2P_BAND_H; r++)
        for (int c = x0; c < x0 + cw; c++)
            if (s_ink[r * GP_AMP2P_BAND_MAX_W + c])
            {
                if (ry0 < 0) { ry0 = r; }
                ry1 = r;
                break;
            }
    if (ry0 < 0)
    {
        for (int i = 0; i < GP_AMP2P_LEN; i++) { s_cov[i] = 0u; }
        return;
    }
    int rh = ry1 - ry0 + 1;

    for (int gr = 0; gr < GP_AMP2P_GLYPH_ROWS; gr++)
    {
        int ya = gp_edge(gr, rh, GP_AMP2P_GLYPH_ROWS);
        int yb = gp_edge(gr + 1, rh, GP_AMP2P_GLYPH_ROWS);
        if (yb <= ya) { yb = ya + 1; }
        if (yb > rh) { yb = rh; }   /* clamp past the bbox → empty (matches the numpy slice) */
        for (int gc = 0; gc < GP_AMP2P_GLYPH_COLS; gc++)
        {
            int xa = gp_edge(gc, cw, GP_AMP2P_GLYPH_COLS);
            int xb = gp_edge(gc + 1, cw, GP_AMP2P_GLYPH_COLS);
            if (xb <= xa) { xb = xa + 1; }
            if (xb > cw) { xb = cw; }
            uint32_t sum = 0u, n = 0u;
            for (int r = ry0 + ya; r < ry0 + yb; r++)
                for (int c = x0 + xa; c < x0 + xb; c++)
                {
                    sum += s_ink[r * GP_AMP2P_BAND_MAX_W + c];
                    n++;
                }
            s_cov[gr * GP_AMP2P_GLYPH_COLS + gc] =
                (n != 0u) ? (uint8_t)((sum * 255u + n / 2u) / n) : 0u;
        }
    }
}

/* argmin integer L1 over the flat template bank. The runner-up gap is measured
 * against the nearest exemplar of a *different digit* — the bank holds
 * GP_AMP2P_VARIANTS templates per digit, so second-best is usually that digit's own
 * other brightness variant, and using it would report a near-zero margin on every
 * correct read (mirrors amp2p.match_cell). */
static int match_cell(int32_t *dist_out, int32_t *margin_out)
{
    int32_t best = INT32_MAX, other = INT32_MAX;
    int best_digit = 0;
    for (int t = 0; t < GP_AMP2P_NTMPL; t++)
    {
        const uint8_t *tmpl = gp_amp2p_tmpl[t];
        int32_t l1 = 0;
        for (int i = 0; i < GP_AMP2P_LEN; i++) { l1 += abs((int)s_cov[i] - (int)tmpl[i]); }
        if (l1 < best) { best = l1; best_digit = (int)gp_amp2p_tmpl_digit[t]; }
    }
    for (int t = 0; t < GP_AMP2P_NTMPL; t++)
    {
        if ((int)gp_amp2p_tmpl_digit[t] == best_digit) { continue; }
        int32_t l1 = 0;
        for (int i = 0; i < GP_AMP2P_LEN; i++) { l1 += abs((int)s_cov[i] - (int)gp_amp2p_tmpl[t][i]); }
        if (l1 < other) { other = l1; }
    }
    *dist_out = best;
    *margin_out = (other == INT32_MAX) ? 0 : (other - best);
    return best_digit;
}

/* Classify `n` cells left→right into s_digits; report the worst distance and the
 * weakest margin (mirrors amp2p._classify_cells). */
static void classify_cells(int right, int n, int cw, int pitch,
                           int32_t *worst, int32_t *weakest)
{
    *worst = 0;
    *weakest = INT32_MAX;
    for (int i = 0; i < n; i++)
    {
        int32_t d = 0, m = 0;
        amp_cell_cov(cell_x0(right, n, i, cw, pitch), cw);
        s_digits[i] = (uint8_t)match_cell(&d, &m);
        if (d > *worst) { *worst = d; }
        if (m < *weakest) { *weakest = m; }
    }
    if (*weakest == INT32_MAX) { *weakest = 0; }
}

/* Ink reaching left of the widest measured grid means a 6th digit: six cells cannot
 * fit the container at the measured pitch, so a 6-digit strip must start there, and
 * nothing else in that span ever inks (mirrors amp2p.has_sixth_digit). */
static int has_sixth_digit(uint8_t side)
{
    int right = side_right_edge(side);
    int gx0 = cell_x0(right, GP_AMP2P_MAX_CELLS, 0, GP_AMP2P_CELL_W, GP_AMP2P_PITCH);
    return span_has_ink(right - GP_AMP2P_CONTAINER_W, gx0);
}

/* The gp_amp2p_grid6 layout that best explains a 6-digit strip, or -1. Ink alone
 * rarely picks one (a (6,8) strip also satisfies (7,8), whose extra column falls in
 * the same blank gap), so among the candidates the ink permits the winner is the one
 * with the smallest worst-cell distance — reading a narrower strip through wider
 * cells drags a neighbouring column into every glyph, which the bank sees at once
 * (mirrors amp2p.fit_six_layout). */
static int fit_six_layout(uint8_t side)
{
    int right = side_right_edge(side);
    int best = -1;
    int32_t best_worst = INT32_MAX;
    for (int k = 0; k < GP_AMP2P_N_GRID6; k++)
    {
        int cw = (int)gp_amp2p_grid6[k][0], pitch = (int)gp_amp2p_grid6[k][1];
        if (cell_x0(right, 6, 0, cw, pitch) < 0)              { continue; }
        if (!spans_aligned(right, 6, cw, pitch))              { continue; }
        int32_t worst = 0, weakest = 0;
        classify_cells(right, 6, cw, pitch, &worst, &weakest);
        if (worst < best_worst) { best_worst = worst; best = k; }
    }
    return best;
}

int gp_read_amp2p(const uint8_t *frame, int width, int height,
                  uint8_t side, gp_amp2p_t *out)
{
    out->value = -1; out->ncells = 0; out->layout_measured = 1;
    out->layout_w = 0; out->layout_pitch = 0; out->dist = 0; out->margin = 0;

    if (frame == NULL)                                     { return -1; }
    if (width != GP_CANON_W || height != GP_CANON_H)       { return -1; }
    if (side != GP_AMP2P_SIDE_LEFT && side != GP_AMP2P_SIDE_RIGHT) { return -1; }

    band_luma(frame, width, height, side);
    band_ink(side);

    int right = side_right_edge(side);
    int n, cw = GP_AMP2P_CELL_W, pitch = GP_AMP2P_PITCH;

    if (has_sixth_digit(side))
    {
        int k = fit_six_layout(side);
        out->ncells = 6;
        out->layout_measured = 0;
        if (k < 0) { return 0; }        /* fits no candidate layout — no value */
        n = 6;
        cw = (int)gp_amp2p_grid6[k][0];
        pitch = (int)gp_amp2p_grid6[k][1];
    }
    else
    {
        /* Digit count from the display: the powered cells must form a contiguous run
         * ending at the right edge, because the score is right-aligned. */
        int lit = 0, gap_after_lit = 0;
        n = 0;
        for (int i = GP_AMP2P_MAX_CELLS - 1; i >= 0; i--)
        {
            int x0 = cell_x0(right, GP_AMP2P_MAX_CELLS, i, cw, pitch);
            if (cell_powered(x0, cw))
            {
                if (gap_after_lit) { lit = -1; break; }   /* a hole: not right-aligned */
                lit++;
            }
            else if (lit > 0) { gap_after_lit = 1; }
        }
        if (lit <= 0)
        {
            out->ncells = (uint8_t)((lit < 0) ? GP_AMP2P_MAX_CELLS : 0);
            return 0;                    /* strip dark, or a non-right-aligned pattern */
        }
        n = lit;
        out->ncells = (uint8_t)n;
        if (n == GP_AMP2P_MAX_CELLS && !spans_aligned(right, n, cw, pitch))
        {
            return 0;                    /* every cell lit but the ink is off-grid */
        }
    }

    out->ncells = (uint8_t)n;
    out->layout_w = (uint8_t)cw;
    out->layout_pitch = (uint8_t)pitch;

    int32_t worst = 0, weakest = 0;
    classify_cells(right, n, cw, pitch, &worst, &weakest);
    out->dist = worst;
    out->margin = weakest;
    if (worst > GP_AMP2P_UNK_DIST || weakest < GP_AMP2P_UNK_MARGIN) { return 0; }

    int32_t value = 0;
    for (int i = 0; i < n; i++) { value = value * 10 + (int32_t)s_digits[i]; }
    out->value = value;
    return 0;
}
