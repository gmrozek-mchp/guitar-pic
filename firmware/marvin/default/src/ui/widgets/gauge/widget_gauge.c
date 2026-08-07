#include "ui/widgets/gauge/widget_gauge.h"

#include "ui/gfx/aa_shape.h"

#include "gfx/legato/core/legato_scheme.h"

/* Shared vtable copy + captured original paint (the PanelAA pattern). */
static leWidgetVTable s_vt;
static void (*s_orig_paint)(leWidget *);
static leBool s_vt_ready = LE_FALSE;

/* Single-instance state: the widget carries no user data, so the value + colours
 * live here (one gauge on the bus screen). */
static uint32_t        s_thickness = 7u;
static uint32_t        s_permille;
static const leScheme *s_track;
static const leScheme *s_fill;

/* Paint the arc after the (empty, transparent) widget paints: the full 180° track,
 * then the fill over its left end — drawn as ONE annulus pass whose colour comes from an
 * angular wedge test, so the seam between them is never blended twice.
 *
 * AaShape_ArcRing, not leDraw_VectorArcStroke: the arc rasterizer takes 8 supersamples per
 * pixel and each runs Atan2 plus a vector normalise, and per legato_vector_review.md's own
 * correction 1 it scans the FULL circle regardless of span. See the journal, 2026-08-07.
 *
 * Both arcs are round-capped, so the ring's ends and the fill's leading edge are
 * semicircles. A cap reaches thickness/2 past its arc end in every direction, which
 * the radius below leaves room for: the ends sit on the horizontal diameter, so the
 * caps grow into the pixels between the arc and the widget's bottom corners. */
static void gauge_paint(leWidget *wgt)
{
    s_orig_paint(wgt);

    if (wgt->status.drawState != LE_WIDGET_DRAW_STATE_DONE ||
        s_track == NULL || s_fill == NULL)
    {
        return;
    }

    leRect rect;
    wgt->fn->rectToScreen(wgt, &rect);
    if (rect.width < 8 || rect.height < 6) { return; }

    /* Half-pixel units. The ring's ends sit on the horizontal diameter, and the round caps
     * reach hw past them, which the radius leaves room for. */
    int32_t hw = (int32_t)s_thickness;                 /* half of thickness px, doubled */
    int32_t r  = rect.width - hw - 2;

    if (r <= hw) { return; }

    int32_t cx = (2 * rect.x) + rect.width;
    int32_t cy = (2 * rect.y) + r + hw + 2;

    leColor track = leScheme_GetRenderColor(s_track, LE_SCHM_BASE);
    leColor fill  = leScheme_GetRenderColor(s_fill,  LE_SCHM_BASE);

    /* The fill grows from the left end (180 degrees) back toward 0, so the wedge is the top
     * `span` degrees of the ring's range. */
    int32_t span = (int32_t)((s_permille * 180u) / 1000u);

    AaShape_ArcRing(cx, cy, r, hw, 0, 180, 180 - span, (span > 0) ? 180 : 0,
                    track, fill, 255u);

    /* Round caps: the ring's two ends, then the fill's leading edge. */
    AaShape_Disc(cx + r, cy, hw, track, 255u);
    AaShape_Disc(cx - r, cy, hw, (span > 0) ? fill : track, 255u);

    if (span > 0 && span < 180)
    {
        int32_t ex = cx + ((r * AaShape_CosQ12(180 - span)) >> 12);
        int32_t ey = cy - ((r * AaShape_SinQ12(180 - span)) >> 12);

        AaShape_Disc(ex, ey, hw, fill, 255u);
    }
}

void Gauge_Enable(leWidget *panel, uint32_t thickness, const leScheme *track)
{
    if (panel == NULL) { return; }

    s_thickness = (thickness < 3u) ? 3u : thickness;
    s_track     = track;

    if (!s_vt_ready)
    {
        s_vt = *panel->fn;
        s_orig_paint = panel->fn->_paint;
        s_vt._paint = gauge_paint;
        s_vt_ready = LE_TRUE;
    }
    panel->fn = &s_vt;
}

void Gauge_Set(uint32_t permille, const leScheme *fill)
{
    s_permille = (permille > 1000u) ? 1000u : permille;
    if (fill != NULL) { s_fill = fill; }
}
