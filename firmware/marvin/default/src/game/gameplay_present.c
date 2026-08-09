#include "game/gameplay_present.h"
#include "game/gameplay_metadata.h"

#include <math.h>
#include <stdint.h>

/* Standardization targets — must match gp_fingerprint (gameplay_classify.c) so the
 * probe reference (baked in the same space) compares in kind. */
#define GP_PRESENT_NORM_MEAN 128.0f
#define GP_PRESENT_NORM_STD  48.0f
#define GP_BPP               3

/* Per-block luma scratch (max block bw*bh). Luma = 29B+150G+77R ≤ 256*255 = 65280,
 * so uint16. Static — the single observer task is the only caller (see header). */
static uint16_t s_luma[GP_PROBE_MAX_LEN];

/* Masked L1 of one probe: standardize the block luma to mean128/std48 over the
 * probe's masked (static-chrome) pixels, then integer L1 vs the baked reference
 * over those pixels. Two passes over the block; stats and compare are masked. */
static int32_t gp_probe_l1(const uint8_t *frame, int width, const gp_probe_t *p)
{
    int x0 = p->block[0], y0 = p->block[1];
    int bw = p->bw, bh = p->bh;
    int n = bw * bh;

    double sum = 0.0, sumsq = 0.0;
    int cnt = 0;
    for (int r = 0; r < bh; r++)
    {
        const uint8_t *row = frame + ((uint32_t)(y0 + r) * (uint32_t)width + (uint32_t)x0) * GP_BPP;
        for (int c = 0; c < bw; c++)
        {
            const uint8_t *px = row + (uint32_t)c * GP_BPP;
            uint16_t lum = (uint16_t)(GP_PROBE_LUMA_B * px[0]
                                    + GP_PROBE_LUMA_G * px[1]
                                    + GP_PROBE_LUMA_R * px[2]);
            int i = r * bw + c;
            s_luma[i] = lum;
            if (p->mask[i]) { sum += lum; sumsq += (double)lum * (double)lum; cnt++; }
        }
    }
    if (cnt == 0) { return 0; }

    float mean = (float)(sum / (double)cnt);
    double vard = sumsq / (double)cnt - (double)mean * (double)mean;
    float std = (vard > 1e-6) ? (float)sqrt(vard) : 0.0f;

    int32_t l1 = 0;
    for (int i = 0; i < n; i++)
    {
        if (!p->mask[i]) { continue; }
        float v = (std > 1e-6f)
                ? ((float)s_luma[i] - mean) / std * GP_PRESENT_NORM_STD + GP_PRESENT_NORM_MEAN
                : GP_PRESENT_NORM_MEAN;
        long q = lroundf(v);
        if (q < 0)        { q = 0; }
        else if (q > 255) { q = 255; }
        int d = (int)q - (int)p->ref[i];
        l1 += (d < 0) ? -d : d;
    }
    return l1;
}

uint8_t gp_present(const uint8_t *frame, int width, int height, int32_t *out_sad_milli)
{
    if (width != GP_CANON_W || height != GP_CANON_H)
    {
        if (out_sad_milli)
        {
            for (int k = 0; k < GP_N_PROBES; k++) { out_sad_milli[k] = INT32_MAX; }
        }
        return GP_SCREEN_UNKNOWN;
    }

    float pp[GP_N_PROBES];
    for (int k = 0; k < GP_N_PROBES; k++)
    {
        int32_t l1 = gp_probe_l1(frame, width, &gp_probes[k]);
        pp[k] = (float)l1 / (float)gp_probes[k].npix;
        if (out_sad_milli) { out_sad_milli[k] = (int32_t)lroundf(pp[k] * 1000.0f); }
    }

    float p1  = pp[GP_PROBE_1P];
    float two = (pp[GP_PROBE_2PL] > pp[GP_PROBE_2PR]) ? pp[GP_PROBE_2PL] : pp[GP_PROBE_2PR];

    if (p1 <= (float)GP_PRESENT_TAU && p1 <= two) { return GP_PRESENT_SCREEN_1P; }
    if (two <= (float)GP_PRESENT_TAU)             { return GP_PRESENT_SCREEN_2P; }
    return GP_SCREEN_UNKNOWN;
}

int gp_ready_p1_present(const uint8_t *frame, int width, int height, int32_t *out_sad_milli)
{
    if (width != GP_CANON_W || height != GP_CANON_H)
    {
        if (out_sad_milli) { *out_sad_milli = INT32_MAX; }
        return -1;
    }

    int32_t l1 = gp_probe_l1(frame, width, &gp_ready_p1);
    float sad = (float)l1 / (float)gp_ready_p1.npix;
    if (out_sad_milli) { *out_sad_milli = (int32_t)lroundf(sad * 1000.0f); }
    return (sad <= (float)GP_READY_TAU) ? 1 : 0;
}
