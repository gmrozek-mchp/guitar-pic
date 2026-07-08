#include "ui/widgets/progressbar_aa/widget_progressbar_aa.h"

#include "ui/gfx/aa_corners.h"

#include "gfx/legato/core/legato_scheme.h"
#include "gfx/legato/renderer/legato_renderer.h"
#include "gfx/legato/widget/legato_widget.h"

/* One shared vtable copy + the captured original paint — all rounded progress bars
 * share the same overridden vtable. Radius + backdrop scheme are fixed per build (the
 * bar and the card it sits on); the BASE colour is resolved at paint time in the active
 * mode. Radius lives here, not in the widget style, so the stock skin keeps drawing a
 * plain square track (a nonzero widget cornerRadius makes it draw its own mis-sized
 * rounded track that overshoots the rect). */
#define PERMILLE_MAX  1000u

static leProgressBarWidgetVTable s_aa_vt;
static void     (*s_orig_paint)(leProgressBarWidget*);
static leBool          s_ready = LE_FALSE;
static uint32_t        s_radius;
static uint32_t        s_fillw;      /* current fill width in px (from permille) */
static const leScheme *s_bg_scheme;

/* Let the stock skin paint the (square) track, then draw the fill ourselves at the
 * exact `s_fillw` pixel width (over the stock track — the widget value stays 0 so the
 * stock fill is skipped), and finally eat the four corners back to the card colour so
 * the bar reads as a rounded capsule. Typed on leProgressBarWidget to match the
 * widget's vtable _paint slot (its THIS_TYPE). */
static void aa_paint(leProgressBarWidget* bar)
{
    s_orig_paint(bar);

    if (bar->widget.status.drawState != LE_WIDGET_DRAW_STATE_DONE) { return; }

    leRect rect;
    bar->fn->rectToScreen(bar, &rect);

    if (s_fillw > 0u)
    {
        leRect fill = { rect.x, rect.y, (int32_t)s_fillw, rect.height };
        leRenderer_RectFill(&fill,
                            leScheme_GetRenderColor(bar->widget.scheme, LE_SCHM_BACKGROUND),
                            255);
    }

    if (s_radius > 0u)
    {
        AaCorners_RenderRoundImage(&rect, s_radius,
                                   leScheme_GetRenderColor(s_bg_scheme, LE_SCHM_BASE),
                                   leRenderer_CurrentColorMode());
    }
}

void ProgressBarAA_SetPermille(leProgressBarWidget* bar, uint32_t permille)
{
    if (permille > PERMILLE_MAX) { permille = PERMILLE_MAX; }

    /* Repaint only when the fill's *pixel* width actually changes — permille has
     * more steps than the bar has pixels, so most updates map to the same width and
     * would otherwise trigger an identical full-widget redraw. */
    uint32_t fw = ((uint32_t)bar->fn->getWidth(bar) * permille) / PERMILLE_MAX;
    if (fw == s_fillw) { return; }

    s_fillw = fw;
    bar->fn->invalidate(bar);   /* full-widget repaint: track redrawn, fill + corners fresh */
}

void ProgressBarAA_EnableRoundImage(leProgressBarWidget* bar, uint32_t radius,
                                    const leScheme* bg)
{
    if (!s_ready)
    {
        s_aa_vt = *bar->fn;                      /* full progressbar vtable (setValue …) */
        s_orig_paint   = bar->fn->_paint;
        s_aa_vt._paint = aa_paint;
        s_ready = LE_TRUE;
    }

    s_radius    = radius;
    s_bg_scheme = bg;

    /* A progress bar carries two vtable pointers to the same table: its own typed
     * `fn` and the base `widget.fn`. The renderer dispatches paint through the base
     * one (it handles a leWidget*), so both must point at the override — repointing
     * only `bar->fn` leaves the original paint running. */
    bar->fn        = &s_aa_vt;
    bar->widget.fn = (const leWidgetVTable*)&s_aa_vt;
}
