#include "game/gameplay_endlayout.h"
#include "game/gameplay_metadata.h"

#include <stdint.h>
#include <stddef.h>

#define GP_LAY_BPP 3

/* Mean luma over a box, floor-divided — identical to gameplay_endprobe.c's
 * box_mean and to endlayout.box_mean, which the host cross-check compares
 * exactly. Kept local rather than shared: the two units are independent by
 * design (the probe runs every frame, this one only on request), and a shared
 * helper would be the only coupling between them. */
static uint8_t box_mean(const uint8_t *frame, int width, const gp_end_box_t *b)
{
    uint32_t total = 0;

    for (int y = (int)b->y0; y < (int)b->y1; y++)
    {
        const uint8_t *px = frame + ((uint32_t)y * (uint32_t)width + b->x0) * GP_LAY_BPP;
        for (int x = (int)b->x0; x < (int)b->x1; x++, px += GP_LAY_BPP)
        {
            total += ((uint32_t)GP_PROBE_LUMA_B * px[0]
                    + (uint32_t)GP_PROBE_LUMA_G * px[1]
                    + (uint32_t)GP_PROBE_LUMA_R * px[2]) >> 8;
        }
    }
    return (uint8_t)(total / (uint32_t)b->pixels);
}

int gp_end_layout(const uint8_t *frame, int width, int height, int16_t *out_contrast)
{
    if (frame == NULL || width != GP_CANON_W || height != GP_CANON_H)
    {
        if (out_contrast != NULL) { *out_contrast = 0; }
        return GP_END_LAY_UNCERTAIN;
    }

    int con = (int)box_mean(frame, width, &gp_end_lay_a)
            - (int)box_mean(frame, width, &gp_end_lay_b);
    if (out_contrast != NULL) { *out_contrast = (int16_t)con; }

    if (con <= GP_END_LAY_T_PRACTICE) { return GP_END_LAY_PRACTICE; }
    if (con >= GP_END_LAY_T_FACEOFF)  { return GP_END_LAY_FACEOFF; }
    return GP_END_LAY_UNCERTAIN;
}
