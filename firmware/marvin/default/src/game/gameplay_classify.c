#include "game/gameplay_classify.h"
#include "game/gameplay_metadata.h"

#include <math.h>
#include <stdint.h>

/* Per-frame value normalization targets — must match the host prototype
 * (tools/gameplay/gameplay/fingerprint.py: _NORM_MEAN / _NORM_STD). */
#define GP_NORM_MEAN  128.0f
#define GP_NORM_STD   48.0f
#define GP_BPP        3

/* Split [0, extent) into `n` contiguous spans; edge(i) = round(i*extent/n). */
static int gp_edge(int i, int extent, int n)
{
    return (int)lround((double)i * (double)extent / (double)n);
}

/* j-th of `n` sample coordinates centred in their sub-cell of [lo, hi). */
static int gp_lattice(int j, int lo, int hi, int n)
{
    int c = (int)((double)lo + (((double)j + 0.5) / (double)n) * (double)(hi - lo));
    if (c < lo)      { c = lo; }
    if (c > hi - 1)  { c = hi - 1; }
    return c;
}

/* Mean BGR over the GP_FP_SAMPLES × GP_FP_SAMPLES lattice of a region. */
static void gp_region_mean(const uint8_t *frame, int w,
                           int x0, int y0, int x1, int y1, float out[3])
{
    uint32_t sb = 0u, sg = 0u, sr = 0u;
    int n = GP_FP_SAMPLES;
    for (int jy = 0; jy < n; jy++)
    {
        int y = gp_lattice(jy, y0, y1, n);
        const uint8_t *row = frame + (uint32_t)y * (uint32_t)w * GP_BPP;
        for (int jx = 0; jx < n; jx++)
        {
            int x = gp_lattice(jx, x0, x1, n);
            const uint8_t *p = row + (uint32_t)x * GP_BPP;
            sb += p[0];
            sg += p[1];
            sr += p[2];
        }
    }
    float cnt = (float)(n * n);
    out[0] = (float)sb / cnt;
    out[1] = (float)sg / cnt;
    out[2] = (float)sr / cnt;
}

/* Fingerprint scratch in normalized float space, reduced to uint8. Static — the
 * single observer task is the only caller (see header). */
static float s_fp_f[GP_FP_LEN];

int gp_fingerprint(const uint8_t *frame, int width, int height, uint8_t *out)
{
    if (width != GP_CANON_W || height != GP_CANON_H) { return -1; }

    int idx = 0;
    for (int r = 0; r < GP_FP_ROWS; r++)
    {
        int y0 = gp_edge(r, GP_CANON_H, GP_FP_ROWS);
        int y1 = gp_edge(r + 1, GP_CANON_H, GP_FP_ROWS);
        for (int c = 0; c < GP_FP_COLS; c++)
        {
            int x0 = gp_edge(c, GP_CANON_W, GP_FP_COLS);
            int x1 = gp_edge(c + 1, GP_CANON_W, GP_FP_COLS);
            float bgr[3];
            gp_region_mean(frame, width, x0, y0, x1, y1, bgr);
            s_fp_f[idx++] = bgr[0];
            s_fp_f[idx++] = bgr[1];
            s_fp_f[idx++] = bgr[2];
        }
    }

    /* Cells over a per-song / per-run region (gp_fp_keep, from the host's
     * fingerprint.EXCLUDED_REGIONS) take no part in the statistics below and are
     * emitted as 0. Excluding them from the *normalization* is the point, not just
     * from the compare: a bright magazine cover shifts the frame's mean/std and so
     * moves every other cell too. Length is unchanged, so the centroids and the L1
     * keep one shape — a zero on both sides adds nothing to the distance. */
#if GP_FP_NORMALIZE
    double sum = 0.0;
    int nkeep = 0;
    for (int i = 0; i < GP_FP_LEN; i++)
    {
        if (gp_fp_keep[i / GP_FP_BPP]) { sum += s_fp_f[i]; nkeep++; }
    }
    float mean = (nkeep > 0) ? (float)(sum / (double)nkeep) : 0.0f;
    double var = 0.0;
    for (int i = 0; i < GP_FP_LEN; i++)
    {
        if (!gp_fp_keep[i / GP_FP_BPP]) { continue; }
        double d = (double)s_fp_f[i] - (double)mean;
        var += d * d;
    }
    float std = (nkeep > 0) ? (float)sqrt(var / (double)nkeep) : 0.0f;
    for (int i = 0; i < GP_FP_LEN; i++)
    {
        if (!gp_fp_keep[i / GP_FP_BPP]) { out[i] = 0u; continue; }
        float v = (std > 1e-6f)
                ? ((s_fp_f[i] - mean) / std * GP_NORM_STD + GP_NORM_MEAN)
                : GP_NORM_MEAN;
        long q = lroundf(v);
        if (q < 0)        { q = 0; }
        else if (q > 255) { q = 255; }
        out[i] = (uint8_t)q;
    }
#else
    for (int i = 0; i < GP_FP_LEN; i++)
    {
        if (!gp_fp_keep[i / GP_FP_BPP]) { out[i] = 0u; continue; }
        long q = lroundf(s_fp_f[i]);
        if (q < 0)        { q = 0; }
        else if (q > 255) { q = 255; }
        out[i] = (uint8_t)q;
    }
#endif
    return 0;
}

static uint8_t s_fp[GP_FP_LEN];

uint8_t gp_classify(const uint8_t *frame, int width, int height,
                    int32_t *out_best_dist, int32_t *out_margin)
{
    if (gp_fingerprint(frame, width, height, s_fp) != 0)
    {
        if (out_best_dist) { *out_best_dist = INT32_MAX; }
        if (out_margin)    { *out_margin = 0; }
        return GP_SCREEN_UNKNOWN;
    }

    int32_t best = INT32_MAX, second = INT32_MAX;
    int best_k = -1;
    for (int k = 0; k < GP_N_SCREENS; k++)
    {
        const uint8_t *cen = gp_centroids[k];
        int32_t d = 0;
        for (int i = 0; i < GP_FP_LEN; i++)
        {
            int diff = (int)s_fp[i] - (int)cen[i];
            d += (diff < 0) ? -diff : diff;
        }
        if (d < best)        { second = best; best = d; best_k = k; }
        else if (d < second) { second = d; }
    }

    int32_t margin = second - best;
    if (out_best_dist) { *out_best_dist = best; }
    if (out_margin)    { *out_margin = margin; }

    if (best <= GP_CLS_T_ABS && margin >= GP_CLS_T_MARGIN)
    {
        return (uint8_t)best_k;
    }
    return GP_SCREEN_UNKNOWN;
}
