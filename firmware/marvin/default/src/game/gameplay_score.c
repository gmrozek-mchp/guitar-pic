#include "game/gameplay_score.h"
#include "game/gameplay_metadata.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define GP_BPP             3
#define GP_SCORE_MAX_RUNS  16   /* raw runs before filtering (>= any digit count) */

/* Split [0, extent) into `n` spans; edge(i) = round(i*extent/n). Matches the
 * prototype's np.linspace bin edges and gameplay_select.c's gp_edge. */
static int gp_edge(int i, int extent, int n)
{
    return (int)lround((double)i * (double)extent / (double)n);
}

/* Digit-band scratch (one mode's band), row-major. */
static uint8_t  s_ink[GP_SCORE_BAND_MAX_H * GP_SCORE_BAND_MAX_W];  /* 1 = pixel above ink threshold */
static uint16_t s_col_ink[GP_SCORE_BAND_MAX_W];                   /* ink-pixel count per column */
static uint8_t  s_glyph[GP_SCORE_LEN];                            /* one digit's coverage mask (0-255) */
static uint16_t s_runs[GP_SCORE_MAX_RUNS][2];                     /* [x0, x1] inclusive column runs */

/* Fill s_ink (0/1) over the band rect: luma = (B*29+G*150+R*77)/256, ink where
 * luma > min + INK_FRAC*(max-min) (relative → gain/offset robust). bw/bh out. */
static void band_ink(const uint8_t *frame, int w, int h,
                     const uint16_t band[4], int *bw, int *bh)
{
    int x0 = band[0], y0 = band[1], x1 = band[2], y1 = band[3];
    int W = x1 - x0, H = y1 - y0;
    *bw = W;
    *bh = H;

    float lo = 1e30f, hi = -1e30f;
    static float lum[GP_SCORE_BAND_MAX_H * GP_SCORE_BAND_MAX_W];
    for (int r = 0; r < H; r++)
    {
        int fy = y0 + r;
        for (int c = 0; c < W; c++)
        {
            int fx = x0 + c;
            float v = 0.0f;
            if (fx >= 0 && fx < w && fy >= 0 && fy < h)
            {
                const uint8_t *p = frame + ((uint32_t)fy * (uint32_t)w + (uint32_t)fx) * GP_BPP;
                v = (float)((double)p[0] * 29.0 + (double)p[1] * 150.0 + (double)p[2] * 77.0) / 256.0f;
            }
            lum[r * W + c] = v;
            if (v < lo) { lo = v; }
            if (v > hi) { hi = v; }
        }
    }
    float thr = lo + GP_SCORE_INK_FRAC * (hi - lo);
    for (int i = 0; i < W * H; i++) { s_ink[i] = (lum[i] > thr) ? 1u : 0u; }
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
        for (int gc = 0; gc < GP_SCORE_GLYPH_COLS; gc++)
        {
            int xa = gp_edge(gc, rw, GP_SCORE_GLYPH_COLS);
            int xb = gp_edge(gc + 1, rw, GP_SCORE_GLYPH_COLS);
            if (xb <= xa) { xb = xa + 1; }
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
