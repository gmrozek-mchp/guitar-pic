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
/* One cell's ink at its OWN threshold — see amp_cell_ink. */
static uint8_t s_cell[GP_AMP2P_BAND_H * GP_AMP2P_CELL_W];
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

/* Fill s_ink. The threshold range comes from the *wide grid span* only — the amp's
 * bright gold chrome sits outside the strip and would otherwise set the range and wash
 * the digits out. The reference span is the 5-cell wide grid rather than the widest
 * layout that exists: the threshold and the blank gate were both measured against
 * these columns (mirrors amp2p._band_ink / amp2p.grid_span). */
static void band_ink(uint8_t side)
{
    int right = side_right_edge(side);
    int gx0 = cell_x0(right, GP_AMP2P_REF_CELLS, 0, GP_AMP2P_CELL_W, GP_AMP2P_PITCH);

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

/* Fill s_cell with the cell's ink at the cell's OWN luma range, not the band's.
 *
 * The LEDs pulse, so a bright glyph's strokes bloom about a pixel wider than a dim
 * one's; under a band-wide threshold the brightest digit sets the range and a dim 9
 * thins into a 5. s_ink (band-wide) is the right mask for the layout guards, which ask
 * where ink is at all — but not for the coverage fingerprint, which is compared against
 * templates cut the same way (mirrors amp2p.cell_cov's own ink_mask call). */
static void amp_cell_ink(int x0, int cw)
{
    int32_t lo = INT32_MAX, hi = INT32_MIN;
    for (int r = 0; r < GP_AMP2P_BAND_H; r++)
        for (int c = x0; c < x0 + cw; c++)
        {
            int32_t v = s_lum[r * GP_AMP2P_BAND_MAX_W + c];
            if (v < lo) { lo = v; }
            if (v > hi) { hi = v; }
        }
    int32_t rhs = (int32_t)GP_AMP2P_INK_NUM * (hi - lo);
    for (int r = 0; r < GP_AMP2P_BAND_H; r++)
        for (int c = 0; c < cw; c++)
        {
            int32_t v = s_lum[r * GP_AMP2P_BAND_MAX_W + x0 + c];
            s_cell[r * GP_AMP2P_CELL_W + c] =
                ((int32_t)GP_AMP2P_INK_DEN * (v - lo) > rhs) ? 1u : 0u;
        }
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
    amp_cell_ink(x0, cw);
    int ry0 = -1, ry1 = -1;
    for (int r = 0; r < GP_AMP2P_BAND_H; r++)
        for (int c = 0; c < cw; c++)
            if (s_cell[r * GP_AMP2P_CELL_W + c])
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
                for (int c = xa; c < xb; c++)
                {
                    sum += s_cell[r * GP_AMP2P_CELL_W + c];
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
static int match_cell(uint8_t font, int32_t *dist_out, int32_t *margin_out)
{
    int32_t best = INT32_MAX, other = INT32_MAX;
    int best_digit = 0;
    /* One font's templates are contiguous in the bank, so take the range once instead
     * of testing every template's font on every cell. */
    int lo = 0, hi = 0;
    for (int k = 0; k < GP_AMP2P_N_FONTS; k++)
    {
        if (gp_amp2p_font_id[k] == font)
        {
            lo = (int)gp_amp2p_font_start[k];
            hi = lo + (int)gp_amp2p_font_count[k];
            break;
        }
    }
    /* An unknown font leaves the range empty, so `best` stays INT32_MAX and the
     * distance gate rejects the cell — never a digit picked from no evidence. */
    for (int t = lo; t < hi; t++)
    {
        const uint8_t *tmpl = gp_amp2p_tmpl[t];
        int32_t l1 = 0;
        for (int i = 0; i < GP_AMP2P_LEN; i++) { l1 += abs((int)s_cov[i] - (int)tmpl[i]); }
        if (l1 < best) { best = l1; best_digit = (int)gp_amp2p_tmpl_digit[t]; }
    }
    for (int t = lo; t < hi; t++)
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
static void classify_cells(int right, int n, int cw, int pitch, uint8_t font,
                           int32_t *worst, int32_t *weakest)
{
    *worst = 0;
    *weakest = INT32_MAX;
    for (int i = 0; i < n; i++)
    {
        int32_t d = 0, m = 0;
        amp_cell_cov(cell_x0(right, n, i, cw, pitch), cw);
        s_digits[i] = (uint8_t)match_cell(font, &d, &m);
        if (d > *worst) { *worst = d; }
        if (m < *weakest) { *weakest = m; }
    }
    if (*weakest == INT32_MAX) { *weakest = 0; }
}

/* Whether the strip sits on the 6-digit grid: all six cells lit and the ink on-grid.
 * The same two questions the wide path asks of its own grid, which is what makes the
 * two layouts mutually exclusive — over 19 091 gated frames of a 222 692-point play,
 * 15 708 fit this grid and 3 381 the wide one, and none fit both.
 *
 * Deliberately NOT "ink appears left of the wide grid": six digits do start there, but
 * so does star-power flare (30 five-digit frames of that capture flood the container's
 * left columns), and flare one column further would read as a six-digit score
 * (mirrors amp2p.is_six_digit). */
static int is_six_digit(uint8_t side)
{
    int right = side_right_edge(side);
    for (int i = 0; i < GP_AMP2P_MAX_CELLS; i++)
    {
        int x0 = cell_x0(right, GP_AMP2P_MAX_CELLS, i, GP_AMP2P_CELL_W, GP_AMP2P_SIX_PITCH);
        if (x0 < 0 || !cell_powered(x0, GP_AMP2P_CELL_W)) { return 0; }
    }
    return spans_aligned(right, GP_AMP2P_MAX_CELLS, GP_AMP2P_CELL_W, GP_AMP2P_SIX_PITCH);
}

int gp_read_amp2p(const uint8_t *frame, int width, int height,
                  uint8_t side, gp_amp2p_t *out)
{
    out->value = -1; out->ncells = 0;
    out->layout_w = 0; out->layout_pitch = 0; out->dist = 0; out->margin = 0;

    if (frame == NULL)                                     { return -1; }
    if (width != GP_CANON_W || height != GP_CANON_H)       { return -1; }
    if (side != GP_AMP2P_SIDE_LEFT && side != GP_AMP2P_SIDE_RIGHT) { return -1; }

    band_luma(frame, width, height, side);
    band_ink(side);

    int right = side_right_edge(side);
    int cw = GP_AMP2P_CELL_W, pitch = GP_AMP2P_PITCH;
    uint8_t font = GP_AMP2P_FONT_WIDE;

    /* Digit count from the display: the powered cells must form a contiguous run
     * ending at the right edge, because the score is right-aligned. */
    int lit = 0, gap_after_lit = 0;
    for (int i = GP_AMP2P_REF_CELLS - 1; i >= 0; i--)
    {
        int x0 = cell_x0(right, GP_AMP2P_REF_CELLS, i, cw, pitch);
        if (cell_powered(x0, cw))
        {
            if (gap_after_lit) { lit = -1; break; }   /* a hole: not right-aligned */
            lit++;
        }
        else if (lit > 0) { gap_after_lit = 1; }
    }
    if (lit <= 0)
    {
        out->ncells = (uint8_t)((lit < 0) ? GP_AMP2P_REF_CELLS : 0);
        return 0;                        /* strip dark, or a non-right-aligned pattern */
    }
    int n = lit;
    out->ncells = (uint8_t)n;

    /* A 6-digit strip lights all five wide cells but cannot sit on their grid, so this
     * is exactly the case that used to end here with no value. Fall through to the
     * 6-cell grid rather than giving up; the 1-5 digit outcome is unchanged. */
    if (n == GP_AMP2P_REF_CELLS
        && !spans_aligned(right, GP_AMP2P_REF_CELLS, cw, pitch))
    {
        if (!is_six_digit(side)) { return 0; }   /* on neither measured grid */
        n = GP_AMP2P_MAX_CELLS;
        pitch = GP_AMP2P_SIX_PITCH;
        font = GP_AMP2P_FONT_SIX;
        out->ncells = (uint8_t)n;
    }

    out->layout_w = (uint8_t)cw;
    out->layout_pitch = (uint8_t)pitch;

    int32_t worst = 0, weakest = 0;
    classify_cells(right, n, cw, pitch, font, &worst, &weakest);
    out->dist = worst;
    out->margin = weakest;
    if (worst > GP_AMP2P_UNK_DIST || weakest < GP_AMP2P_UNK_MARGIN) { return 0; }

    int32_t value = 0;
    for (int i = 0; i < n; i++) { value = value * 10 + (int32_t)s_digits[i]; }
    out->value = value;
    return 0;
}
