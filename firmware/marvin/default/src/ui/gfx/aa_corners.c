#include "ui/gfx/aa_corners.h"

#include <math.h>

#include "gfx/legato/common/legato_color.h"
#include "gfx/legato/renderer/legato_renderer.h"

/* Distance of pixel (px, py) within a radius-r corner box from the arc centre, with
 * (flip_x, flip_y) orienting the box so all four corners reuse the upper-left math
 * (centre at the box's inner corner). Coverage of an arc of radius `ra` is then
 * clamp(ra + 0.5 - d): 1 fully inside, 0 fully outside, the fraction between giving
 * the 1px anti-aliased band. */
static float arc_dist(uint32_t px, uint32_t py, uint32_t r, leBool flip_x, leBool flip_y)
{
    uint32_t mx = flip_x ? (r - 1u - px) : px;
    uint32_t my = flip_y ? (r - 1u - py) : py;
    float    dx = (float)r - (float)mx - 0.5f;
    float    dy = (float)r - (float)my - 0.5f;

    return sqrtf(dx * dx + dy * dy);
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
    leColor   bg = leRenderer_GetPixel(flip_x ? ox + (int32_t)r - 1 : ox,
                                       flip_y ? oy + (int32_t)r - 1 : oy);
    uint32_t  px, py;

    for (py = 0u; py < r; py++)
    {
        for (px = 0u; px < r; px++)
        {
            float    d  = arc_dist(px, py, r, flip_x, flip_y);
            leColor  c;

            float co = arc_coverage(d, (float)r);   /* coverage inside the outer arc */

            if (bw == 0u)
            {
                c = leColorLerp(bg, fill, (uint32_t)(co * 100.0f + 0.5f), mode);
            }
            else
            {
                float ci = arc_coverage(d, (float)(r - bw));   /* coverage inside the fill */

                c = leColorLerp(bg, border, (uint32_t)(co * 100.0f + 0.5f), mode);
                c = leColorLerp(c,  fill,   (uint32_t)(ci * 100.0f + 0.5f), mode);
            }

            leRenderer_PutPixel(ox + (int32_t)px, oy + (int32_t)py, c);
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
    uint32_t px, py;

    for (py = 0u; py < r; py++)
    {
        for (px = 0u; px < r; px++)
        {
            float co = arc_coverage(arc_dist(px, py, r, flip_x, flip_y), (float)r);

            if (co >= 1.0f) { continue; }     /* fully inside → keep the image  */

            int32_t x = ox + (int32_t)px;
            int32_t y = oy + (int32_t)py;
            leColor img = leRenderer_GetPixel(x, y);
            /* co→1 keeps the image, co→0 is full bg. */
            leRenderer_PutPixel(x, y,
                                leColorLerp(bg, img, (uint32_t)(co * 100.0f + 0.5f), mode));
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
    uint32_t px, py;

    for (py = 0u; py < r; py++)
    {
        for (px = 0u; px < r; px++)
        {
            int32_t x  = ox + (int32_t)px;
            int32_t y  = oy + (int32_t)py;
            float   d  = arc_dist(px, py, r, flip_x, flip_y);
            float   co = arc_coverage(d, (float)r);
            leColor bg = sample(ctx, x, y);
            leColor c;

            if (bw == 0u)
            {
                c = leColorLerp(bg, fill, (uint32_t)(co * 100.0f + 0.5f), LE_COLOR_MODE_RGB_565);
            }
            else
            {
                float ci = arc_coverage(d, (float)(r - bw));

                c = leColorLerp(bg, border, (uint32_t)(co * 100.0f + 0.5f), LE_COLOR_MODE_RGB_565);
                c = leColorLerp(c,  fill,   (uint32_t)(ci * 100.0f + 0.5f), LE_COLOR_MODE_RGB_565);
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
