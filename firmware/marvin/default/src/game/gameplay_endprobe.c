#include "game/gameplay_endprobe.h"
#include "game/gameplay_metadata.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define GP_END_BPP 3

/* Mean luma over a box, integer throughout and floor-divided, matching
 * endprobe.box_mean exactly — the host cross-check compares byte-for-byte, so
 * the rounding is part of the contract. Sums luma per pixel (not per channel)
 * for the same reason: the >> 8 happens before the accumulate on both sides. */
static uint8_t box_mean(const uint8_t *frame, int width, const gp_end_box_t *b)
{
    uint32_t total = 0;

    for (int y = (int)b->y0; y < (int)b->y1; y++)
    {
        const uint8_t *px = frame + ((uint32_t)y * (uint32_t)width + b->x0) * GP_END_BPP;
        for (int x = (int)b->x0; x < (int)b->x1; x++, px += GP_END_BPP)
        {
            total += ((uint32_t)GP_PROBE_LUMA_B * px[0]
                    + (uint32_t)GP_PROBE_LUMA_G * px[1]
                    + (uint32_t)GP_PROBE_LUMA_R * px[2]) >> 8;
        }
    }
    return (uint8_t)(total / (uint32_t)b->pixels);
}

/* Median of the dark boxes. Median rather than min or max so one box landing on
 * something unexpected cannot drag the reference on its own. Insertion sort over
 * GP_END_N_DARK (3) elements. */
static uint8_t dark_level(const uint8_t *frame, int width)
{
    uint8_t v[GP_END_N_DARK];
    for (int k = 0; k < GP_END_N_DARK; k++)
    {
        v[k] = box_mean(frame, width, &gp_end_dark[k]);
    }
    for (int i = 1; i < GP_END_N_DARK; i++)
    {
        uint8_t x = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
        v[j + 1] = x;
    }
    return v[GP_END_N_DARK / 2];
}

int gp_end_probe(const uint8_t *frame, int width, int height,
                 int16_t *out_contrast, uint8_t *out_dark)
{
    if (frame == NULL || width != GP_CANON_W || height != GP_CANON_H)
    {
        if (out_contrast != NULL)
        {
            for (int k = 0; k < GP_END_N_BRIGHT; k++) { out_contrast[k] = 0; }
        }
        if (out_dark != NULL) { *out_dark = 0u; }
        return -1;
    }

    uint8_t d = dark_level(frame, width);
    if (out_dark != NULL) { *out_dark = d; }

    int hits = 0;
    for (int k = 0; k < GP_END_N_BRIGHT; k++)
    {
        int con = (int)box_mean(frame, width, &gp_end_bright[k]) - (int)d;
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
