#include "ui/widgets/panel_aa/widget_panel_aa.h"

#include "ui/gfx/aa_corners.h"
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

/* A capsule in the panel's BASE colour, drawn by the vector rasterizer: a disc when
 * the panel is square, a stadium otherwise (the ends round by min(w,h)/2). The middle
 * band plus one disc per end — a single rounded RectFill cannot do it, because the
 * vector rect fill clamps its corner radii to min(w,h)/4.
 *
 * Nothing else may paint the panel's background: the shape blends against whatever is
 * behind it, so a skin fill underneath would leave square corners. PanelAA_EnableDot
 * clears the background type for that reason. */
static void dot_paint(leWidget* wgt)
{
    leRect     rect;
    leRectF    band;
    leVector2  end;
    leReal_i16 radius;
    leReal_i16 offset;

    s_dot_orig_paint(wgt);

    if (wgt->status.drawState != LE_WIDGET_DRAW_STATE_DONE) { return; }

    wgt->fn->rectToScreen(wgt, &rect);

    if (rect.width < 2 || rect.height < 2) { return; }

    radius = LE_REAL_I16_FROM_INT((rect.width < rect.height) ? rect.width : rect.height) / 2;

    UiVec_RectF(&rect, &band);

    if (rect.width > rect.height)
    {
        band.extents.x -= radius;
        offset = band.extents.x;
    }
    else
    {
        band.extents.y -= radius;
        offset = band.extents.y;
    }

    leVectorArc_FillAttr disc =
    {
        .color    = leScheme_GetRenderColor(wgt->scheme, LE_SCHM_BASE),
        .alpha    = 255u,
        .hardness = LE_REAL_I16_ONE,
        .aaMode   = UI_VEC_AA,
    };

    if (offset > 0)
    {
        leVectorRect_FillAttr fill =
        {
            .color  = disc.color,
            .alpha  = 255u,
            .aaMode = UI_VEC_AA,
        };

        leDraw_VectorRectFill(&band, &fill);
    }

    end = band.origin;

    if (rect.width > rect.height) { end.x -= offset; } else { end.y -= offset; }
    leDraw_VectorArcFill(&end, radius, 0, UI_VEC_FULL_CIRCLE, &disc);

    if (offset > 0)
    {
        end = band.origin;

        if (rect.width > rect.height) { end.x += offset; } else { end.y += offset; }
        leDraw_VectorArcFill(&end, radius, 0, UI_VEC_FULL_CIRCLE, &disc);
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
    leRect     rect;
    leRectF    body;
    leReal_i16 radius;

    s_top_orig_paint(wgt);

    if (wgt->status.drawState != LE_WIDGET_DRAW_STATE_DONE) { return; }

    wgt->fn->rectToScreen(wgt, &rect);

    if (rect.width < 1 || rect.height < 1) { return; }

    radius = LE_REAL_I16_FROM_INT((int32_t)wgt->style.cornerRadius);

    UiVec_RectF(&rect, &body);

    leVectorRect_FillAttr fill =
    {
        .color          = leScheme_GetRenderColor(wgt->scheme, LE_SCHM_BASE),
        .alpha          = 255u,
        .aaMode         = UI_VEC_AA,
        .topLeftRadius  = radius,
        .topRightRadius = radius,
    };

    leDraw_VectorRectFill(&body, &fill);
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
