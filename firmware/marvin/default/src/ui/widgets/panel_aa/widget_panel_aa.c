#include "ui/widgets/panel_aa/widget_panel_aa.h"

#include "ui/gfx/aa_corners.h"
#include "ui/gfx/aa_shape.h"
#include "ui/gfx/vec_draw.h"

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

/* A capsule in the panel's BASE colour: a disc when the panel is square, a stadium
 * otherwise (the ends round by min(w,h)/2). Coverage is computed analytically per pixel as
 * the distance to the capsule's spine — the segment joining the two end-cap centres, which
 * degenerates to a point for a disc — so one shape is one pass with no case analysis.
 *
 * Drawn by AaShape_Capsule, deliberately NOT by `leDraw_VectorArcFill`, which this used to
 * use and which costs ~5.7 ms for a radius-6 disc on this core (see that header).
 *
 * Nothing else may paint the panel's background: the shape blends against whatever is
 * behind it, so a skin fill underneath would leave square corners. PanelAA_EnableDot
 * clears the background type for that reason.
 *
 * Drawn at the panel's own Legato alpha (255 unless setAlphaEnabled + setAlphaAmount say
 * otherwise), which is what lets a caller fade one; coverage scales it, so the antialiased
 * edge stays correct at every level. The blend reads the backdrop the layer's opaque
 * parents have just repainted under the damaged rect, so a fade never accumulates. */
static void dot_paint(leWidget* wgt)
{
    leRect   rect;
    uint32_t alpha;

    s_dot_orig_paint(wgt);

    if (wgt->status.drawState != LE_WIDGET_DRAW_STATE_DONE) { return; }

    alpha = wgt->fn->getCumulativeAlphaAmount(wgt);

    if (alpha == 0u) { return; }

    wgt->fn->rectToScreen(wgt, &rect);

    if (rect.width < 2 || rect.height < 2) { return; }

    AaShape_Capsule(&rect, leScheme_GetRenderColor(wgt->scheme, LE_SCHM_BASE), alpha);
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
    panel->fn->setBackgroundType(panel, LE_WIDGET_BACKGROUND_NONE);
}

/* Top-rounded variant: own vtable copy + captured paint. */
static leWidgetVTable s_top_vt;
static void (*s_top_orig_paint)(leWidget*);
static leBool s_top_ready = LE_FALSE;

/* One rounded rect fill in the panel's BASE colour, radii on the top corners only, so
 * the shape stands on the plot floor. The whole body is redrawn from the current rect
 * every paint — nothing is carried over from the previous size — which is what makes it
 * safe on a bar whose height and y move on every refresh.
 *
 * As with dot_paint, nothing else may paint the background: the arcs blend against what
 * is behind the panel, and a skin fill underneath would square them off again. */
static void round_top_paint(leWidget* wgt)
{
    leRect rect;

    s_top_orig_paint(wgt);

    if (wgt->status.drawState != LE_WIDGET_DRAW_STATE_DONE) { return; }

    wgt->fn->rectToScreen(wgt, &rect);

    if (rect.width < 1 || rect.height < 1) { return; }

    /* AaShape_RRectCorners, not leDraw_VectorRectFill: the vector rect fill supersamples 8x
     * per pixel, and these are the bus screen's TX bars — seven of them, up to 46x204, resized
     * and repainted every second. That was the single most expensive thing on that screen. See
     * the journal, 2026-08-07 (night). */
    AaShape_RRectCorners(&rect, (int32_t)wgt->style.cornerRadius, AA_CORNER_TOP,
                         leScheme_GetRenderColor(wgt->scheme, LE_SCHM_BASE), 255u);
}

void PanelAA_EnableRoundTop(leWidget* panel, uint32_t radius)
{
    if (!s_top_ready)
    {
        s_top_vt = *panel->fn;
        s_top_orig_paint = panel->fn->_paint;
        s_top_vt._paint = round_top_paint;
        s_top_ready = LE_TRUE;
    }

    panel->fn = &s_top_vt;
    panel->fn->setCornerRadius(panel, radius);
    panel->fn->setBackgroundType(panel, LE_WIDGET_BACKGROUND_NONE);
}

/* Left-accent variant: own vtable copy + captured paint, plus the two widths, which
 * the variant shares the way it shares the vtable (one node-card geometry). */
static leWidgetVTable s_la_vt;
static void (*s_la_orig_paint)(leWidget*);
static leBool s_la_ready = LE_FALSE;
static uint32_t s_la_edge_w, s_la_border_w;

static void left_accent_paint(leWidget* wgt)
{
    s_la_orig_paint(wgt);

    if (wgt->status.drawState == LE_WIDGET_DRAW_STATE_DONE &&
        wgt->style.cornerRadius > 0u)
    {
        leRect rect;

        wgt->fn->rectToScreen(wgt, &rect);
        AaCorners_RenderLeftEdge(&rect, wgt->style.cornerRadius,
                                 s_la_edge_w, s_la_border_w,
                                 leScheme_GetRenderColor(wgt->scheme, LE_SCHM_BASE),
                                 leRenderer_CurrentColorMode());
    }
}

void PanelAA_EnableLeftAccent(leWidget* panel, uint32_t edgeWidth, uint32_t borderWidth)
{
    if (!s_la_ready)
    {
        s_la_vt = *panel->fn;
        s_la_orig_paint = panel->fn->_paint;
        s_la_vt._paint = left_accent_paint;
        s_la_ready = LE_TRUE;
    }

    s_la_edge_w   = edgeWidth;
    s_la_border_w = borderWidth;

    panel->fn = &s_la_vt;
    panel->fn->setBackgroundType(panel, LE_WIDGET_BACKGROUND_NONE);
}
