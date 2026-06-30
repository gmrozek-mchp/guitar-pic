#include "ui/gfx/aa_corners.h"

#include <math.h>

#include "gfx/legato/common/legato_color.h"
#include "gfx/legato/renderer/legato_renderer.h"

/* Repaint one radius×radius corner box. (ox, oy) is its top-left in screen space;
 * (flip_x, flip_y) orient the arc centre so all four corners reuse the upper-left
 * math (centre at the box's inner corner). bg is sampled from the box's outer
 * pixel — the pixel furthest from the arc centre, which is always backdrop. Each
 * pixel is a blend of backdrop → border → fill by its coverage of the outer arc
 * (radius r) and inner arc (radius r-borderWidth). */
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
            uint32_t mx = flip_x ? (r - 1u - px) : px;
            uint32_t my = flip_y ? (r - 1u - py) : py;
            float    dx = (float)r - (float)mx - 0.5f;
            float    dy = (float)r - (float)my - 0.5f;
            float    d  = sqrtf(dx * dx + dy * dy);
            leColor  c;

            float co = (float)r + 0.5f - d;   /* coverage inside the outer arc */
            if (co < 0.0f) { co = 0.0f; } else if (co > 1.0f) { co = 1.0f; }

            if (bw == 0u)
            {
                c = leColorLerp(bg, fill, (uint32_t)(co * 100.0f + 0.5f), mode);
            }
            else
            {
                float ci = (float)(r - bw) + 0.5f - d;   /* coverage inside the fill */
                if (ci < 0.0f) { ci = 0.0f; } else if (ci > 1.0f) { ci = 1.0f; }

                c = leColorLerp(bg, border, (uint32_t)(co * 100.0f + 0.5f), mode);
                c = leColorLerp(c,  fill,   (uint32_t)(ci * 100.0f + 0.5f), mode);
            }

            leRenderer_PutPixel(ox + (int32_t)px, oy + (int32_t)py, c);
        }
    }
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
            uint32_t mx = flip_x ? (r - 1u - px) : px;
            uint32_t my = flip_y ? (r - 1u - py) : py;
            float    dx = (float)r - (float)mx - 0.5f;
            float    dy = (float)r - (float)my - 0.5f;
            float    d  = sqrtf(dx * dx + dy * dy);

            float co = (float)r + 0.5f - d;   /* coverage inside the outer arc */
            if (co >= 1.0f) { continue; }     /* fully inside → keep the image  */
            if (co < 0.0f)  { co = 0.0f; }

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
