#include "ui/widgets/sparkline/widget_sparkline.h"

#include <stdbool.h>

#include "ui/gfx/aa_shape.h"
#include "gfx/legato/core/legato_scheme.h"
#include "gfx/legato/renderer/legato_renderer.h"

/* Shared vtable copy + captured original paint (the PanelAA pattern). Every widget that
 * carries it was built by Sparkline_Constructor, so the paint may cast to
 * SparklineWidget. */
static leWidgetVTable s_vt;
static void (*s_orig_paint)(leWidget *);
static leBool s_vt_ready = LE_FALSE;

/* Integer coordinates are pixel *edges* to the vector rasterizer, and a stroke is
 * centred on its path, so an even thickness lands crisply on integer coordinates.
 * The plot area is inset by a full thickness on all four sides (one more with a marker,
 * whose disc is wider than the stroke) so neither the stroke nor its round caps can
 * reach outside the widget rect — a screen may invalidate a whole panel, and then the
 * clip rect during paint is wider than we are. */
static int32_t plot_inset(const SparklineWidget *sp)
{
    return (int32_t)sp->thickness + (sp->marker ? 1 : 0);
}

/* Oldest-first sample i of the series (0 .. count-1). */
static uint32_t series_at(const SparklineSeries *s, uint32_t i)
{
    uint32_t first = (s->count < SPARKLINE_SAMPLES)
                   ? 0u : (s->head % SPARKLINE_SAMPLES);
    return s->sample[(first + i) % SPARKLINE_SAMPLES];
}

/* How many samples this widget plots: the whole series, or the newest `window` of it. */
static uint32_t plot_count(const SparklineWidget *sp)
{
    uint32_t n = sp->series->count;

    return (sp->window != 0u && sp->window < n) ? sp->window : n;
}

/* Oldest-first sample i of the plotted window (0 .. plot_count-1). */
static uint32_t sample_at(const SparklineWidget *sp, uint32_t i)
{
    return series_at(sp->series, sp->series->count - plot_count(sp) + i);
}

/* The value window the plot's height represents: 0..scale fixed, or the plotted samples'
 * own min..max widened to min_span when autoscaling. */
static void value_window(const SparklineWidget *sp, uint32_t *lo, uint32_t *span)
{
    if (!sp->autoscale)
    {
        *lo   = 0u;
        *span = (sp->scale == 0u) ? 1u : sp->scale;
        return;
    }

    uint32_t n   = plot_count(sp);
    uint32_t min = sample_at(sp, 0u);
    uint32_t max = min;

    for (uint32_t i = 1u; i < n; i++)
    {
        uint32_t v = sample_at(sp, i);
        if (v < min) { min = v; }
        if (v > max) { max = v; }
    }

    uint32_t s = max - min;
    if (s < sp->min_span) { s = sp->min_span; }
    if (s == 0u)          { s = 1u; }

    /* Centre the samples in the widened window so a flat line sits mid-plot rather than
     * pinned to an edge. */
    uint32_t pad = (s - (max - min)) / 2u;
    *lo   = (min > pad) ? (min - pad) : 0u;
    *span = s;
}

static int32_t value_to_y(const leRect *rect, int32_t inset,
                          uint32_t lo, uint32_t span, uint32_t v)
{
    int32_t h = rect->height - 2 * inset;

    if (h < 1)  { h = 1; }
    if (v < lo) { v = lo; }
    v -= lo;
    if (v > span) { v = span; }

    return rect->y + inset + h - (int32_t)(((uint64_t)v * (uint32_t)h) / span);
}

/* Pixel x of oldest-first sample i of n, evenly spaced across the inset width. */
static int32_t sample_to_x(const leRect *rect, int32_t inset, uint32_t i, uint32_t n)
{
    int32_t w = rect->width - 2 * inset;

    return rect->x + inset + (int32_t)(((uint64_t)i * (uint32_t)w) / (n - 1u));
}

/* Antialiased polyline by analytic coverage: one pass over the plot rect, each pixel shaded
 * from its distance to the *nearest* segment.
 *
 * Taking the minimum over segments rather than drawing them one at a time is what makes the
 * joins right — the union of round-capped strokes is exactly the set of points within the
 * half-width of the polyline — and it means every pixel is blended once, where per-segment
 * drawing double-blends the rim wherever two segments' boxes overlap.
 *
 * Vertices are x-monotonic and evenly spaced, so the candidate segments for a pixel column
 * are a short contiguous run; two moving indices find it in amortized constant time.
 *
 * ALL OF IT IS INTEGER, and that is the point. This replaced 23 `leDraw_VectorLine` calls
 * measured at 7894 µs for an 80×28 plot — but a first version of this same analytic maths
 * written in `float` measured 7074 µs, barely an improvement, because the core has no FPU and
 * every float operation is a libgcc call at ~80 cycles. Supersampling was never the whole
 * problem; the per-operation cost was. So: half-pixel units keep pixel centres on integers,
 * and comparing squared distances *pre-multiplied by* |d|² removes the projection divide.
 * There is no float here at all — the rim's root is AaShape_Isqrt on a 64ths ratio.
 *
 * Ranges, in half-pixel plot-local units: |w| ≤ 160, |d| ≤ 90, so |w|²·len2 ≤ 6.7e7 and
 * cross² ≤ 8.3e8, both inside int32. Only the rim's min-across-segments comparison needs
 * int64, and only for those ~120 pixels. */
typedef struct
{
    int32_t x0, y0;    /* start point, half-pixel plot-local units */
    int32_t dx, dy;    /* segment vector                           */
    int32_t len2;      /* |d|²                                     */
    int32_t in_thr;    /* T_in²  · len2 — at or under this is fully covered */
    int32_t out_thr;   /* T_out² · len2 — at or over this contributes nothing */
} spark_seg_t;

static void paint_polyline(const leRect *rect, const SparklineWidget *sp,
                           uint32_t lo, uint32_t span, leColor color)
{
    /* Static, not on the stack: 40 entries is 800 bytes against LEGATO_Tasks' 4 KB. Safe
     * because Legato paints one widget at a time on one task, and the only other caller of
     * the painter (Titlebar_ProbeFrameUs) holds the render lock, which guarantees
     * LEGATO_Tasks is suspended — so the two can never overlap. */
    static spark_seg_t seg[SPARKLINE_SAMPLES];

    int32_t  inset = plot_inset(sp);
    uint32_t n     = plot_count(sp);
    uint32_t nseg  = n - 1u;
    int32_t  ox    = rect->x + inset;         /* plot-local origin */
    int32_t  oy    = rect->y + inset;

    /* Half-pixel thresholds: the stroke is `thickness` wide, so a pixel centre is fully
     * covered within (thickness-1) half-pixels of the spine and untouched beyond
     * (thickness+1). */
    int32_t  t_in  = (int32_t)sp->thickness - 1;
    int32_t  t_out = (int32_t)sp->thickness + 1;
    uint32_t i;
    int32_t  px, py;

    for (i = 0u; i < nseg; i++)
    {
        int32_t ax = 2 * (sample_to_x(rect, inset, i, n) - ox);
        int32_t ay = 2 * (value_to_y(rect, inset, lo, span, sample_at(sp, i)) - oy);
        int32_t bx = 2 * (sample_to_x(rect, inset, i + 1u, n) - ox);
        int32_t by = 2 * (value_to_y(rect, inset, lo, span, sample_at(sp, i + 1u)) - oy);

        seg[i].x0   = ax;
        seg[i].y0   = ay;
        seg[i].dx   = bx - ax;
        seg[i].dy   = by - ay;
        seg[i].len2 = (seg[i].dx * seg[i].dx) + (seg[i].dy * seg[i].dy);
        if (seg[i].len2 < 1) { seg[i].len2 = 1; }

        seg[i].in_thr  = (t_in  > 0) ? (t_in  * t_in  * seg[i].len2) : -1;
        seg[i].out_thr = t_out * t_out * seg[i].len2;
    }

    uint32_t lo_i = 0u;   /* first segment that can still reach this column */

    for (px = ox; px < rect->x + rect->width - inset; px++)
    {
        int32_t pxh = 2 * (px - ox) + 1;      /* pixel centre, half-pixel units */

        /* Advance the window: drop segments whose right end is behind us, admit those whose
         * left end is within reach. Both indices only move forward across the row. */
        while (lo_i + 1u < nseg &&
               (seg[lo_i].x0 + seg[lo_i].dx) < (pxh - t_out)) { lo_i++; }

        uint32_t hi_i = lo_i;
        while (hi_i + 1u < nseg && seg[hi_i + 1u].x0 <= (pxh + t_out)) { hi_i++; }

        /* Only scan the rows the candidate segments can actually reach. A pixel further than
         * t_out from every candidate's y-extent is more than t_out away in y alone, so its
         * distance exceeds the stroke and it could only ever be rejected — and the line
         * occupies ~350 of the plot's 1628 pixels, so this is most of them. */
        int32_t ylo = seg[lo_i].y0, yhi = ylo;

        for (i = lo_i; i <= hi_i; i++)
        {
            int32_t ya = seg[i].y0;
            int32_t yb = seg[i].y0 + seg[i].dy;

            if (ya < ylo) { ylo = ya; }
            if (ya > yhi) { yhi = ya; }
            if (yb < ylo) { ylo = yb; }
            if (yb > yhi) { yhi = yb; }
        }

        /* Half-pixel row bounds → pixel rows, one row of slack each way for the halving. */
        int32_t py_lo = oy + ((ylo - t_out) / 2) - 1;
        int32_t py_hi = oy + ((yhi + t_out) / 2) + 1;

        if (py_lo < oy) { py_lo = oy; }
        if (py_hi > rect->y + rect->height - inset) { py_hi = rect->y + rect->height - inset; }

        for (py = py_lo; py < py_hi; py++)
        {
            int32_t pyh   = 2 * (py - oy) + 1;
            int32_t b_num = 0, b_den = 0;     /* best d², as num/den */
            bool    full  = false;
            uint32_t alpha;

            for (i = lo_i; i <= hi_i; i++)
            {
                int32_t wx   = pxh - seg[i].x0;
                int32_t wy   = pyh - seg[i].y0;
                int32_t proj = (wx * seg[i].dx) + (wy * seg[i].dy);
                int32_t d2s;

                /* Squared distance to the segment, pre-multiplied by len2 — which is what
                 * lets the interior/exterior tests and the cross-segment minimum run
                 * without ever dividing. */
                if (proj <= 0)
                {
                    d2s = ((wx * wx) + (wy * wy)) * seg[i].len2;
                }
                else if (proj >= seg[i].len2)
                {
                    wx -= seg[i].dx;
                    wy -= seg[i].dy;
                    d2s = ((wx * wx) + (wy * wy)) * seg[i].len2;
                }
                else
                {
                    int32_t cross = (wx * seg[i].dy) - (wy * seg[i].dx);
                    d2s = cross * cross;
                }

                if (d2s <= seg[i].in_thr) { full = true; break; }
                if (d2s >= seg[i].out_thr) { continue; }

                /* Rim: keep the smallest d² across candidates, compared as fractions so no
                 * division is needed. Only these pixels reach int64 or a square root. */
                if (b_den == 0 ||
                    ((int64_t)d2s * b_den) < ((int64_t)b_num * seg[i].len2))
                {
                    b_num = d2s;
                    b_den = seg[i].len2;
                }
            }

            if (full)
            {
                (void)leRenderer_PutPixel_Safe(px, py, color);
                continue;
            }
            if (b_den == 0) { continue; }

            /* cov = ((thickness+1) - d) / 2 with d in half-pixels, integer throughout: the
             * ratio is taken to 64ths before the root, so one 64-bit divide and one isqrt
             * replace two int-to-float conversions, a float divide, a sqrtf and a multiply.
             * This was the last per-pixel float left on a repaint path. */
            {
                uint32_t d64  = AaShape_Isqrt((uint32_t)((((uint64_t)b_num) << 12) /
                                                         (uint32_t)b_den));
                int32_t  cov64 = (((int32_t)t_out * 64) - (int32_t)d64) / 2;

                if (cov64 <= 0) { continue; }

                if (cov64 >= 64)
                {
                    (void)leRenderer_PutPixel_Safe(px, py, color);
                    continue;
                }

                alpha = ((uint32_t)cov64 * 255u) / 64u;
                if (alpha != 0u)
                {
                    (void)leRenderer_BlendPixel_Safe(px, py, color, alpha);
                }
            }
        }
    }
}

/* Draw the plot after the (empty, transparent) widget paints. */
static void sparkline_paint(leWidget *wgt)
{
    SparklineWidget *sp = (SparklineWidget *)wgt;

    s_orig_paint(wgt);

    if (wgt->status.drawState != LE_WIDGET_DRAW_STATE_DONE ||
        sp->series == NULL || plot_count(sp) < 2u)
    {
        return;
    }

    leRect rect;
    wgt->fn->rectToScreen(wgt, &rect);

    int32_t inset = plot_inset(sp);
    if (rect.width < 4 * inset || rect.height < 4 * inset) { return; }

    leColor  color = leScheme_GetRenderColor(wgt->scheme, LE_SCHM_BASE);
    uint32_t lo, span;

    value_window(sp, &lo, &span);

    paint_polyline(&rect, sp, lo, span, color);

    if (sp->marker)
    {
        uint32_t n = plot_count(sp);
        int32_t  mx = sample_to_x(&rect, inset, n - 1u, n);
        int32_t  my = value_to_y(&rect, inset, lo, span, sample_at(sp, n - 1u));

        /* Diameter 2×(thickness+1) — 6 px against a 2 px stroke, matching the mockup's
         * 3.33× ratio (r=2.5 dot on a 1.5 px line) closely enough, and even so that the
         * disc centres exactly on the vertex. An odd diameter would land half a pixel off,
         * because vertices sit on integer coordinates (= pixel edges).
         *
         * AaShape_Capsule, not leDraw_VectorArcFill: the vector arc fill costs ~32,000
         * cycles per pixel on this core (see ui/gfx/aa_shape.h). */
        int32_t r = (int32_t)sp->thickness + 1;
        leRect  disc = { .x = mx - r, .y = my - r, .width = 2 * r, .height = 2 * r };

        AaShape_Capsule(&disc, color, 255u);
    }
}

void Sparkline_SeriesInit(SparklineSeries *s)
{
    if (s == NULL) { return; }

    s->head  = 0u;
    s->count = 0u;
}

void Sparkline_Push(SparklineSeries *s, uint32_t permille)
{
    if (s == NULL) { return; }

    s->sample[s->head % SPARKLINE_SAMPLES] = permille;
    s->head = (s->head + 1u) % SPARKLINE_SAMPLES;
    if (s->count < SPARKLINE_SAMPLES) { s->count++; }
}

void Sparkline_Constructor(SparklineWidget *sp, const SparklineSeries *series,
                           uint32_t permille_max)
{
    if (sp == NULL) { return; }

    leWidget_Constructor(&sp->widget);

    if (!s_vt_ready)
    {
        s_vt = *sp->widget.fn;
        s_orig_paint = sp->widget.fn->_paint;
        s_vt._paint = sparkline_paint;
        s_vt_ready = LE_TRUE;
    }
    sp->widget.fn = &s_vt;

    sp->series    = series;
    sp->scale     = (permille_max > 0u) ? permille_max : 1u;
    sp->min_span  = 0u;
    sp->window    = 0u;
    sp->thickness = 2u;
    sp->autoscale = false;
    sp->marker    = false;
}

void Sparkline_SetAutoscale(SparklineWidget *sp, uint32_t min_span_permille)
{
    if (sp == NULL) { return; }

    sp->autoscale = true;
    sp->min_span  = min_span_permille;
}

void Sparkline_SetWindow(SparklineWidget *sp, uint32_t samples)
{
    if (sp == NULL) { return; }

    sp->window = (samples > SPARKLINE_SAMPLES) ? SPARKLINE_SAMPLES : samples;
}

void Sparkline_SetStyle(SparklineWidget *sp, uint32_t thickness, bool marker)
{
    if (sp == NULL) { return; }

    sp->thickness = (thickness > 0u) ? thickness : 1u;
    sp->marker    = marker;
}
