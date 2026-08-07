#include "ui/gfx/aa_corners.h"

#include <math.h>

#include "gfx/legato/common/legato_color.h"
#include "gfx/legato/renderer/legato_renderer.h"

/* Squared distance of pixel (px, py) within a radius-r corner box from the arc centre, in
 * HALF-pixel units, with (flip_x, flip_y) orienting the box so all four corners reuse the
 * upper-left maths (centre at the box's inner corner).
 *
 * Half-pixel units are what make this integer: the offsets are r - m - 0.5, so doubling
 * them lands on odd integers exactly. Coverage of an arc of radius `ra` is
 * clamp(ra + 0.5 - d) — 1 fully inside, 0 fully outside, the fraction between being the 1px
 * antialiased band — and in these units that is clamp(((2·ra + 1) - sqrt(d2)) / 2), so the
 * two saturated cases are integer comparisons against (2·ra ∓ 1)² and only band pixels ever
 * need a square root.
 *
 * This used to be `sqrtf` plus float coverage maths on *every* pixel of *every* corner box —
 * 576 px per radius-12 card, on a core with no FPU where each float operation is a libgcc
 * call at ~80 cycles — and it runs for every card and button in the UI. See the journal,
 * 2026-08-07 (night).
 *
 * Ranges: d2 ≤ (2r)²·2, so radius ≤ 45 keeps `d2 << 16` inside int32. */
static uint32_t arc_dist2(uint32_t px, uint32_t py, uint32_t r, leBool flip_x, leBool flip_y)
{
    uint32_t mx = flip_x ? (r - 1u - px) : px;
    uint32_t my = flip_y ? (r - 1u - py) : py;
    int32_t  dx = (int32_t)(2u * r) - (int32_t)(2u * mx) - 1;
    int32_t  dy = (int32_t)(2u * r) - (int32_t)(2u * my) - 1;

    return (uint32_t)((dx * dx) + (dy * dy));
}

/* Integer square root, bit-by-bit. ~16 iterations of a shift and a conditional subtract
 * against a soft-float `sqrtf` call, and exact for the values used here. */
static uint32_t isqrt32(uint32_t v)
{
    uint32_t res = 0u;
    uint32_t bit = 1u << 30;

    while (bit > v) { bit >>= 2; }

    while (bit != 0u)
    {
        if (v >= (res + bit))
        {
            v  -= res + bit;
            res = (res >> 1) + bit;
        }
        else
        {
            res >>= 1;
        }
        bit >>= 2;
    }

    return res;
}

/* Arc coverage as a 0..COV_ONE weight, from the squared half-pixel distance. `ra2_in` and
 * `ra2_out` are (2·ra - 1)² and (2·ra + 1)², precomputed by the caller so the saturated
 * cases cost one comparison. */
#define COV_ONE  256u

static uint32_t arc_cov(uint32_t d2, uint32_t ra, uint32_t ra2_in, uint32_t ra2_out)
{
    if (d2 <= ra2_in)  { return COV_ONE; }
    if (d2 >= ra2_out) { return 0u; }

    /* 256·sqrt(d2), so the ramp resolves to 1/256 of a pixel. */
    uint32_t s = isqrt32(d2 << 16);
    uint32_t t = ((2u * ra) + 1u) * COV_ONE;

    return (s >= t) ? 0u : ((t - s) / 2u);
}

/* Coverage weight → leColorLerp's 0..100 percent. */
static uint32_t cov_pct(uint32_t cov)
{
    return ((cov * 100u) + (COV_ONE / 2u)) / COV_ONE;
}

/* Float forms, kept for AaCorners_RenderLeftEdge alone: its inner boundary is an ELLIPSE
 * whose radius along each ray is solved per pixel, so there is no fixed threshold to
 * compare a squared distance against and the integer shortcut above does not apply. It is
 * also the coldest of these paths — one call per node card on the System Info screen. */
static float arc_dist(uint32_t px, uint32_t py, uint32_t r, leBool flip_x, leBool flip_y)
{
    uint32_t mx = flip_x ? (r - 1u - px) : px;
    uint32_t my = flip_y ? (r - 1u - py) : py;
    float    dx = (float)r - (float)mx - 0.5f;
    float    dy = (float)r - (float)my - 0.5f;

    return sqrtf((dx * dx) + (dy * dy));
}

static float arc_coverage(float d, float ra)
{
    float c = ra + 0.5f - d;

    if (c < 0.0f) { return 0.0f; }
    if (c > 1.0f) { return 1.0f; }
    return c;
}

/* Repaint one radius×radius corner box. (ox, oy) is its top-left in screen space.
 * bg is sampled from the box's outer pixel — the pixel furthest from the arc centre,
 * which is always backdrop. Each pixel is a blend of backdrop → border → fill by its
 * coverage of the outer arc (radius r) and inner arc (radius r-borderWidth). */
static void blend_corner(int32_t ox, int32_t oy, uint32_t r, uint32_t bw,
                         leColor fill, leColor border, leColorMode mode,
                         leBool flip_x, leBool flip_y)
{
    /* Clip to the rect actually being drawn. This is a correctness fix, not just a saving:
     * these writes used to go through the UNCHECKED `leRenderer_PutPixel`, so a damage rect
     * smaller than the widget sent the corner boxes writing outside the scratch buffer — very
     * likely why every screen in this codebase invalidates whole cards. Culling the box also
     * makes partial invalidation cheap, since a corner is usually nowhere near the rect that
     * actually changed. */
    leRect box  = { .x = ox, .y = oy, .width = (int32_t)r, .height = (int32_t)r };
    leRect clip;

    if (leRenderer_CullDrawRect(&box) == LE_TRUE) { return; }

    leRenderer_ClipDrawRect(&box, &clip);

    if (clip.width < 1 || clip.height < 1) { return; }

    /* Backdrop comes from the pixel furthest from the arc centre — the one guaranteed to be
     * outside the arc. Taken from the CLIPPED box so the read stays in bounds; if the clip
     * excludes the arc's outside entirely then every surviving pixel saturates and bg is
     * never used. */
    leColor   bg = leRenderer_GetPixel(flip_x ? (clip.x + clip.width  - 1) : clip.x,
                                       flip_y ? (clip.y + clip.height - 1) : clip.y);
    uint32_t  ri = (bw < r) ? (r - bw) : 0u;    /* inner (fill) arc radius */
    uint32_t  o_in  = ((2u * r) - 1u) * ((2u * r) - 1u);
    uint32_t  o_out = ((2u * r) + 1u) * ((2u * r) + 1u);
    uint32_t  i_in  = (ri > 0u) ? (((2u * ri) - 1u) * ((2u * ri) - 1u)) : 0u;
    uint32_t  i_out = (ri > 0u) ? (((2u * ri) + 1u) * ((2u * ri) + 1u)) : 0u;
    int32_t   px, py;

    for (py = clip.y - oy; py < (clip.y - oy) + clip.height; py++)
    {
        for (px = clip.x - ox; px < (clip.x - ox) + clip.width; px++)
        {
            uint32_t d2 = arc_dist2((uint32_t)px, (uint32_t)py, r, flip_x, flip_y);
            uint32_t co, ci;
            leColor  c;

            /* Outside the outer arc the result is `bg`, which is what the pixel already
             * holds — so there is nothing to write. This is ~a quarter of every box. */
            if (d2 >= o_out) { continue; }

            if (bw == 0u)
            {
                if (d2 <= o_in) { c = fill; }    /* saturated: no lerp needed */
                else
                {
                    co = arc_cov(d2, r, o_in, o_out);
                    c  = leColorLerp(bg, fill, cov_pct(co), mode);
                }
            }
            else if ((ri > 0u) && (d2 <= i_in))
            {
                c = fill;                        /* fully inside the fill arc */
            }
            else if (d2 <= o_in && ((ri == 0u) || (d2 >= i_out)))
            {
                c = border;                      /* between the arcs: solid border */
            }
            else
            {
                co = arc_cov(d2, r, o_in, o_out);
                ci = (ri > 0u) ? arc_cov(d2, ri, i_in, i_out) : 0u;

                c = leColorLerp(bg, border, cov_pct(co), mode);
                c = leColorLerp(c,  fill,   cov_pct(ci), mode);
            }

            leRenderer_PutPixel(ox + px, oy + py, c);
        }
    }
}

/* One left corner of the accent edge. (ox, oy) is the corner box's top-left in screen
 * space; flip_y selects bottom-left. (u, v) are the pixel's distances from the rect's
 * left edge and from its nearer horizontal edge, which is the frame CSS mitres in. */
static void accent_corner(int32_t ox, int32_t oy, uint32_t r,
                          uint32_t ew, uint32_t bw, leColor accent,
                          leColorMode mode, leBool flip_y)
{
    float rx = (float)r - (float)ew;   /* inner ellipse: thick at the left edge  */
    float ry = (float)r - (float)bw;   /*                thin at the top/bottom  */

    for (uint32_t py = 0u; py < r; py++)
    {
        for (uint32_t px = 0u; px < r; px++)
        {
            uint32_t my = flip_y ? (r - 1u - py) : py;
            float    u  = (float)px + 0.5f;          /* from the left edge  */
            float    v  = (float)my + 0.5f;          /* from the top edge   */
            float    dx = (float)r - u;              /* from the arc centre */
            float    dy = (float)r - v;
            float    d  = sqrtf(dx * dx + dy * dy);
            float    ra, band, mitre, cov;
            leColor  c;

            /* Polar form of the inner ellipse along this ray, so the AA band is
             * measured radially exactly as the circular path measures it. */
            float den = sqrtf((ry * dx) * (ry * dx) + (rx * dy) * (rx * dy));
            ra = (den > 0.0f) ? (rx * ry * d / den) : rx;

            band = arc_coverage(d, (float)r) - arc_coverage(d, ra);
            if (band <= 0.0f) { continue; }
            if (band > 1.0f)  { band = 1.0f; }

            /* Signed distance to the mitre, softened over a pixel. Below it (toward
             * the left edge) is this border's; above it belongs to the top/bottom. */
            mitre = (v * (float)ew - u * (float)bw)
                    / sqrtf((float)(ew * ew + bw * bw)) + 0.5f;
            if (mitre <= 0.0f) { continue; }
            if (mitre > 1.0f)  { mitre = 1.0f; }

            cov = band * mitre;
            c   = leRenderer_GetPixel(ox + (int32_t)px, oy + (int32_t)py);
            c   = leColorLerp(c, accent, (uint32_t)(cov * 100.0f + 0.5f), mode);

            leRenderer_PutPixel(ox + (int32_t)px, oy + (int32_t)py, c);
        }
    }
}

void AaCorners_RenderLeftEdge(const leRect *rect, uint32_t radius,
                              uint32_t edgeWidth, uint32_t borderWidth,
                              leColor accent, leColorMode mode)
{
    int32_t r = (int32_t)radius;
    int32_t y;

    if (radius == 0u || edgeWidth == 0u) { return; }

    /* Straight run between the two corner boxes — no AA, it is axis-aligned. */
    for (y = rect->y + r; y < rect->y + rect->height - r; y++)
    {
        for (int32_t x = rect->x; x < rect->x + (int32_t)edgeWidth; x++)
        {
            leRenderer_PutPixel(x, y, accent);
        }
    }

    accent_corner(rect->x, rect->y, radius, edgeWidth, borderWidth,
                  accent, mode, LE_FALSE);
    accent_corner(rect->x, rect->y + rect->height - r, radius, edgeWidth, borderWidth,
                  accent, mode, LE_TRUE);
}

void AaCorners_Render(const leRect *rect, uint32_t radius, uint32_t borderWidth,
                      leColor fill, leColor border, leColorMode mode)
{
    int32_t r = (int32_t)radius;

    if (radius == 0u) { return; }

    blend_corner(rect->x,                       rect->y,                        radius, borderWidth, fill, border, mode, LE_FALSE, LE_FALSE);
    blend_corner(rect->x + rect->width - r,     rect->y,                        radius, borderWidth, fill, border, mode, LE_TRUE,  LE_FALSE);
    blend_corner(rect->x,                       rect->y + rect->height - r,     radius, borderWidth, fill, border, mode, LE_FALSE, LE_TRUE);
    blend_corner(rect->x + rect->width - r,     rect->y + rect->height - r,     radius, borderWidth, fill, border, mode, LE_TRUE,  LE_TRUE);
}

/* Eat one radius×radius corner back to `bg`. Same arc geometry as blend_corner,
 * but the per-pixel "inner" colour is the pixel already there (the image), so the
 * inside of the arc is preserved and only the outside wedge fades to bg. Pixels
 * fully inside the arc are skipped entirely (left as the image drew them). */
static void round_corner(int32_t ox, int32_t oy, uint32_t r, leColor bg,
                         leColorMode mode, leBool flip_x, leBool flip_y)
{
    int32_t px, py;

    uint32_t o_in  = ((2u * r) - 1u) * ((2u * r) - 1u);
    uint32_t o_out = ((2u * r) + 1u) * ((2u * r) + 1u);
    leRect   box   = { .x = ox, .y = oy, .width = (int32_t)r, .height = (int32_t)r };
    leRect   clip;

    if (leRenderer_CullDrawRect(&box) == LE_TRUE) { return; }

    leRenderer_ClipDrawRect(&box, &clip);

    if (clip.width < 1 || clip.height < 1) { return; }

    for (py = clip.y - oy; py < (clip.y - oy) + clip.height; py++)
    {
        for (px = clip.x - ox; px < (clip.x - ox) + clip.width; px++)
        {
            uint32_t d2 = arc_dist2((uint32_t)px, (uint32_t)py, r, flip_x, flip_y);
            uint32_t co;
            int32_t  x, y;

            if (d2 <= o_in) { continue; }     /* fully inside → keep the image  */

            x = ox + px;
            y = oy + py;

            /* Fully outside is plain bg, with no need to read the image back. */
            if (d2 >= o_out)
            {
                leRenderer_PutPixel(x, y, bg);
                continue;
            }

            co = arc_cov(d2, r, o_in, o_out);
            /* co→1 keeps the image, co→0 is full bg. */
            leRenderer_PutPixel(x, y,
                                leColorLerp(bg, leRenderer_GetPixel(x, y),
                                            cov_pct(co), mode));
        }
    }
}

void AaCorners_RenderRoundImage(const leRect *rect, uint32_t radius,
                                leColor bg, leColorMode mode)
{
    int32_t r = (int32_t)radius;

    if (radius == 0u) { return; }

    round_corner(rect->x,                   rect->y,                    radius, bg, mode, LE_FALSE, LE_FALSE);
    round_corner(rect->x + rect->width - r, rect->y,                    radius, bg, mode, LE_TRUE,  LE_FALSE);
    round_corner(rect->x,                   rect->y + rect->height - r, radius, bg, mode, LE_FALSE, LE_TRUE);
    round_corner(rect->x + rect->width - r, rect->y + rect->height - r, radius, bg, mode, LE_TRUE,  LE_TRUE);
}

/* One corner of the surface flavour: same arc geometry, but the backdrop comes from
 * `sample` per pixel and the writes go straight into the RGB565 surface. */
static void surface_corner(uint16_t *surface, uint32_t stride,
                           int32_t ox, int32_t oy, uint32_t r, uint32_t bw,
                           leColor fill, leColor border,
                           aa_backdrop_fn sample, void *ctx,
                           leBool flip_x, leBool flip_y)
{
    uint32_t ri    = (bw < r) ? (r - bw) : 0u;
    uint32_t o_in  = ((2u * r) - 1u) * ((2u * r) - 1u);
    uint32_t o_out = ((2u * r) + 1u) * ((2u * r) + 1u);
    uint32_t i_in  = (ri > 0u) ? (((2u * ri) - 1u) * ((2u * ri) - 1u)) : 0u;
    uint32_t i_out = (ri > 0u) ? (((2u * ri) + 1u) * ((2u * ri) + 1u)) : 0u;
    uint32_t px, py;

    for (py = 0u; py < r; py++)
    {
        for (px = 0u; px < r; px++)
        {
            int32_t  x  = ox + (int32_t)px;
            int32_t  y  = oy + (int32_t)py;
            uint32_t d2 = arc_dist2(px, py, r, flip_x, flip_y);
            uint32_t co, ci;
            leColor  c;

            /* Unlike blend_corner this cannot skip the outside: the destination is a fresh
             * surface, not pixels already holding the backdrop, so bg must be written. */
            if (d2 >= o_out)
            {
                c = sample(ctx, x, y);
            }
            else if ((bw != 0u) && (ri > 0u) && (d2 <= i_in))
            {
                c = fill;
            }
            else if ((bw == 0u) && (d2 <= o_in))
            {
                c = fill;
            }
            else if ((bw != 0u) && (d2 <= o_in) && ((ri == 0u) || (d2 >= i_out)))
            {
                c = border;
            }
            else
            {
                leColor bg = sample(ctx, x, y);

                co = arc_cov(d2, r, o_in, o_out);

                if (bw == 0u)
                {
                    c = leColorLerp(bg, fill, cov_pct(co), LE_COLOR_MODE_RGB_565);
                }
                else
                {
                    ci = (ri > 0u) ? arc_cov(d2, ri, i_in, i_out) : 0u;

                    c = leColorLerp(bg, border, cov_pct(co), LE_COLOR_MODE_RGB_565);
                    c = leColorLerp(c,  fill,   cov_pct(ci), LE_COLOR_MODE_RGB_565);
                }
            }

            surface[(uint32_t)y * stride + (uint32_t)x] = (uint16_t)c;
        }
    }
}

void AaCorners_RenderSurface565(uint16_t *surface, uint32_t stride,
                                const leRect *rect, uint32_t radius, uint32_t borderWidth,
                                leColor fill, leColor border,
                                aa_backdrop_fn sample, void *ctx)
{
    int32_t r = (int32_t)radius;

    if (surface == NULL || sample == NULL || radius == 0u) { return; }

    surface_corner(surface, stride, rect->x,                   rect->y,                    radius, borderWidth, fill, border, sample, ctx, LE_FALSE, LE_FALSE);
    surface_corner(surface, stride, rect->x + rect->width - r, rect->y,                    radius, borderWidth, fill, border, sample, ctx, LE_TRUE,  LE_FALSE);
    surface_corner(surface, stride, rect->x,                   rect->y + rect->height - r, radius, borderWidth, fill, border, sample, ctx, LE_FALSE, LE_TRUE);
    surface_corner(surface, stride, rect->x + rect->width - r, rect->y + rect->height - r, radius, borderWidth, fill, border, sample, ctx, LE_TRUE,  LE_TRUE);
}
