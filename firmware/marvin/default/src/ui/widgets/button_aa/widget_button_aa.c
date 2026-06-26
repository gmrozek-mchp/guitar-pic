#include "ui/widgets/button_aa/widget_button_aa.h"

#include <math.h>

#include "gfx/legato/common/legato_color.h"
#include "gfx/legato/core/legato_scheme.h"
#include "gfx/legato/renderer/legato_renderer.h"
#include "gfx/legato/widget/legato_widget.h"

#define R  BUTTON_AA_RADIUS

/* One shared vtable copy + the captured original paint: every button shares the
 * same classic-skin vtable, so a single overridden copy serves all callers. */
static leButtonWidgetVTable s_aa_vt;
static void (*s_orig_paint)(leButtonWidget*);
static leBool s_vt_ready = LE_FALSE;

/* Corner coverage [0..255]: 255 inside the arc, 0 outside, fractional on the
 * 1px transition band. Quadrant for the upper-left corner; other corners mirror
 * by index. Arc centre is the inner corner of the R x R box at (R, R). */
static uint8_t s_mask[R][R];
static leBool s_mask_ready = LE_FALSE;

static void build_mask(void)
{
    unsigned int x, y;

    for (y = 0u; y < R; y++)
    {
        for (x = 0u; x < R; x++)
        {
            float dx  = (float)R - (float)x - 0.5f;
            float dy  = (float)R - (float)y - 0.5f;
            float d   = sqrtf(dx * dx + dy * dy);
            float cov = (float)R + 0.5f - d;

            if (cov < 0.0f) cov = 0.0f;
            if (cov > 1.0f) cov = 1.0f;

            s_mask[y][x] = (uint8_t)(cov * 255.0f + 0.5f);
        }
    }

    s_mask_ready = LE_TRUE;
}

/* Smooth one corner box. (ox, oy) is its top-left in screen space; (mx, my) map
 * a box pixel to its mask cell. bg is sampled from the box's outermost pixel,
 * which the rounded fill leaves untouched. Only transition pixels are written. */
static void blend_corner(int32_t ox, int32_t oy,
                         leColor fill, leColorMode mode,
                         leBool flip_x, leBool flip_y)
{
    leColor bg = leRenderer_GetPixel(flip_x ? ox + (int32_t)R - 1 : ox,
                                     flip_y ? oy + (int32_t)R - 1 : oy);
    unsigned int px, py;

    for (py = 0u; py < R; py++)
    {
        for (px = 0u; px < R; px++)
        {
            unsigned int mx = flip_x ? (R - 1u - px) : px;
            unsigned int my = flip_y ? (R - 1u - py) : py;
            uint8_t cov = s_mask[my][mx];

            if (cov != 0u && cov != 255u)
            {
                leColor c = leColorLerp(bg, fill, (uint32_t)cov * 100u / 255u, mode);
                leRenderer_PutPixel(ox + (int32_t)px, oy + (int32_t)py, c);
            }
        }
    }
}

static void aa_corners(leButtonWidget* btn)
{
    leRect rect;
    leColor fill;
    leColorMode mode;
    int32_t rx, ry, rw, rh;

    btn->fn->rectToScreen(btn, &rect);

    rx = rect.x;
    ry = rect.y;
    rw = rect.width;
    rh = rect.height;

    /* Match the skin: pressed fills with BACKGROUND, released with BASE. */
    fill = leScheme_GetRenderColor(btn->widget.scheme,
                                   (btn->state != LE_BUTTON_STATE_UP)
                                       ? LE_SCHM_BACKGROUND : LE_SCHM_BASE);
    mode = leRenderer_CurrentColorMode();

    blend_corner(rx,                  ry,                  fill, mode, LE_FALSE, LE_FALSE);
    blend_corner(rx + rw - (int32_t)R, ry,                 fill, mode, LE_TRUE,  LE_FALSE);
    blend_corner(rx,                  ry + rh - (int32_t)R, fill, mode, LE_FALSE, LE_TRUE);
    blend_corner(rx + rw - (int32_t)R, ry + rh - (int32_t)R, fill, mode, LE_TRUE,  LE_TRUE);
}

static void aa_paint(leButtonWidget* btn)
{
    s_orig_paint(btn);

    if (btn->widget.status.drawState == LE_WIDGET_DRAW_STATE_DONE &&
        btn->widget.style.cornerRadius == R)
    {
        aa_corners(btn);
    }
}

void ButtonAA_Enable(leButtonWidget* btn)
{
    if (!s_mask_ready)
        build_mask();

    if (!s_vt_ready)
    {
        s_aa_vt = *btn->fn;
        s_orig_paint = btn->fn->_paint;
        s_aa_vt._paint = aa_paint;
        s_vt_ready = LE_TRUE;
    }

    btn->fn = &s_aa_vt;
    btn->widget.fn = (const leWidgetVTable*)&s_aa_vt;
}
