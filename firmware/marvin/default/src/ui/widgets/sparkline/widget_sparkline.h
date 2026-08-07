#ifndef UI_WIDGET_SPARKLINE_H
#define UI_WIDGET_SPARKLINE_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/legato/widget/legato_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Time-series sparkline: samples drawn as a connected antialiased polyline, oldest at
 * the left edge, newest at the right, optionally with a filled disc on the newest sample.
 *
 * The history is a SparklineSeries, separate from the widget that draws it, because the
 * titlebar plots one series in five places at once (one titlebar per base-view screen)
 * and a per-widget ring buffer would leave the four hidden copies stale. Push once per
 * refresh tick; every widget bound to that series scrolls with it.
 *
 * A widget is a SparklineWidget, not a plain leWidget: the shared paint override reaches
 * its style through the leWidget it embeds as its first member (the way Legato's own
 * widgets derive), so the style needs no side table and the instance count is whatever
 * the caller allocates. */
#define SPARKLINE_SAMPLES  40u

typedef struct
{
    uint32_t sample[SPARKLINE_SAMPLES];
    uint32_t head;                    /* next write slot                        */
    uint32_t count;                   /* filled slots; < 2 draws nothing        */
} SparklineSeries;

typedef struct
{
    leWidget               widget;    /* MUST be first — the paint casts to this */
    const SparklineSeries *series;
    uint32_t               scale;     /* permille at the top edge (fixed mode)   */
    uint32_t               min_span;  /* narrowest autoscale window, permille    */
    uint32_t               window;    /* newest N samples plotted (0 = all)      */
    uint32_t               thickness; /* stroke width, px (even lands crisply)   */
    bool                   autoscale;
    bool                   marker;    /* filled disc on the newest sample        */
} SparklineWidget;

void Sparkline_SeriesInit(SparklineSeries *s);

/* Append a sample (permille of full scale) and drop the oldest. */
void Sparkline_Push(SparklineSeries *s, uint32_t permille);

/* Build a sparkline over `series`, plotting 0..`permille_max` against the widget's
 * height. The line colour is the widget scheme's BASE, so give it a scheme whose base is
 * the line colour. Defaults: 2px stroke, no marker. */
void Sparkline_Constructor(SparklineWidget *sp, const SparklineSeries *series,
                           uint32_t permille_max);

/* Fit the plot to the window's own min/max instead of a fixed 0..scale, widened to at
 * least `min_span_permille` so a near-flat signal isn't amplified into a mountain range.
 * For a plot with no axis, where the value beside it carries the absolute number. */
void Sparkline_SetAutoscale(SparklineWidget *sp, uint32_t min_span_permille);

/* Plot only the newest `samples` of the series (0 = all of it). For a plot narrower than
 * the series is long: the sample pitch, not the sample count, is what decides whether the
 * result reads as a line or as a comb — the mockup's tiles run ~3 px per sample. */
void Sparkline_SetWindow(SparklineWidget *sp, uint32_t samples);

void Sparkline_SetStyle(SparklineWidget *sp, uint32_t thickness, bool marker);

#ifdef __cplusplus
}
#endif

#endif /* UI_WIDGET_SPARKLINE_H */
