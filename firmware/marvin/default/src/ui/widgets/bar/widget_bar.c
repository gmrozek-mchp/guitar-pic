#include "ui/widgets/bar/widget_bar.h"

#include "FreeRTOS.h"        /* configASSERT */

#include "ui/gfx/aa_corners.h"
#include "ui/gfx/gradient.h"

#include "gfx/legato/common/legato_color.h"
#include "gfx/legato/core/legato_scheme.h"
#include "gfx/legato/renderer/legato_renderer.h"

/* One shared vtable copy + the captured original paint — every bar shares the override
 * — but geometry and colours are PER BAR: the dashboard runs three (song playtime, two
 * streak bars) plus the test-pattern ramp, so module-wide state would paint them all
 * to whichever was set last. */
#define PERMILLE_MAX  1000u
#define BAR_MAX          6u

typedef struct
{
    leWidget       *bar;
    uint32_t        radius;
    uint32_t        fillw;      /* current fill width in px (from permille) */
    const leScheme *bg;         /* solid backdrop behind the bar            */
    uint32_t        from, to;   /* 0xRRGGBB gradient ends; equal ⇒ flat     */
} bar_state_t;

static leWidgetVTable s_vt;
static void         (*s_orig_paint)(leWidget*);
static leBool         s_ready = LE_FALSE;
static bar_state_t    s_state[BAR_MAX];
static unsigned       s_n;

static bar_state_t *bar_state(const leWidget *bar)
{
    for (unsigned i = 0; i < s_n; i++)
    {
        if (s_state[i].bar == bar) { return &s_state[i]; }
    }
    return NULL;
}

/* Let the classic skin paint the track, draw the fill at the exact pixel width, then eat
 * the four corners back to the backdrop so the bar reads as a rounded capsule (the
 * fill's leading cap rounds with it — interior pixels are preserved, only the
 * outside-arc wedges fade to bg). */
static void bar_paint(leWidget* wgt)
{
    s_orig_paint(wgt);

    if (wgt->status.drawState != LE_WIDGET_DRAW_STATE_DONE) { return; }

    bar_state_t *st = bar_state(wgt);
    if (st == NULL) { return; }

    leRect rect;
    wgt->fn->rectToScreen(wgt, &rect);

    if (st->fillw > 0u)
    {
        leColorMode mode = leRenderer_CurrentColorMode();
        leRect      fill = { rect.x, rect.y, (int32_t)st->fillw, rect.height };

        if (st->from == st->to)
        {
            leRenderer_RectFill(&fill, leColorConvert(LE_COLOR_MODE_RGB_888, mode, st->from), 255);
        }
        else
        {
            /* The ramp spans the bar's full width, so a pixel's colour doesn't shift as
             * the value grows; dithered, because a 16-bit ramp otherwise bands. */
            Gradient_FillH(&fill, rect.x, (uint32_t)rect.width, st->from, st->to, mode);
        }
    }

    if (st->radius > 0u)
    {
        AaCorners_RenderRoundImage(&rect, st->radius,
                                   leScheme_GetRenderColor(st->bg, LE_SCHM_BASE),
                                   leRenderer_CurrentColorMode());
    }
}

void Bar_SetPermille(leWidget* bar, uint32_t permille)
{
    bar_state_t *st = bar_state(bar);
    if (st == NULL) { return; }

    if (permille > PERMILLE_MAX) { permille = PERMILLE_MAX; }

    /* Repaint only when the fill's *pixel* width changes — permille has more steps than
     * the bar has pixels, so most updates map to the same width and would otherwise
     * trigger an identical full-widget redraw. */
    uint32_t fw = ((uint32_t)bar->fn->getWidth(bar) * permille) / PERMILLE_MAX;
    if (fw == st->fillw) { return; }

    st->fillw = fw;
    bar->fn->invalidate(bar);   /* track redrawn, fill + corners fresh */
}

void Bar_Enable(leWidget* bar, uint32_t radius, const leScheme* bg,
                uint32_t rgb_from, uint32_t rgb_to)
{
    if (!s_ready)
    {
        s_vt = *bar->fn;
        s_orig_paint = bar->fn->_paint;
        s_vt._paint  = bar_paint;
        s_ready = LE_TRUE;
    }

    bar_state_t *st = bar_state(bar);
    if (st == NULL)
    {
        configASSERT(s_n < BAR_MAX);
        st = &s_state[s_n++];
        st->bar = bar;
    }
    st->radius = radius;
    st->bg     = bg;
    st->from   = rgb_from;
    st->to     = rgb_to;
    st->fillw  = 0u;

    bar->fn = &s_vt;
}
