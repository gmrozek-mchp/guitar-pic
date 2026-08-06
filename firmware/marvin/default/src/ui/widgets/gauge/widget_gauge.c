#include "ui/widgets/gauge/widget_gauge.h"

#include "ui/gfx/vec_draw.h"

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
 * then the fill over its left end. The vector rasterizer anti-aliases both radial
 * edges and the fill's leading edge against whatever is already in the framebuffer,
 * and clips to the widget's damage rect — no stock rounded/arc paint involved. */
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

    leReal_i16 t = LE_REAL_I16_FROM_INT((int32_t)s_thickness);
    leReal_i16 r = LE_REAL_I16_FROM_INT(rect.width) / 2 - t / 2 - LE_REAL_I16_ONE;

    if (r <= t / 2) { return; }

    leVector2 centre = { .x = LE_REAL_I16_FROM_INT(rect.x) +
                              LE_REAL_I16_FROM_INT(rect.width) / 2,
                         .y = LE_REAL_I16_FROM_INT(rect.y) + r + t / 2 + LE_REAL_I16_ONE };

    /* The fill grows from the left end (180°) clockwise, so it ends where the track's
     * remaining span begins. */
    int32_t span = (int32_t)((s_permille * (uint32_t)UI_VEC_DEG16(180)) / 1000u);

    leVectorArc_StrokeAttr arc =
    {
        .color    = leScheme_GetRenderColor(s_track, LE_SCHM_BASE),
        .alpha    = 255u,
        .width    = t,
        .hardness = LE_REAL_I16_ONE,
        .mask     = LE_STROKEMASK_ALL,
        .aaMode   = UI_VEC_AA,
        .capStyle = LE_CAPSTYLE_SQUARE,
    };

    leDraw_VectorArcStroke(&centre, r, 0, UI_VEC_DEG16(180), &arc);

    if (span > 0)
    {
        arc.color = leScheme_GetRenderColor(s_fill, LE_SCHM_BASE);
        leDraw_VectorArcStroke(&centre, r, UI_VEC_DEG16(180) - span, span, &arc);
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
