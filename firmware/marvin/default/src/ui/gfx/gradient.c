#include "ui/gfx/gradient.h"

#include "gfx/legato/common/legato_color.h"
#include "gfx/legato/renderer/legato_renderer.h"

/* Bayer 8×8 threshold matrix, the classic ordered-dither kernel: values 0..63 in the
 * recursive pattern that spreads thresholds as evenly as possible, so the stipple has
 * no visible structure at a distance and is stable frame to frame (no crawling). */
static const uint8_t BAYER8[8][8] = {
    {  0, 32,  8, 40,  2, 34, 10, 42 },
    { 48, 16, 56, 24, 50, 18, 58, 26 },
    { 12, 44,  4, 36, 14, 46,  6, 38 },
    { 60, 28, 52, 20, 62, 30, 54, 22 },
    {  3, 35, 11, 43,  1, 33,  9, 41 },
    { 51, 19, 59, 27, 49, 17, 57, 25 },
    { 15, 47,  7, 39, 13, 45,  5, 37 },
    { 63, 31, 55, 23, 61, 29, 53, 21 },
};

/* Bits per channel for the modes we render into. 0 = don't dither (either ≥8 bits, so
 * there is nothing to gain, or a palette mode where per-channel truncation is
 * meaningless). */
static void mode_depth(leColorMode mode, uint8_t *r, uint8_t *g, uint8_t *b)
{
    switch (mode)
    {
        case LE_COLOR_MODE_RGB_565:   *r = 5u; *g = 6u; *b = 5u; break;
        case LE_COLOR_MODE_RGBA_5551: *r = 5u; *g = 5u; *b = 5u; break;
        case LE_COLOR_MODE_RGB_332:   *r = 3u; *g = 3u; *b = 2u; break;
        default:                      *r = 0u; *g = 0u; *b = 0u; break;
    }
}

/* Quantize one 8-bit channel to `bits`, nudged by the pixel's dither threshold, then
 * expand it back to 8 bits so leColorConvert can pack it for the target mode. The
 * threshold is scaled to one quantization step, which is what turns the truncation
 * error into a spatial pattern instead of a hard edge. */
static uint32_t dither_channel(uint32_t v8, uint8_t bits, uint8_t threshold)
{
    uint32_t levels = (1u << bits) - 1u;
    uint32_t t      = ((uint32_t)threshold * 255u) / 64u;      /* 0..252 */
    uint32_t q      = (v8 * levels + t) / 255u;

    if (q > levels) { q = levels; }
    return (q * 255u) / levels;
}

void Gradient_FillH(const leRect *rect, int32_t span_x, uint32_t span_w,
                    uint32_t rgb_from, uint32_t rgb_to, leColorMode mode)
{
    uint8_t rb, gb, bb;

    if (rect == NULL || rect->width <= 0 || rect->height <= 0 || span_w == 0u) { return; }

    mode_depth(mode, &rb, &gb, &bb);

    /* Signed channel deltas — a ramp may descend in any channel. */
    const int32_t r0 = (int32_t)((rgb_from >> 16) & 0xFFu);
    const int32_t g0 = (int32_t)((rgb_from >>  8) & 0xFFu);
    const int32_t b0 = (int32_t)( rgb_from        & 0xFFu);
    const int32_t dr = (int32_t)((rgb_to   >> 16) & 0xFFu) - r0;
    const int32_t dg = (int32_t)((rgb_to   >>  8) & 0xFFu) - g0;
    const int32_t db = (int32_t)( rgb_to          & 0xFFu) - b0;
    const int32_t last = (span_w > 1u) ? (int32_t)(span_w - 1u) : 1;

    for (int32_t x = 0; x < rect->width; x++)
    {
        /* Position along the FULL ramp, clamped: a sub-rect gets its true slice. */
        int32_t sx = rect->x + x - span_x;
        int32_t t  = (sx < 0) ? 0 : ((sx > last) ? last : sx);

        uint32_t r8 = (uint32_t)(r0 + (dr * t + last / 2) / last);
        uint32_t g8 = (uint32_t)(g0 + (dg * t + last / 2) / last);
        uint32_t b8 = (uint32_t)(b0 + (db * t + last / 2) / last);

        for (int32_t y = 0; y < rect->height; y++)
        {
            uint32_t rgb;

            if (rb == 0u)
            {
                rgb = (r8 << 16) | (g8 << 8) | b8;
            }
            else
            {
                uint8_t th = BAYER8[(uint32_t)(rect->y + y) & 7u][(uint32_t)(rect->x + x) & 7u];
                rgb = (dither_channel(r8, rb, th) << 16)
                    | (dither_channel(g8, gb, th) << 8)
                    |  dither_channel(b8, bb, th);
            }

            leRenderer_PutPixel(rect->x + x, rect->y + y,
                                leColorConvert(LE_COLOR_MODE_RGB_888, mode, rgb));
        }
    }
}
