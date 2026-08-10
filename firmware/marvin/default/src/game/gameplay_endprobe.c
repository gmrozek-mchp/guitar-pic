#include "game/gameplay_endprobe.h"
#include "game/gameplay_metadata.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define GP_END_BPP 3

/* Mean luma over a patch's n*n lattice samples at `stride` spacing. Integer
 * throughout and floor-divided, matching endprobe.patch_mean exactly — the host
 * cross-check compares byte-for-byte, so the rounding is part of the contract. */
static uint8_t patch_mean(const uint8_t *frame, int width, const gp_end_patch_t *p)
{
    int c = (p->n - 1) / 2;
    uint32_t total = 0;

    for (int i = 0; i < (int)p->n; i++)
    {
        int y = (int)p->y + (i - c) * (int)p->stride;
        const uint8_t *row = frame + (uint32_t)y * (uint32_t)width * GP_END_BPP;
        for (int j = 0; j < (int)p->n; j++)
        {
            int x = (int)p->x + (j - c) * (int)p->stride;
            const uint8_t *px = row + (uint32_t)x * GP_END_BPP;
            uint32_t lum = ((uint32_t)GP_PROBE_LUMA_B * px[0]
                          + (uint32_t)GP_PROBE_LUMA_G * px[1]
                          + (uint32_t)GP_PROBE_LUMA_R * px[2]) >> 8;
            total += lum;
        }
    }
    return (uint8_t)(total / ((uint32_t)p->n * (uint32_t)p->n));
}

/* Median of the anchor patches. Median rather than min or max so one anchor
 * landing on something unexpected cannot drag the dark reference on its own.
 * Insertion sort over GP_END_N_ANCHOR (3) elements. */
static uint8_t anchor_level(const uint8_t *frame, int width)
{
    uint8_t v[GP_END_N_ANCHOR];
    for (int k = 0; k < GP_END_N_ANCHOR; k++)
    {
        v[k] = patch_mean(frame, width, &gp_end_anchor[k]);
    }
    for (int i = 1; i < GP_END_N_ANCHOR; i++)
    {
        uint8_t x = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
        v[j + 1] = x;
    }
    return v[GP_END_N_ANCHOR / 2];
}

int gp_end_probe(const uint8_t *frame, int width, int height,
                 int16_t *out_contrast, uint8_t *out_anchor)
{
    if (frame == NULL || width != GP_CANON_W || height != GP_CANON_H)
    {
        if (out_contrast != NULL)
        {
            for (int k = 0; k < GP_END_N_BRIGHT; k++) { out_contrast[k] = 0; }
        }
        if (out_anchor != NULL) { *out_anchor = 0u; }
        return -1;
    }

    uint8_t a = anchor_level(frame, width);
    if (out_anchor != NULL) { *out_anchor = a; }

    int hits = 0;
    for (int k = 0; k < GP_END_N_BRIGHT; k++)
    {
        int con = (int)patch_mean(frame, width, &gp_end_bright[k]) - (int)a;
        if (out_contrast != NULL) { out_contrast[k] = (int16_t)con; }
        if (con >= (int)gp_end_thresh[k]) { hits++; }
    }
    return hits;
}

void gp_end_tracker_reset(gp_end_tracker_t *t)
{
    if (t == NULL) { return; }
    t->count = 0u;
    t->latched = false;
}

bool gp_end_tracker_update(gp_end_tracker_t *t, int hits)
{
    if (t == NULL) { return false; }

    if (hits >= GP_END_K_HITS)
    {
        if (t->count < 0xFFu) { t->count++; }
        if (t->count >= GP_END_CONFIRM_FRAMES) { t->latched = true; }
    }
    else
    {
        t->count = 0u;
        t->latched = false;
    }
    return t->latched;
}
