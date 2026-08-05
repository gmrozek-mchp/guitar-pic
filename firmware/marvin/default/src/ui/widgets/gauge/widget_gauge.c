#include "ui/widgets/gauge/widget_gauge.h"

#include <math.h>

#include "gfx/legato/common/legato_color.h"
#include "gfx/legato/core/legato_scheme.h"
#include "gfx/legato/renderer/legato_renderer.h"

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

#define PI_F  3.14159265f

/* Paint the arc after the (empty, transparent) widget paints. Each pixel's
 * coverage of the ring is 1 - |d - r| / (t/2), so the inner and outer edges are
 * anti-aliased against whatever is already in the framebuffer; the sweep angle
 * decides whether it takes the fill or the track colour. Bounded by the widget
 * rect — no stock rounded/arc paint involved. */
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

    leColorMode mode = leRenderer_CurrentColorMode();
    leColor track = leScheme_GetRenderColor(s_track, LE_SCHM_BASE);
    leColor fill  = leScheme_GetRenderColor(s_fill,  LE_SCHM_BASE);

    float t    = (float)s_thickness;
    float cx   = (float)rect.x + (float)rect.width / 2.0f;
    float r    = (float)rect.width / 2.0f - t / 2.0f - 1.0f;
    float cy   = (float)rect.y + r + t / 2.0f + 1.0f;
    float pct  = (float)s_permille / 1000.0f;
    int   ymax = (int)(r + t / 2.0f + 2.0f);

    if (ymax > rect.height) { ymax = rect.height; }

    for (int py = 0; py < ymax; py++)
    {
        for (int px = 0; px < rect.width; px++)
        {
            float fx = (float)(rect.x + px) + 0.5f;
            float fy = (float)(rect.y + py) + 0.5f;
            float dx = fx - cx;
            float dy = cy - fy;          /* positive above the centre */

            if (dy < -0.5f) { continue; }   /* below the flat side */

            float d   = sqrtf(dx * dx + dy * dy);
            float cov = 1.0f - fabsf(d - r) / (t / 2.0f);
            if (cov <= 0.0f) { continue; }
            if (cov > 1.0f)  { cov = 1.0f; }

            /* atan2 gives π at the left end, 0 at the right → sweep fraction. */
            float frac = 1.0f - (atan2f((dy < 0.0f) ? 0.0f : dy, dx) / PI_F);
            leColor c  = (frac <= pct) ? fill : track;

            int32_t x = rect.x + px;
            int32_t y = rect.y + py;
            leColor bg = leRenderer_GetPixel(x, y);
            leRenderer_PutPixel(x, y,
                                leColorLerp(bg, c, (uint32_t)(cov * 100.0f + 0.5f), mode));
        }
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
