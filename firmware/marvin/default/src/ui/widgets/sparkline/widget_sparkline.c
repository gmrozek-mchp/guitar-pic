#include "ui/widgets/sparkline/widget_sparkline.h"

#include "ui/gfx/vec_draw.h"
#include "gfx/legato/core/legato_scheme.h"

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

/* Integer coordinates are pixel *edges* to the vector rasterizer, and a stroke is
 * centred on its path, so an even LINE_THICK lands crisply on integer coordinates.
 * The plot area is inset by a full LINE_THICK on all four sides so neither the
 * stroke nor its round caps can reach outside the widget rect — the bus screen
 * invalidates the whole panel, so the clip rect during paint is wider than us. */
#define PLOT_INSET  LINE_THICK

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
    int32_t  span  = rect->height - 2 * PLOT_INSET;

    if (v > scale) { v = scale; }
    if (span < 1)  { span = 1; }

    return rect->y + PLOT_INSET + span
         - (int32_t)(((uint64_t)v * (uint32_t)span) / scale);
}

/* Vertex for oldest-first sample i of n, evenly spaced across the inset width. */
static void plot_point(const leRect *rect, uint32_t i, uint32_t n, leVector2 *out)
{
    int32_t w = rect->width - 2 * PLOT_INSET;
    int32_t x = rect->x + PLOT_INSET
              + (int32_t)(((uint64_t)i * (uint32_t)w) / (n - 1u));

    out->x = LE_REAL_I16_FROM_INT(x);
    out->y = LE_REAL_I16_FROM_INT(value_to_y(rect, sample_at(i)));
}

/* Draw the polyline after the (empty, transparent) widget paints: one antialiased
 * vector segment per sample pair, round-capped so the caps overlap at each shared
 * vertex and form a join instead of a notch. */
static void sparkline_paint(leWidget *wgt)
{
    s_orig_paint(wgt);

    if (wgt->status.drawState != LE_WIDGET_DRAW_STATE_DONE || s_count < 2u)
    {
        return;
    }

    leRect rect;
    wgt->fn->rectToScreen(wgt, &rect);
    if (rect.width < 4 * PLOT_INSET || rect.height < 4 * PLOT_INSET) { return; }

    /* hardness must stay LE_REAL_I16_ONE — see ui/gfx/vec_draw.h. */
    leVectorLineAttr attr =
    {
        .color    = leScheme_GetRenderColor(wgt->scheme, LE_SCHM_BASE),
        .alpha    = 255u,
        .width    = LE_REAL_I16_FROM_INT(LINE_THICK),
        .hardness = LE_REAL_I16_ONE,
        .aaMode   = UI_VEC_AA,
        .capStyle = LE_CAPSTYLE_ROUND,
    };

    uint32_t  n = s_count;
    leVector2 a, b;

    plot_point(&rect, 0u, n, &a);
    for (uint32_t i = 1u; i < n; i++)
    {
        plot_point(&rect, i, n, &b);
        leDraw_VectorLine(&a, &b, &attr);
        a = b;
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
