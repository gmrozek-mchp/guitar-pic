#include "ui/widgets/panel_aa/widget_panel_aa.h"

#include "ui/gfx/aa_corners.h"

#include "gfx/legato/core/legato_scheme.h"
#include "gfx/legato/renderer/legato_renderer.h"
#include "gfx/legato/widget/legato_widget.h"

/* One shared vtable copy + the captured original paint. Plain panels all share the
 * base leWidget vtable, so a single overridden copy serves every caller. */
static leWidgetVTable s_aa_vt;
static void (*s_orig_paint)(leWidget*);
static leBool s_vt_ready = LE_FALSE;

/* After the classic skin paints the panel (background fill + optional border),
 * smooth its rounded corners: fill is the panel's BASE colour, a LINE border is a
 * 1px SHADOWDARK ring. Backdrop is sampled from the panel's own corner pixel — the
 * opaque parent behind it. */
static void aa_paint(leWidget* wgt)
{
    s_orig_paint(wgt);

    if (wgt->status.drawState == LE_WIDGET_DRAW_STATE_DONE &&
        wgt->style.cornerRadius > 0u)
    {
        leRect   rect;
        leColor  fill;
        leColor  border;
        uint32_t bw;

        wgt->fn->rectToScreen(wgt, &rect);

        fill   = leScheme_GetRenderColor(wgt->scheme, LE_SCHM_BASE);
        bw     = (wgt->style.borderType == LE_WIDGET_BORDER_LINE) ? 1u : 0u;
        border = (bw != 0u) ? leScheme_GetRenderColor(wgt->scheme, LE_SCHM_SHADOWDARK)
                            : fill;

        AaCorners_Render(&rect, wgt->style.cornerRadius, bw,
                         fill, border, leRenderer_CurrentColorMode());
    }
}

void PanelAA_Enable(leWidget* panel)
{
    if (!s_vt_ready)
    {
        s_aa_vt = *panel->fn;
        s_orig_paint = panel->fn->_paint;
        s_aa_vt._paint = aa_paint;
        s_vt_ready = LE_TRUE;
    }

    panel->fn = &s_aa_vt;
}

/* Round-image variant: its own vtable copy + captured paint (the paint op differs
 * from aa_paint). */
static leWidgetVTable s_ri_vt;
static void (*s_ri_orig_paint)(leWidget*);
static leBool s_ri_ready = LE_FALSE;

/* For a transparent overlay sitting directly over an image on the same layer:
 * after the (empty) overlay paints, eat the four corners of the overlay's rect back
 * to its BASE colour. The image drew into this rect first (earlier sibling), so the
 * corner blend reads real image pixels; BASE must equal the solid backdrop the
 * rounded corners should match (the dialog gray behind the layer). */
static void round_image_paint(leWidget* wgt)
{
    s_ri_orig_paint(wgt);

    if (wgt->status.drawState == LE_WIDGET_DRAW_STATE_DONE &&
        wgt->style.cornerRadius > 0u)
    {
        leRect rect;

        wgt->fn->rectToScreen(wgt, &rect);
        AaCorners_RenderRoundImage(&rect, wgt->style.cornerRadius,
                                   leScheme_GetRenderColor(wgt->scheme, LE_SCHM_BASE),
                                   leRenderer_CurrentColorMode());
    }
}

void PanelAA_EnableRoundImage(leWidget* panel)
{
    if (!s_ri_ready)
    {
        s_ri_vt = *panel->fn;
        s_ri_orig_paint = panel->fn->_paint;
        s_ri_vt._paint = round_image_paint;
        s_ri_ready = LE_TRUE;
    }

    panel->fn = &s_ri_vt;
}

/* Dot variant: own vtable copy + captured paint. */
static leWidgetVTable s_dot_vt;
static void (*s_dot_orig_paint)(leWidget*);
static leBool s_dot_ready = LE_FALSE;

/* Fill (a plain square, cornerRadius 0 — the stock rounded-rect paint hangs at
 * cornerRadius == size/2), then eat the four corners to the backdrop so the square
 * becomes an anti-aliased circle. The fill covers the whole square, so unlike
 * aa_paint we can't read the backdrop from the widget's own corner pixel — sample
 * it just outside the dot instead (uniform panel fill around a small dot). */
static void dot_paint(leWidget* wgt)
{
    s_dot_orig_paint(wgt);

    if (wgt->status.drawState == LE_WIDGET_DRAW_STATE_DONE)
    {
        leRect   rect;
        uint32_t r;

        wgt->fn->rectToScreen(wgt, &rect);
        r = (uint32_t)((rect.width < rect.height ? rect.width : rect.height) / 2);

        if (r > 0u && rect.x > 0)
        {
            leColor bg = leRenderer_GetPixel(rect.x - 1, rect.y + rect.height / 2);
            AaCorners_RenderRoundImage(&rect, r, bg, leRenderer_CurrentColorMode());
        }
    }
}

void PanelAA_EnableDot(leWidget* panel)
{
    if (!s_dot_ready)
    {
        s_dot_vt = *panel->fn;
        s_dot_orig_paint = panel->fn->_paint;
        s_dot_vt._paint = dot_paint;
        s_dot_ready = LE_TRUE;
    }

    panel->fn = &s_dot_vt;
}
