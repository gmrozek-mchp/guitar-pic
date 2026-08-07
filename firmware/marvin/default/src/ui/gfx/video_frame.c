#include "ui/gfx/video_frame.h"

#include "ui/gfx/aa_shape.h"

/* Coverage of the rounded rect, and of the same shape inset by the stroke, per pixel — then
 * alpha = 1 - inner (opaque over the cut and the stroke, transparent over the video) and the
 * opaque colour is the stroke scaled by its share of the opaque part, the rest being the
 * black cut.
 *
 * All integer. This runs over the WHOLE surface — 630x420 for each of its two callers — and it
 * used to evaluate a float rounded-box SDF per pixel, which on a core with no FPU is ~20
 * libgcc calls each. That is boot time, since both callers fill their surface once during
 * setup. See the journal, 2026-08-07 (night).
 *
 * `radius` and `stroke` arrive in pixels and may be fractional in principle; they are taken to
 * the nearest half pixel, which is the resolution AaShape works in. */
void VideoFrame_Fill(uint16_t *buf, uint32_t w, uint32_t h,
                     float radius, float stroke, uint32_t c4)
{
    AaRRect  outer, inner;
    int32_t  r_hp = (int32_t)((radius * 2.0f) + 0.5f);
    int32_t  s_hp = (int32_t)((stroke * 2.0f) + 0.5f);
    int32_t  hw   = (int32_t)w;
    int32_t  hh   = (int32_t)h;
    uint32_t x, y;

    if (buf == NULL || w == 0u || h == 0u) { return; }

    /* Centre in half-pixel units for a rect at the origin: 2*0 + extent. */
    AaShape_RRectSet(&outer, hw, hh, hw, hh, r_hp);
    AaShape_RRectSet(&inner, hw, hh, hw - s_hp, hh - s_hp, r_hp - s_hp);

    for (y = 0u; y < h; y++)
    {
        int32_t py = (2 * (int32_t)y) + 1;

        for (x = 0u; x < w; x++)
        {
            int32_t  px = (2 * (int32_t)x) + 1;
            uint32_t co = AaShape_RRectCov(&outer, px, py);
            uint32_t ci = AaShape_RRectCov(&inner, px, py);
            uint32_t a  = AA_COV_ONE - ci;
            uint16_t out;

            if (a == 0u)
            {
                out = 0x0000u;                      /* transparent — video shows */
            }
            else
            {
                /* Stroke's share of the opaque part; co >= ci always, since the inner shape
                 * is contained in the outer one. */
                uint32_t sc = ((c4 * (co - ci)) + (a / 2u)) / a;
                uint32_t a4 = ((a * 15u) + (AA_COV_ONE / 2u)) / AA_COV_ONE;

                if (sc > 15u) { sc = 15u; }
                if (a4 > 15u) { a4 = 15u; }
                out = (uint16_t)((a4 << 12) | (sc << 8) | (sc << 4) | sc);
            }

            buf[(y * w) + x] = out;
        }
    }
}
