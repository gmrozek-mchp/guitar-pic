#include "ui/widgets/sparkline/widget_sparkline.h"

#include "gfx/legato/core/legato_scheme.h"
#include "gfx/legato/renderer/legato_renderer.h"

/* Shared vtable copy + captured original paint (the PanelAA pattern). */
static leWidgetVTable s_vt;
static void (*s_orig_paint)(leWidget *);
static leBool s_vt_ready = LE_FALSE;

/* Single-instance ring buffer (one sparkline on the bus screen). s_count stops the
 * plot drawing a flat line across slots that have never been filled. */
static uint32_t s_sample[SPARKLINE_SAMPLES];
static uint32_t s_head;                       /* next write slot */
static uint32_t s_count;
static uint32_t s_scale = 600u;               /* permille at the top edge */

#define LINE_THICK  2

/* Oldest-first sample i (0 .. s_count-1). */
static uint32_t sample_at(uint32_t i)
{
    uint32_t first = (s_count < SPARKLINE_SAMPLES)
                   ? 0u : (s_head % SPARKLINE_SAMPLES);
    return s_sample[(first + i) % SPARKLINE_SAMPLES];
}

static int32_t value_to_y(const leRect *rect, uint32_t v)
{
    uint32_t scale = (s_scale == 0u) ? 1u : s_scale;
    int32_t  span  = rect->height - LINE_THICK;

    if (v > scale) { v = scale; }
    if (span < 1)  { span = 1; }

    return rect->y + span - (int32_t)(((uint64_t)v * (uint32_t)span) / scale);
}

/* Draw the polyline after the (empty, transparent) widget paints: walk the pixel
 * columns of each segment, interpolate the sample values, and emit a vertical run
 * spanning this column's y to the next column's y so the line stays connected on
 * steep slopes. Bounded by the widget width. */
static void sparkline_paint(leWidget *wgt)
{
    s_orig_paint(wgt);

    if (wgt->status.drawState != LE_WIDGET_DRAW_STATE_DONE || s_count < 2u)
    {
        return;
    }

    leRect rect;
    wgt->fn->rectToScreen(wgt, &rect);
    if (rect.width < 4 || rect.height < 4) { return; }

    leColor line = leScheme_GetRenderColor(wgt->scheme, LE_SCHM_BASE);
    uint32_t n   = s_count;
    int32_t  w   = rect.width - 1;

    for (uint32_t i = 0; i + 1u < n; i++)
    {
        int32_t x0 = (int32_t)(((uint64_t)i * (uint32_t)w) / (n - 1u));
        int32_t x1 = (int32_t)(((uint64_t)(i + 1u) * (uint32_t)w) / (n - 1u));
        int32_t y0 = value_to_y(&rect, sample_at(i));
        int32_t y1 = value_to_y(&rect, sample_at(i + 1u));
        int32_t dx = (x1 > x0) ? (x1 - x0) : 1;

        for (int32_t x = x0; x <= x1; x++)
        {
            int32_t xn = (x + 1 > x1) ? x1 : (x + 1);   /* next column, for the run */
            int32_t ya = y0 + ((y1 - y0) * (x  - x0)) / dx;
            int32_t yb = y0 + ((y1 - y0) * (xn - x0)) / dx;
            int32_t top = (ya < yb) ? ya : yb;
            int32_t bot = (ya > yb) ? ya : yb;

            leRenderer_VertLine(rect.x + x, top, (bot - top) + LINE_THICK, line, 255u);
        }
    }
}

void Sparkline_Enable(leWidget *panel)
{
    if (panel == NULL) { return; }

    if (!s_vt_ready)
    {
        s_vt = *panel->fn;
        s_orig_paint = panel->fn->_paint;
        s_vt._paint = sparkline_paint;
        s_vt_ready = LE_TRUE;
    }
    panel->fn = &s_vt;
}

void Sparkline_Push(uint32_t permille)
{
    s_sample[s_head % SPARKLINE_SAMPLES] = permille;
    s_head = (s_head + 1u) % SPARKLINE_SAMPLES;
    if (s_count < SPARKLINE_SAMPLES) { s_count++; }
}

void Sparkline_SetScale(uint32_t permille_max)
{
    if (permille_max > 0u) { s_scale = permille_max; }
}
