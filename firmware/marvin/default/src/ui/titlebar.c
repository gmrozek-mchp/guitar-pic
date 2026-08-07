#include "ui/titlebar.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "ui/ui_manager.h"                             /* RenderLock / RenderUnlock */
#include "ui/screens/navigation/screen_navigation.h"   /* ScreenNavigation_ToggleDrawer */
#include "ui/widgets/panel_aa/widget_panel_aa.h"       /* PanelAA_Enable / _EnableDot */
#include "ui/widgets/sparkline/widget_sparkline.h"
#include "health/health_monitor.h"                     /* HealthMonitor_CpuPermille */
#include "net/t1s/t1s_link.h"                          /* T1SLink_GetBusStats */
#include "util/legato_utf8.h"

#include "ui/gfx/render_probe.h"                        /* the frame-timing probe */

#include "gfx/legato/legato.h"
#include "gfx/legato/string/legato_fixedstring.h"
#include "gfx/legato/widget/legato_widget.h"
#include "gfx/legato/widget/button/legato_widget_button.h"
#include "gfx/legato/widget/image/legato_widget_image.h"
#include "gfx/legato/widget/label/legato_widget_label.h"
#include "gfx/legato/generated/le_gen_assets.h"    /* BUTTON_ICON_HAMBURGER, LOGO_*, fonts */
#include "gfx/legato/generated/le_gen_scheme.h"     /* SCHEME_BACKGROUND */

/* Legato quirk: the image widget exposes only its *internal* in-place constructor
 * (`_leImageWidget_Constructor`, external linkage but undeclared in the public
 * header); the public `leImageWidget_Constructor` is declared but never defined,
 * unlike the button/widget/label ones. `leImageWidget_New` uses this same symbol
 * internally, so calling it directly for static allocation is safe. */
extern void _leImageWidget_Constructor(leImageWidget *img);

/* One statically-allocated titlebar instance per base-view screen that uses it
 * (dashboard, wiimotes, bus, system overview + detail). No Legato pool — widgets live in
 * BSS via the in-place Constructors. Geometry mirrors the MGS-authored dashboard
 * titlebar. Sized with slack: exhaustion returns NULL, which costs a screen its whole
 * chrome (and its only way back to the drawer) without any build or assert failure. */
#define TITLEBAR_MAX  8

/* Bar geometry (mirrors the MGS-authored dashboard titlebar it replaced). */
#define BAR_X   12
#define BAR_Y   12
#define BAR_W  1256
#define BAR_H    53

/* ── metric tiles ───────────────────────────────────────────────────────────────
 * The mockup header's two readouts: a rounded-xl zinc-900 card holding a right-aligned
 * caption over a value + unit, and beside it a sparkline over the last PLOT_WINDOW
 * seconds.
 *
 * Metrics are decoded from `le_gen_fonts.c` (regenerate with the MGS skill's
 * font_metrics.py), and a glyph's ADVANCE is not its ink width — `%` at Mono_12 advances
 * 7 px but is 8 px wide, so a box sized to the advance clips its last column. Widths here
 * are sized to the ink; heights are the fonts' own, since a box shorter than its font
 * clips a row top and bottom. */
#define TILE_H     42
#define TILE_Y      5
#define TILE_R     12          /* Tailwind `rounded-xl` */
#define TILE_PAD   12          /* card px-3 */
#define TILE_GAP   12          /* gap-3, readout to plot */
#define TILE_SP     8          /* gap-2, tile to tile */
#define VAL_W      33          /* 3 digits × Bold_18 advance 11              */
#define UNIT_INK    8          /* `%` ink width at Mono_12 (advance 7)       */
#define UNIT_W     (UNIT_INK + 1)
#define CAP_W      (VAL_W + UNIT_INK)   /* caption right-aligns to the unit's ink edge */
#define READOUT_W  (VAL_W + UNIT_W)
#define PLOT_W     80
#define PLOT_H     28
#define TILE_W     (TILE_PAD + READOUT_W + TILE_GAP + PLOT_W + TILE_PAD)

/* Vertical placement is derived from where the INK lands, not from the boxes: Legato puts
 * a single line's baseline at `y + h/2 - baseline/2 + baseline` and the glyphs rise
 * cap-height above it, so stacking two boxes edge to edge leaves the gap between the two
 * fonts' internal leading (8 px here) instead of the mockup's tight `leading-none`.
 *
 * Cap heights 7 (Mono_9) and 13 (Bold_18); baseline offsets 11, 20 and 14 for Mono_9 in a
 * 12-tall box, Bold_18 in 22 and Mono_12 in 16. The three-line block is CAP_INK + INK_GAP
 * + VAL_INK tall, centred in the card, and the unit shares the value's baseline. */
#define CAP_INK     7
#define VAL_INK    13
#define INK_GAP     3
#define INK_TOP    ((TILE_H - (CAP_INK + INK_GAP + VAL_INK)) / 2)
#define CAP_Y      (TILE_Y + INK_TOP + CAP_INK - 11)
#define VAL_Y      (TILE_Y + INK_TOP + CAP_INK + INK_GAP + VAL_INK - 20)
#define UNIT_Y     (VAL_Y + 20 - 14)
#define PLOT_Y_OFF (TILE_Y + (TILE_H - PLOT_H) / 2)

/* Status LED, then the divider and the two tiles laid out leftward from it (the mockup's
 * `w-px h-8 bg-zinc-700 mx-2` between the charts and the dot). */
#define DOT_D      12
#define DOT_X    1037
#define DIV_H      32          /* h-8  */
#define DIV_GAP     8          /* mx-2 */
#define DIV_X      (DOT_X - DIV_GAP - 1)
#define TILE1_X    (DIV_X - DIV_GAP - TILE_SP - 2 * TILE_W)
#define TILE2_X    (DIV_X - DIV_GAP - TILE_W)

/* Fit the plot to its own window rather than 0..100%: at 28 px tall with no axis beside
 * it, a fixed full scale flattens every real signal into a straight line. The floor keeps
 * a near-idle metric reading calm instead of amplifying its noise into a mountain range;
 * the value beside the plot carries the absolute number. */
#define PLOT_MIN_SPAN  150u    /* permille */

/* Newest N of the shared history, chosen for the pitch rather than the span: 24 samples
 * across the 74 px of plot inside PLOT_W is ~3 px each, the mockup's proportion. All 40
 * would be under 2 px, where a jumpy signal reads as a comb instead of a line. */
#define PLOT_WINDOW  24u

/* Sample cadence, and the LED pulse. The tick is the pulse's step, not the sample's — the
 * metrics move once a second (the health supervisor's own sample period) while the LED
 * needs ~10 steps a second to read as a fade. */
#define TICK_MS     100u
#define SAMPLE_MS  1000u
#define PULSE_MS   2000u       /* Tailwind's animate-pulse period */
#define PULSE_MIN   128u       /* its 0.5 opacity trough */

/* Frame probe: an invisible, resizable widget parked in the clear span between the PIC
 * logo (ends x=409) and the first tile (starts x=696). Transparent and contentless, so a
 * damage rect on it paints only the parent panel's background fill — cost strictly
 * proportional to area, which is what makes the per-pixel figure mean anything. */
#define PROBE_X      420
#define PROBE_Y        6
#define PROBE_W_MAX  260
#define PROBE_H_MAX   40

#define TASK_STACK_WORDS  1024u
#define TASK_PRIORITY       2u

typedef enum { METRIC_CPU, METRIC_BUS, METRIC_N } metric_t;

/* Per-metric constants. The line colour and the value colour are the same three bands as
 * the mockup (< warn / < crit / above), and the schemes carrying them are picked for the
 * colour they hold, not the node they name: the sparkline and the AA shapes fill from a
 * scheme's BASE, and only the SCHEME_NODE_* set has BASE equal to an accent colour (the
 * bus screen picks its gauge colours the same way). NODE_MARVIN is #22D3EE,
 * NODE_FAUXMOTE #A78BFA, NODE_FRETBOARD #FB923C and NODE_LIGHTSHOW #F87171 — the
 * mockup's cyan, violet, orange and red exactly. */
typedef struct {
    const char     *caption;
    const char     *unit;
    int             x;
    uint32_t        warn, crit;          /* permille */
    const leScheme *line[3];             /* mono colour carriers: fills from BASE */
    const leScheme *text[3];
} metric_def_t;

static const metric_def_t METRIC[METRIC_N] = {
    [METRIC_CPU] = {
        .caption = "CPU", .unit = "%", .x = TILE1_X, .warn = 500u, .crit = 750u,
        .line = { &SCHEME_NODE_MARVIN, &SCHEME_NODE_FRETBOARD, &SCHEME_NODE_LIGHTSHOW },
        .text = { &SCHEME_TEXT_CYAN_400, &SCHEME_TEXT_TIER_6, &SCHEME_TEXT_TIER_7 },
    },
    [METRIC_BUS] = {
        .caption = "T1S BUS", .unit = "%", .x = TILE2_X, .warn = 450u, .crit = 700u,
        .line = { &SCHEME_NODE_FAUXMOTE, &SCHEME_NODE_FRETBOARD, &SCHEME_NODE_LIGHTSHOW },
        .text = { &SCHEME_TEXT_VIOLET_400, &SCHEME_TEXT_TIER_6, &SCHEME_TEXT_TIER_7 },
    },
};

#define CAP_CH   10            /* "T1S BUS" + NUL, rounded up */

typedef struct {
    leWidget        card;
    leLabelWidget   caption, value, unit;
    leFixedString   caption_fs, value_fs, unit_fs;
    leChar          caption_buf[CAP_CH], value_buf[4], unit_buf[3];
    SparklineWidget plot;
} metric_tile_t;

typedef struct {
    leWidget       bar;
    leButtonWidget nav;
    leImageWidget  guitar, pic, chip;
    leWidget       rule;      /* 1px divider along the bar's bottom edge */
    leWidget       vrule;     /* vertical rule between the tiles and the LED */
    leWidget       dot;       /* system status LED, left of the Microchip logo */
    leWidget       probe;     /* invisible, resized by Titlebar_ProbeFrameUs */
    metric_tile_t  tile[METRIC_N];
} titlebar_t;

static titlebar_t s_bar[TITLEBAR_MAX];
static unsigned   s_n;

/* The history is shared by every instance (see Titlebar_SetShown), so it lives here
 * rather than in a tile, and s_value carries the latest sample for the readouts. */
static SparklineSeries s_series[METRIC_N];
static uint32_t        s_value[METRIC_N];

static titlebar_t *volatile s_live;      /* the instance on screen, or NULL */
static volatile bool        s_dirty;     /* s_live changed — refresh it next tick */

static volatile bool s_pulse_on = true;
static volatile bool s_tiles_on = true;

static StackType_t  s_task_stack[TASK_STACK_WORDS];
static StaticTask_t s_task_tcb;
static bool         s_started;

static void nav_pressed(leButtonWidget *btn)
{
    (void)btn;
    ScreenNavigation_ToggleDrawer();
}

/* ── construction ───────────────────────────────────────────────────────────── */

static void add_label(leLabelWidget *l, leFixedString *fs, leChar *buf, uint32_t cap,
                      leWidget *parent, int x, int y, int w, int h,
                      const leFont *font, const leScheme *scheme, leHAlignment ha)
{
    leLabelWidget_Constructor(l);
    l->fn->setPosition(l, x, y);
    l->fn->setSize(l, w, h);
    l->fn->setScheme(l, scheme);
    l->fn->setBackgroundType(l, LE_WIDGET_BACKGROUND_NONE);
    l->fn->setHAlignment(l, ha);
    l->fn->setVAlignment(l, LE_VALIGN_MIDDLE);

    leFixedString_Constructor(fs, buf, cap);
    ((leString *)fs)->fn->setFont((leString *)fs, (leFont *)font);
    l->fn->setString(l, (leString *)fs);

    parent->fn->addChild(parent, (leWidget *)l);
}

/* One metric tile. The caption, value, unit and plot are CHILDREN of the card, so the
 * whole tile redraws from a single invalidate on the card — the labels are transparent
 * and the plot scrolls, both of which need the card's fill repainted underneath. */
static void add_tile(titlebar_t *t, metric_t m, leWidget *bar)
{
    const metric_def_t *d  = &METRIC[m];
    metric_tile_t      *tl = &t->tile[m];

    leWidget *card = &tl->card;
    leWidget_Constructor(card);
    card->fn->setPosition(card, d->x, TILE_Y);
    card->fn->setSize(card, TILE_W, TILE_H);
    card->fn->setScheme(card, &SCHEME_FILL_ZINC_900);
    card->fn->setBackgroundType(card, LE_WIDGET_BACKGROUND_FILL);
    card->fn->setBorderType(card, LE_WIDGET_BORDER_LINE);
    card->fn->setCornerRadius(card, TILE_R);
    PanelAA_Enable(card);
    bar->fn->addChild(bar, card);

    /* Card-relative from here down. */
    add_label(&tl->caption, &tl->caption_fs, tl->caption_buf, CAP_CH, card,
              TILE_PAD, CAP_Y - TILE_Y, CAP_W, 12,
              (const leFont *)&DejaVuSansMono_9, &SCHEME_TEXT_ZINC_500, LE_HALIGN_RIGHT);
    (void)lestring_set_utf8((leString *)&tl->caption_fs, d->caption);

    add_label(&tl->value, &tl->value_fs, tl->value_buf, sizeof tl->value_buf / sizeof(leChar),
              card, TILE_PAD, VAL_Y - TILE_Y, VAL_W, 22,
              (const leFont *)&DejaVuSansMonoBold_18, d->text[0], LE_HALIGN_RIGHT);

    add_label(&tl->unit, &tl->unit_fs, tl->unit_buf, sizeof tl->unit_buf / sizeof(leChar),
              card, TILE_PAD + VAL_W, UNIT_Y - TILE_Y, UNIT_W, 16,
              (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT);
    (void)lestring_set_utf8((leString *)&tl->unit_fs, d->unit);

    Sparkline_Constructor(&tl->plot, &s_series[m], 1000u);
    Sparkline_SetAutoscale(&tl->plot, PLOT_MIN_SPAN);
    Sparkline_SetWindow(&tl->plot, PLOT_WINDOW);
    Sparkline_SetStyle(&tl->plot, 2u, true);

    leWidget *plot = &tl->plot.widget;
    plot->fn->setPosition(plot, TILE_PAD + READOUT_W + TILE_GAP, PLOT_Y_OFF - TILE_Y);
    plot->fn->setSize(plot, PLOT_W, PLOT_H);
    plot->fn->setScheme(plot, d->line[0]);
    plot->fn->setBackgroundType(plot, LE_WIDGET_BACKGROUND_NONE);
    card->fn->addChild(card, plot);
}

leWidget *Titlebar_Add(leWidget *parent)
{
    if (parent == NULL || s_n >= TITLEBAR_MAX) { return NULL; }
    titlebar_t *t = &s_bar[s_n++];

    leWidget *bar = &t->bar;
    leWidget_Constructor(bar);
    bar->fn->setPosition(bar, BAR_X, BAR_Y);
    bar->fn->setSize(bar, BAR_W, BAR_H);
    bar->fn->setBackgroundType(bar, LE_WIDGET_BACKGROUND_NONE);
    parent->fn->addChild(parent, bar);

    /* Divider between the titlebar and the screen content — the mockup header's
     * `border-b border-zinc-700`. Sits on the bar's bottom row (bar-relative), so
     * it stays inside the bar's rect and needs nothing from the parent's layout. */
    leWidget *rule = &t->rule;
    leWidget_Constructor(rule);
    rule->fn->setPosition(rule, 0, BAR_H - 1);
    rule->fn->setSize(rule, BAR_W, 1);
    rule->fn->setScheme(rule, &SCHEME_FILL_ZINC_700);
    rule->fn->setBackgroundType(rule, LE_WIDGET_BACKGROUND_FILL);
    bar->fn->addChild(bar, rule);

    leButtonWidget *nav = &t->nav;
    leButtonWidget_Constructor(nav);
    nav->fn->setPosition(nav, 1, 4);
    nav->fn->setSize(nav, 40, 40);
    nav->fn->setBackgroundType(nav, LE_WIDGET_BACKGROUND_NONE);
    nav->fn->setBorderType(nav, LE_WIDGET_BORDER_NONE);
    nav->fn->setPressedImage(nav, (leImage *)&BUTTON_ICON_HAMBURGER);
    nav->fn->setReleasedImage(nav, (leImage *)&BUTTON_ICON_HAMBURGER);
    nav->fn->setPressedEventCallback(nav, nav_pressed);
    bar->fn->addChild(bar, (leWidget *)nav);

    leImageWidget *g = &t->guitar;
    _leImageWidget_Constructor(g);
    g->fn->setPosition(g, 52, 1);
    g->fn->setSize(g, 225, 45);
    g->fn->setScheme(g, &SCHEME_BACKGROUND);
    g->fn->setBorderType(g, LE_WIDGET_BORDER_NONE);
    g->fn->setImage(g, (leImage *)&LOGO_GUITAR);
    bar->fn->addChild(bar, (leWidget *)g);

    leImageWidget *p = &t->pic;
    _leImageWidget_Constructor(p);
    p->fn->setPosition(p, 294, 3);
    p->fn->setSize(p, 115, 41);
    p->fn->setScheme(p, &SCHEME_BACKGROUND);
    p->fn->setBorderType(p, LE_WIDGET_BORDER_NONE);
    p->fn->setImage(p, (leImage *)&LOGO_PIC);
    bar->fn->addChild(bar, (leWidget *)p);

    for (unsigned m = 0u; m < (unsigned)METRIC_N; m++)
    {
        add_tile(t, (metric_t)m, bar);
    }

    leWidget *vrule = &t->vrule;
    leWidget_Constructor(vrule);
    vrule->fn->setPosition(vrule, DIV_X, (BAR_H - 1 - DIV_H) / 2);
    vrule->fn->setSize(vrule, 1, DIV_H);
    vrule->fn->setScheme(vrule, &SCHEME_FILL_ZINC_700);
    vrule->fn->setBackgroundType(vrule, LE_WIDGET_BACKGROUND_FILL);
    bar->fn->addChild(bar, vrule);

    /* System status LED (the mockup header's pulsing green dot). Always green: it says
     * "the UI is up", which is true whenever it is on screen — there is no aggregate
     * health signal behind it yet. The pulse is the tick fading the widget's Legato alpha,
     * which the dot paint honours. Sits gap-4 left of the Microchip logo. */
    leWidget *dot = &t->dot;
    leWidget_Constructor(dot);
    dot->fn->setPosition(dot, DOT_X, (BAR_H - DOT_D) / 2);
    dot->fn->setSize(dot, DOT_D, DOT_D);
    dot->fn->setScheme(dot, &SCHEME_FILL_GREEN_500);
    dot->fn->setBackgroundType(dot, LE_WIDGET_BACKGROUND_FILL);
    dot->fn->setBorderType(dot, LE_WIDGET_BORDER_NONE);
    dot->fn->setAlphaEnabled(dot, LE_TRUE);
    PanelAA_EnableDot(dot);
    bar->fn->addChild(bar, dot);

    leWidget *probe = &t->probe;
    leWidget_Constructor(probe);
    probe->fn->setPosition(probe, PROBE_X, PROBE_Y);
    probe->fn->setSize(probe, 1u, 1u);
    probe->fn->setBackgroundType(probe, LE_WIDGET_BACKGROUND_NONE);
    probe->fn->setBorderType(probe, LE_WIDGET_BORDER_NONE);
    probe->flags &= ~LE_WIDGET_ENABLED;   /* never a pick target */
    bar->fn->addChild(bar, probe);

    leImageWidget *c = &t->chip;
    _leImageWidget_Constructor(c);
    c->fn->setPosition(c, 1069, 3);
    c->fn->setSize(c, 177, 41);
    c->fn->setBackgroundType(c, LE_WIDGET_BACKGROUND_NONE);
    c->fn->setBorderType(c, LE_WIDGET_BORDER_NONE);
    c->fn->setImage(c, (leImage *)&LOGO_MICROCHIP);
    bar->fn->addChild(bar, (leWidget *)c);

    return bar;
}

/* ── tick ───────────────────────────────────────────────────────────────────── */

/* Read both metrics and append them to the shared history. Runs whether or not a bar is
 * on screen: it touches no widget, and keeping the history warm is what lets an incoming
 * screen show a full plot rather than start from a blank box. */
static void metric_sample(void)
{
    s_value[METRIC_CPU] = HealthMonitor_CpuPermille();

    T1SLink_BusStats bs;
    s_value[METRIC_BUS] = T1SLink_GetBusStats(&bs) ? bs.util_permille : 0u;

    for (unsigned m = 0u; m < (unsigned)METRIC_N; m++)
    {
        Sparkline_Push(&s_series[m], s_value[m]);
    }
}

/* Rewrite one instance's readouts and recolour both tiles to their current band, then
 * invalidate only what moved: the value text and the plot.
 *
 * Deliberately NOT the whole card. The caption and the unit are static strings, and neither
 * of the two damaged rects reaches a corner box (the value spans card-x 12..44 and the plot
 * 66..145, while the radius-12 corners occupy 0..11 and 146..157), so a tile refresh now
 * skips four of the six glyphs, all four antialiased corners and ~2500 px of fill. The card
 * itself still repaints, clipped to those rects, which is what puts its opaque fill back
 * under the transparent labels and the scrolled plot.
 *
 * This is only safe because AaCorners_Render clips to the draw rect — until that fix it
 * wrote corner boxes through the unchecked `leRenderer_PutPixel` regardless of the damage,
 * i.e. outside the scratch buffer. */
static void tiles_refresh(titlebar_t *t)
{
    for (unsigned m = 0u; m < (unsigned)METRIC_N; m++)
    {
        const metric_def_t *d    = &METRIC[m];
        metric_tile_t      *tl   = &t->tile[m];
        uint32_t            v    = s_value[m];
        unsigned            band = (v > d->crit) ? 2u : (v > d->warn) ? 1u : 0u;
        char                txt[8];

        (void)snprintf(txt, sizeof txt, "%lu", (unsigned long)((v + 5u) / 10u));
        (void)lestring_set_utf8((leString *)&tl->value_fs, txt);
        tl->value.fn->setScheme(&tl->value, d->text[band]);
        tl->plot.widget.fn->setScheme(&tl->plot.widget, d->line[band]);

        /* The value's box is fixed width and right-aligned, so invalidating the whole box is
         * what erases a wider previous reading ("100" → "13"). */
        tl->value.fn->invalidate(&tl->value);
        tl->plot.widget.fn->invalidate(&tl->plot.widget);
    }
}

/* Tailwind's animate-pulse eased in integers: a triangle over the period, smoothstepped,
 * mapped to alpha 255 (opacity 1) at the ends and PULSE_MIN (0.5) at the trough. */
static uint32_t pulse_alpha(uint32_t phase_ms)
{
    uint32_t half = PULSE_MS / 2u;
    uint32_t tri  = (phase_ms < half) ? phase_ms : (PULSE_MS - phase_ms);
    uint32_t x    = (tri * 256u) / half;                       /* 0..256          */
    uint32_t s    = (x * x * (768u - 2u * x)) >> 16;            /* x²(3-2x), 0..256 */

    return 255u - ((255u - PULSE_MIN) * s) / 256u;
}

static void titlebar_task(void *param)
{
    (void)param;

    TickType_t last  = xTaskGetTickCount();
    uint32_t   phase = 0u;
    uint32_t   since = SAMPLE_MS;   /* sample on the first tick */

    for (;;)
    {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(TICK_MS));

        bool sample = (since >= SAMPLE_MS);
        if (sample) { since = 0u; metric_sample(); }
        since += TICK_MS;

        phase = (phase + TICK_MS) % PULSE_MS;

        titlebar_t *t = s_live;
        if (t == NULL) { continue; }

        bool     tiles = s_tiles_on && (sample || s_dirty);
        uint32_t alpha = s_pulse_on ? pulse_alpha(phase) : 255u;
        bool     led   = (alpha != (uint32_t)t->dot.style.alphaAmount);

        /* Decide before taking the lock: a tick with nothing to change must not suspend
         * the render task at all, which is what makes the switches above cost nothing
         * when they are off. setAlphaAmount applies the same guard internally, but only
         * after we would already have paid for the lock. */
        if (!tiles && !led) { continue; }

        UiManager_RenderLock();
        if (tiles)
        {
            s_dirty = false;
            tiles_refresh(t);
        }
        if (led) { (void)t->dot.fn->setAlphaAmount(&t->dot, alpha); }
        UiManager_RenderUnlock();
    }
}

void Titlebar_SetPulseEnabled(bool on) { s_pulse_on = on; }
void Titlebar_SetTilesEnabled(bool on) { s_tiles_on = on; }

uint32_t Titlebar_ProbeFrameUs(unsigned iters, uint32_t w, uint32_t h)
{
    titlebar_t *t = s_live;

    if (t == NULL || iters == 0u || w == 0u || h == 0u) { return 0u; }

    if (w > PROBE_W_MAX) { w = PROBE_W_MAX; }
    if (h > PROBE_H_MAX) { h = PROBE_H_MAX; }

    UiManager_RenderLock();
    t->probe.fn->setSize(&t->probe, w, h);
    UiManager_RenderUnlock();

    uint32_t us = RenderProbe_WidgetUs(&t->probe, iters);

    /* Back to 1x1 so a stray later repaint of the bar costs nothing. */
    UiManager_RenderLock();
    t->probe.fn->setSize(&t->probe, 1u, 1u);
    UiManager_RenderUnlock();

    return us;
}

uint32_t Titlebar_ProbePartUs(unsigned iters, Titlebar_Part part)
{
    titlebar_t *t = s_live;

    if (t == NULL || iters == 0u) { return 0u; }

    leWidget *target;

    switch (part)
    {
        case TITLEBAR_PART_DOT:  target = &t->dot;                          break;
        case TITLEBAR_PART_PLOT: target = &t->tile[METRIC_CPU].plot.widget; break;
        case TITLEBAR_PART_CARD: target = &t->tile[METRIC_CPU].card;        break;
        default: return 0u;
    }

    return RenderProbe_WidgetUs(target, iters);
}

void Titlebar_GetStatus(Titlebar_Status *out)
{
    if (out == NULL) { return; }

    out->pulse        = s_pulse_on;
    out->tiles        = s_tiles_on;
    out->live         = (s_live != NULL);
    out->cpu_permille = s_value[METRIC_CPU];
    out->bus_permille = s_value[METRIC_BUS];
}

void Titlebar_Start(void)
{
    if (s_started) { return; }
    s_started = true;

    for (unsigned m = 0u; m < (unsigned)METRIC_N; m++)
    {
        Sparkline_SeriesInit(&s_series[m]);
    }

    (void)xTaskCreateStatic(titlebar_task, "Titlebar", TASK_STACK_WORDS, NULL,
                            TASK_PRIORITY, s_task_stack, &s_task_tcb);
}

void Titlebar_SetShown(leWidget *bar, bool shown)
{
    /* Resolved by search rather than by casting `bar` back to its instance: the caller
     * holds the bar widget, and a wrong pointer should be ignored, not reinterpreted. */
    for (unsigned i = 0u; i < s_n; i++)
    {
        if (&s_bar[i].bar != bar) { continue; }

        if (shown)
        {
            s_live  = &s_bar[i];
            s_dirty = true;
        }
        else if (s_live == &s_bar[i])
        {
            s_live = NULL;
        }
        return;
    }
}
