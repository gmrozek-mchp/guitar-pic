#include "ui/widgets/button_aa/widget_button_aa.h"

#include "ui/gfx/aa_corners.h"

#include "gfx/legato/core/legato_scheme.h"
#include "gfx/legato/renderer/legato_renderer.h"
#include "gfx/legato/widget/legato_widget.h"

/* One shared vtable copy + the captured original paint: every button shares the
 * same classic-skin vtable, so a single overridden copy serves all callers. */
static leButtonWidgetVTable s_aa_vt;
static void (*s_orig_paint)(leButtonWidget*);
static leBool s_vt_ready = LE_FALSE;

/* After the classic skin paints the button (background, border, text), smooth its
 * rounded corners. Fill matches the skin (pressed → BACKGROUND, released → BASE);
 * a LINE border is a 1px ring in SHADOWDARK (the colour the classic line-border
 * draw uses), so the AA reproduces backdrop → border → fill at each corner. The
 * radius is whatever the button was given — AaCorners_Render handles any value. */
static void aa_paint(leButtonWidget* btn)
{
    s_orig_paint(btn);

    if (btn->widget.status.drawState == LE_WIDGET_DRAW_STATE_DONE &&
        btn->widget.style.cornerRadius > 0u)
    {
        leRect   rect;
        leColor  fill;
        leColor  border;
        uint32_t bw;

        btn->fn->rectToScreen(btn, &rect);

        fill = leScheme_GetRenderColor(btn->widget.scheme,
                                       (btn->state != LE_BUTTON_STATE_UP)
                                           ? LE_SCHM_BACKGROUND : LE_SCHM_BASE);

        bw     = (btn->widget.style.borderType == LE_WIDGET_BORDER_LINE) ? 1u : 0u;
        border = (bw != 0u) ? leScheme_GetRenderColor(btn->widget.scheme, LE_SCHM_SHADOWDARK)
                            : fill;

        AaCorners_Render(&rect, btn->widget.style.cornerRadius, bw,
                         fill, border, leRenderer_CurrentColorMode());
    }
}

void ButtonAA_Enable(leButtonWidget* btn)
{
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

/* Pressed-border variant: own vtable copy + captured paint. */
static leButtonWidgetVTable s_pb_vt;
static void (*s_pb_orig_paint)(leButtonWidget*);
static leBool s_pb_ready = LE_FALSE;

/* As aa_paint, but the 1px LINE border also changes colour while pressed — the mockup's
 * `hover:border-zinc-500` on top of `hover:bg-zinc-800`.
 *
 * It has to be an overdraw: the classic skin's line border is hard-wired to SHADOWDARK
 * with no state branch (leWidget_SkinClassic_DrawStandardLineBorder), so the only way to
 * recolour it is to draw over the four straight runs after the skin has painted, then let
 * the corner pass repaint the corner boxes in the same colour. Order matters — the side
 * helpers run the full length of each edge, and AaCorners_Render corrects the corners
 * afterwards.
 *
 * The pressed colour is the scheme's HIGHLIGHT, which Legato leaves unused for a
 * LINE-bordered button (the classic button skin reads BASE, BACKGROUND, TEXT, and the
 * bevel-only slots), so it is free to mean "pressed border" here. */
static void pressed_border_paint(leButtonWidget* btn)
{
    s_pb_orig_paint(btn);

    if (btn->widget.status.drawState == LE_WIDGET_DRAW_STATE_DONE &&
        btn->widget.style.cornerRadius > 0u)
    {
        leBool   pressed = (btn->state != LE_BUTTON_STATE_UP) ? LE_TRUE : LE_FALSE;
        leRect   rect;
        leColor  fill;
        leColor  border;
        uint32_t bw;

        btn->fn->rectToScreen(btn, &rect);

        fill   = leScheme_GetRenderColor(btn->widget.scheme,
                                         pressed ? LE_SCHM_BACKGROUND : LE_SCHM_BASE);
        bw     = (btn->widget.style.borderType == LE_WIDGET_BORDER_LINE) ? 1u : 0u;
        border = (bw != 0u) ? leScheme_GetRenderColor(btn->widget.scheme,
                                                     pressed ? LE_SCHM_HIGHLIGHT
                                                             : LE_SCHM_SHADOWDARK)
                            : fill;

        if (bw != 0u && pressed)
        {
            /* The four straight runs only, inset by the radius — the corner boxes are
             * repainted wholesale by AaCorners_Render below. Drawn with the renderer's
             * own line primitives rather than the classic skin's DrawLineBorder* helpers:
             * those are declared in legato_widget_skin_classic_common.h but not defined in
             * this Legato build, so calling them only fails at link time. */
            int32_t r  = (int32_t)btn->widget.style.cornerRadius;
            int32_t iw = rect.width  - 2 * r;
            int32_t ih = rect.height - 2 * r;

            (void)leRenderer_HorzLine(rect.x + r, rect.y,                   iw, border, 255u);
            (void)leRenderer_HorzLine(rect.x + r, rect.y + rect.height - 1, iw, border, 255u);
            (void)leRenderer_VertLine(rect.x,                  rect.y + r,  ih, border, 255u);
            (void)leRenderer_VertLine(rect.x + rect.width - 1, rect.y + r,  ih, border, 255u);
        }

        AaCorners_Render(&rect, btn->widget.style.cornerRadius, bw,
                         fill, border, leRenderer_CurrentColorMode());
    }
}

void ButtonAA_EnablePressedBorder(leButtonWidget* btn)
{
    if (!s_pb_ready)
    {
        s_pb_vt = *btn->fn;
        /* The paint to chain to is the STOCK one. If ButtonAA_Enable already wrapped
         * this button — which it has, when a caller's helper applies the plain variant
         * on construction — then btn->fn->_paint is aa_paint, and chaining to it would
         * run the corner pass twice, the first time with the un-pressed border. */
        s_pb_orig_paint = (btn->fn->_paint == aa_paint) ? s_orig_paint : btn->fn->_paint;
        s_pb_vt._paint = pressed_border_paint;
        s_pb_ready = LE_TRUE;
    }

    btn->fn = &s_pb_vt;
    btn->widget.fn = (const leWidgetVTable*)&s_pb_vt;
}
