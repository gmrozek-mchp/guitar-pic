#include "ui/screens/bus/screen_bus.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "ui/ui_manager.h"   /* CANVAS_BUS, BASE_W, BASE_H, RenderLock/Unlock */
#include "ui/titlebar.h"     /* shared hamburger + logos titlebar */
#include "ui/widgets/panel_aa/widget_panel_aa.h"
#include "ui/widgets/gauge/widget_gauge.h"
#include "ui/widgets/sparkline/widget_sparkline.h"
#include "net/t1s/t1s_link.h"

#include "ui/gfx/ui_surface.h"
#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/string/legato_fixedstring.h"
#include "gfx/legato/widget/legato_widget.h"          /* leWidget_Constructor */
#include "gfx/legato/widget/label/legato_widget_label.h"
#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"                  /* DejaVu fonts */
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* Marvin_PANEL_BUS */
#include "util/legato_utf8.h"

/* 10BASE-T1S bus statistics — KPI tile row + per-node table + chart row, built
 * programmatically into the MGS layer-6 panel (Marvin_PANEL_BUS) and backed by the
 * T1SLink telemetry API. Refreshed ~1 Hz only while shown.
 *
 * Layout mirrors tools' BusStatsScreen.tsx mockup, with Tailwind units resolved to
 * pixels (gap-3 = 12, rounded = 4, px-4 = 16, text-xs/sm/xl = 12/14/20) and the
 * table's column widths scaled to this panel's width. Every section is a rounded
 * AA card: zinc-900 fill + 1px zinc-700 border (SCHEME_FILL_ZINC_900's shadowDark
 * is #404040 ≈ zinc-700, so the stock border colour already matches).
 *
 * The utilization gauge and the three plots are custom-painted widgets
 * (ui/widgets/gauge, ui/widgets/sparkline) plus vector-drawn rounded rects for the bars.
 * Values come from the data-source accessors below, which read either the live
 * T1SLink telemetry or a simulated feed (see BUS_SIM_DEFAULT). */

#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))
static uint16_t FB_NOCACHE s_fb[BASE_W * BASE_H];

/* ── layout ─────────────────────────────────────────────────────────────────
 * Content spans x 16..1264 below the shared titlebar (top ~65px). Rows are
 * separated by GAP, matching the mockup's flex gap-3. */
#define MARGIN     16
#define GAP        12
#define CARD_R      4          /* Tailwind `rounded` */
#define PAD        16          /* card px-4 */
#define CONTENT_X  MARGIN
#define CONTENT_W  (BASE_W - 2 * MARGIN)   /* 1248 */

#define KPI_Y      76
#define KPI_H      86
#define GAUGE_W    200         /* mockup's fixed-width utilization card */
#define KPI_W      137         /* the seven flex cards */

#define TBL_Y      (KPI_Y + KPI_H + GAP)          /* 174 */
#define TBL_HDR_H  36                             /* py-2.5 + text-xs */
#define ROW_H      40
#define MAX_ROWS   10
#define TBL_H      334                            /* mockup height; holds 7 rows */

#define CHART_Y    (TBL_Y + TBL_H + GAP)          /* 520 */
#define CHART_H    (BASE_H - MARGIN - CHART_Y)    /* 264 */
#define CHART_W    ((CONTENT_W - 2 * GAP) / 3)    /* 408 */

/* Plot area inside a chart card: below the title, above the footer/axis labels. */
#define AXIS_W     26                             /* y-tick gutter */
#define PLOT_Y     (CHART_Y + 34)
#define PLOT_H     (CHART_H - 34 - 26)

#define DOT_NODE   8           /* w-2  */
#define DOT_STAT   6           /* w-1.5 */
#define BADGE_W    52
#define BADGE_H    20
#define BAR_R       2          /* TX bars' rounded top corners */
#define EBAR_H      8          /* error pill track/fill height (h-2) */

/* Column x / width inside the table card (mockup's grid, scaled to CONTENT_W). */
/* C_HBAGE shows the age of the node's last heartbeat, not a round-trip latency —
 * marvin has no ping/echo protocol (see docs/t1s-podl-link.md §7.2). */
enum { C_NODE, C_ADDR, C_ROLE, C_TXTOT, C_RXTOT, C_TXR, C_RXR, C_CRC, C_SYM, C_HBAGE, C_STATUS, C_COUNT };
static const struct { const char *hdr; int x, w; } COL[C_COUNT] = {
    { "NODE",      32, 187 }, { "ADDR",    219,  63 }, { "ROLE",    282,  78 },
    { "TX TOTAL", 360, 133 }, { "RX TOTAL",493, 133 }, { "TX RATE", 626, 121 },
    { "RX RATE",  747, 121 }, { "CRC ERR", 868,  88 }, { "SYM ERR", 956,  88 },
    { "LAST HB", 1044, 101 }, { "STATUS", 1145, 101 },
};

/* ── static widget storage ──────────────────────────────────────────────────
 * Widgets live in BSS (no Legato pool / LE_MALLOC): the in-place Constructors build
 * them exactly as leX_New would after LE_MALLOC. The widget set is fixed, so the
 * pools are sized to it (configASSERT catches undersizing at bring-up). Labels are
 * transparent; refresh rewrites the string then invalidates the whole panel so the
 * opaque card backdrop repaints under them. */
#define CAP       28           /* longest string: "BUS UTILIZATION HISTORY" */
#define LBL_MAX  200
#define WGT_MAX   96

static leChar        s_buf[LBL_MAX][CAP];
static leFixedString s_fs[LBL_MAX];
static leLabelWidget s_lbl[LBL_MAX];
static unsigned      s_nlbl;
static leWidget      s_wgt[WGT_MAX];
static unsigned      s_nwgt;

static leWidget *next_widget(void)
{
    configASSERT(s_nwgt < WGT_MAX);
    leWidget *p = &s_wgt[s_nwgt++];
    leWidget_Constructor(p);
    return p;
}

/* A label box shorter than its font clips a row off the text — top and bottom, so the
 * visible symptom is a cut descender (several boxes here are 14px around a 16px font).
 * Legato centres the text at `y + h/2 - fontH/2` (leUtils_ArrangeRectangleRelative) and
 * clips to the widget rect, so growing the box to the font's height and shifting y by the
 * same halved amount fits the glyphs without moving them. */
static void fit_font(const leFont *font, int *y, int *h)
{
    if ((font == NULL) || (font->type != LE_RASTER_FONT)) { return; }

    int fh = (int)((const leRasterFont *)font)->height;

    if (*h < fh)
    {
        *y += (*h / 2) - (fh / 2);
        *h  = fh;
    }
}

static leLabelWidget *add_label(int x, int y, int w, int h, const leFont *font,
                                const leScheme *scheme, leHAlignment ha)
{
    configASSERT(s_nlbl < LBL_MAX);

    fit_font(font, &y, &h);

    leLabelWidget *l = &s_lbl[s_nlbl];
    leLabelWidget_Constructor(l);
    l->fn->setPosition(l, x, y);
    l->fn->setSize(l, w, h);
    l->fn->setScheme(l, scheme);
    l->fn->setBackgroundType(l, LE_WIDGET_BACKGROUND_NONE);
    l->fn->setHAlignment(l, ha);
    l->fn->setVAlignment(l, LE_VALIGN_MIDDLE);

    leFixedString *fs = &s_fs[s_nlbl];
    leFixedString_Constructor(fs, s_buf[s_nlbl], CAP);
    ((leString *)fs)->fn->setFont((leString *)fs, (leFont *)font);
    l->fn->setString(l, (leString *)fs);
    s_nlbl++;

    Marvin_PANEL_BUS->fn->addChild(Marvin_PANEL_BUS, (leWidget *)l);
    return l;
}

static void set_text(leLabelWidget *l, const char *s)
{
    if (l != NULL) { (void)lestring_set_utf8(l->fn->getString(l), s); }
}

/* A section card: zinc-900 fill, 1px border, 4px AA-rounded corners. */
static leWidget *add_card(int x, int y, int w, int h)
{
    leWidget *p = next_widget();
    p->fn->setPosition(p, x, y);
    p->fn->setSize(p, w, h);
    p->fn->setScheme(p, &SCHEME_FILL_ZINC_900);
    p->fn->setBackgroundType(p, LE_WIDGET_BACKGROUND_FILL);
    p->fn->setBorderType(p, LE_WIDGET_BORDER_LINE);
    p->fn->setCornerRadius(p, CARD_R);
    PanelAA_Enable(p);
    Marvin_PANEL_BUS->fn->addChild(Marvin_PANEL_BUS, p);
    return p;
}

/* A 1px horizontal rule (the table's header/row separators). */
static void add_rule(int x, int y, int w, const leScheme *scheme)
{
    leWidget *p = next_widget();
    p->fn->setPosition(p, x, y);
    p->fn->setSize(p, w, 1);
    p->fn->setScheme(p, scheme);
    p->fn->setBackgroundType(p, LE_WIDGET_BACKGROUND_FILL);
    Marvin_PANEL_BUS->fn->addChild(Marvin_PANEL_BUS, p);
}

/* A filled chart shape, rounded as the mockup draws it: SHAPE_PILL is a stadium
 * (`rounded-full` — the error track and its fill), SHAPE_BAR is a column with only its
 * top corners rounded (`radius={[2,2,0,0]}` — the TX-rate bars). Both hand the whole
 * shape to a vector paint that redraws it from the current rect, so the ones that get
 * resized on every refresh round correctly at every size. */
typedef enum { SHAPE_PILL, SHAPE_BAR } shape_t;

static leWidget *add_rect(int x, int y, int w, int h, const leScheme *scheme, shape_t shape)
{
    leWidget *p = next_widget();
    p->fn->setPosition(p, x, y);
    p->fn->setSize(p, w, h);
    p->fn->setScheme(p, scheme);
    if (shape == SHAPE_PILL) { PanelAA_EnableDot(p); }
    else                     { PanelAA_EnableRoundTop(p, BAR_R); }
    Marvin_PANEL_BUS->fn->addChild(Marvin_PANEL_BUS, p);
    return p;
}

/* A filled AA circle (PanelAA_EnableDot draws it and owns the fill). Never sets
 * cornerRadius: Legato's stock rounded-rect paint hangs once the radius reaches half
 * the widget size. */
static leWidget *add_dot(int x, int y, int d, const leScheme *scheme)
{
    leWidget *p = next_widget();
    p->fn->setPosition(p, x, y);
    p->fn->setSize(p, d, d);
    p->fn->setScheme(p, scheme);
    p->fn->setBackgroundType(p, LE_WIDGET_BACKGROUND_FILL);
    PanelAA_EnableDot(p);
    Marvin_PANEL_BUS->fn->addChild(Marvin_PANEL_BUS, p);
    return p;
}

/* A role pill: a rounded zinc-800 fill with its caption drawn over it (two widgets
 * so the fill and the text colour are independent — no bespoke 2-tone scheme). */
static void add_badge(int x, int y, const char *text, const leScheme *text_scheme)
{
    leWidget *bg = next_widget();
    bg->fn->setPosition(bg, x, y);
    bg->fn->setSize(bg, BADGE_W, BADGE_H);
    bg->fn->setScheme(bg, &SCHEME_FILL_ZINC_800);
    bg->fn->setBackgroundType(bg, LE_WIDGET_BACKGROUND_FILL);
    bg->fn->setCornerRadius(bg, CARD_R);
    PanelAA_EnableRoundImage(bg);
    Marvin_PANEL_BUS->fn->addChild(Marvin_PANEL_BUS, bg);

    set_text(add_label(x, y, BADGE_W, BADGE_H, (const leFont *)&DejaVuSansMonoBold_12,
                       text_scheme, LE_HALIGN_CENTER), text);
}

/* ── formatting (mirrors the mockup) ────────────────────────────────────────*/
static void fmt_count(uint32_t v, char *b, size_t n)
{
    if (v >= 1000000u) { (void)snprintf(b, n, "%lu.%02luM", (unsigned long)(v / 1000000u), (unsigned long)((v % 1000000u) / 10000u)); }
    else if (v >= 1000u) { (void)snprintf(b, n, "%lu.%luK", (unsigned long)(v / 1000u), (unsigned long)((v % 1000u) / 100u)); }
    else { (void)snprintf(b, n, "%lu", (unsigned long)v); }
}
static void fmt_rate(uint32_t v, char *b, size_t n)
{
    if (v >= 1000u) { (void)snprintf(b, n, "%lu.%luk/s", (unsigned long)(v / 1000u), (unsigned long)((v % 1000u) / 100u)); }
    else { (void)snprintf(b, n, "%lu/s", (unsigned long)v); }
}

static const leScheme *node_scheme(const char *type)
{
    if (type == NULL)                       { return &SCHEME_NODE_MARVIN; }
    if (strncmp(type, "fauxmote", 8) == 0)  { return &SCHEME_NODE_FAUXMOTE; }
    if (strcmp(type, "guitar") == 0)        { return &SCHEME_NODE_GUITAR; }
    if (strcmp(type, "fretboard") == 0)     { return &SCHEME_NODE_FRETBOARD; }
    if (strcmp(type, "beatbox") == 0)       { return &SCHEME_NODE_BEATBOX; }
    if (strcmp(type, "lemmy") == 0)         { return &SCHEME_NODE_LEMMY; }
    if (strcmp(type, "lightshow") == 0)     { return &SCHEME_NODE_LIGHTSHOW; }
    return &SCHEME_NODE_MARVIN;
}
static const leScheme *crc_scheme(uint16_t v) { return (v > 15u) ? &SCHEME_TEXT_RED_400 : (v > 6u) ? &SCHEME_TEXT_YELLOW_400 : &SCHEME_TEXT_ZINC_500; }
static const leScheme *sym_scheme(uint16_t v) { return (v > 8u)  ? &SCHEME_TEXT_RED_400 : (v > 3u) ? &SCHEME_TEXT_YELLOW_400 : &SCHEME_TEXT_ZINC_500; }

/* ── data source: live telemetry, or a simulated feed ────────────────────────
 * Every value on this screen is read through these four accessors, so a synthetic
 * feed can stand in for the live T1S telemetry. That matters for development: a
 * healthy bus is near-idle and error-free, so the thresholds, colour ramps, gauge
 * sweep and chart scaling never exercise on real data (and followers report zeros
 * until each is reflashed with the v2 heartbeat).
 *
 * The simulator mirrors the mockup's seed values and jitter, keeps its own
 * accumulators, and marks beatbox absent (it genuinely isn't on the bus yet) so the
 * OFFLINE styling is covered too. Toggle with ScreenBus_SetSimulated(); when it is
 * on, the UPTIME tile's sub-line reads SIMULATED so the screen never lies about
 * where its numbers came from. */

/* Default for development builds. Set to false to ship live telemetry. */
#define BUS_SIM_DEFAULT   true

static bool s_sim = BUS_SIM_DEFAULT;

typedef struct {
    const char *type;
    uint8_t     node_id;
    uint16_t    base_tx, base_rx;   /* frames/s at unity jitter */
    uint8_t     base_err;
    bool        present;
} sim_node_t;

/* Row 0 is marvin (the coordinator/self row); the rest mirror the real node table's
 * ids so the ADDR column stays representative. */
static const sim_node_t SIM_NODE[] = {
    { "marvin",    0u, 1140u, 560u, 1u, true  },
    { "fauxmote",  1u,  310u, 870u, 5u, true  },
    { "guitar",    3u,  265u, 740u, 2u, true  },
    { "fretboard", 4u,  230u, 700u, 9u, true  },
    { "beatbox",   5u,  185u, 660u, 3u, false },   /* not on the bus yet */
    { "lemmy",     6u,  145u, 600u, 2u, true  },
    { "lightshow", 7u,  410u, 690u, 4u, true  },
};
#define SIM_N  (sizeof SIM_NODE / sizeof SIM_NODE[0])

static struct {
    uint32_t tx, rx, tx_rate, rx_rate, age_ms;
    uint16_t crc, sym;
} s_sim_rt[SIM_N];

static uint32_t s_sim_util = 320u;   /* permille */
static uint32_t s_sim_uptime;
static bool     s_sim_seeded;

/* xorshift32 — a deterministic, allocation-free jitter source. */
static uint32_t s_rng = 0x1BADB002u;
static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

/* Seed the totals so the counters start in a plausible place (millions of frames,
 * a handful of errors) instead of climbing from zero. */
static void sim_seed(void)
{
    for (unsigned i = 0; i < SIM_N; i++)
    {
        s_sim_rt[i].tx  = (uint32_t)SIM_NODE[i].base_tx * 4800u + (rnd() % 20000u);
        s_sim_rt[i].rx  = (uint32_t)SIM_NODE[i].base_rx * 4800u + (rnd() % 20000u);
        /* Seeded low enough that the per-node CRC/SYM columns start spread across
         * all three threshold tiers (zinc / yellow / red) rather than saturating
         * red — the point of the simulated feed is to exercise the styling. */
        s_sim_rt[i].crc = (uint16_t)(SIM_NODE[i].base_err * 2u + (rnd() % 4u));
        s_sim_rt[i].sym = (uint16_t)(SIM_NODE[i].base_err * 1u + (rnd() % 3u));
    }
    s_sim_uptime = 15397u;   /* 4:16:37, like the mockup */
    s_sim_seeded = true;
}

/* Advance one refresh tick: jitter each node's rate, accumulate totals, sprinkle
 * errors, and walk the utilization figure around 25–43%. */
static void sim_advance(void)
{
    if (!s_sim_seeded) { sim_seed(); }

    for (unsigned i = 0; i < SIM_N; i++)
    {
        uint32_t j = 820u + (rnd() % 361u);            /* 0.82 .. 1.18 */
        if (!SIM_NODE[i].present)
        {
            s_sim_rt[i].tx_rate = 0u;
            s_sim_rt[i].rx_rate = 0u;
            s_sim_rt[i].age_ms  = 30000u;              /* long gone */
            continue;
        }
        s_sim_rt[i].tx_rate = (uint32_t)SIM_NODE[i].base_tx * j / 1000u;
        s_sim_rt[i].rx_rate = (uint32_t)SIM_NODE[i].base_rx * j / 1000u;
        s_sim_rt[i].tx     += s_sim_rt[i].tx_rate;
        s_sim_rt[i].rx     += s_sim_rt[i].rx_rate;
        s_sim_rt[i].age_ms  = (i == 0u) ? 0u : (rnd() % 600u);

        if ((rnd() % 100u) < 4u) { s_sim_rt[i].crc++; }
        if ((rnd() % 100u) < 2u) { s_sim_rt[i].sym++; }
    }

    s_sim_util = 250u + (rnd() % 190u);
    s_sim_uptime++;
}

static void sim_fill(unsigned i, T1SLink_NodeStats *out)
{
    out->node_id  = SIM_NODE[i].node_id;
    out->type     = SIM_NODE[i].type;
    out->present  = SIM_NODE[i].present;
    out->age_ms   = s_sim_rt[i].age_ms;
    out->tx_count = s_sim_rt[i].tx;
    out->rx_count = s_sim_rt[i].rx;
    out->tx_rate  = s_sim_rt[i].tx_rate;
    out->rx_rate  = s_sim_rt[i].rx_rate;
    out->crc_err  = s_sim_rt[i].crc;
    out->sym_err  = s_sim_rt[i].sym;
}

static uint8_t bus_node_count(void)
{
    return s_sim ? (uint8_t)(SIM_N - 1u) : T1SLink_NodeTableCount();
}

static bool bus_self(T1SLink_NodeStats *out)
{
    if (!s_sim) { return T1SLink_GetSelfStats(out); }
    if (!s_sim_seeded) { sim_seed(); }
    sim_fill(0u, out);
    return true;
}

static bool bus_node(uint8_t idx, T1SLink_NodeStats *out)
{
    if (!s_sim) { return T1SLink_GetNodeStats(idx, out); }
    if ((uint32_t)idx + 1u >= SIM_N) { return false; }
    if (!s_sim_seeded) { sim_seed(); }
    sim_fill((unsigned)idx + 1u, out);
    return true;
}

static bool bus_stats(T1SLink_BusStats *out)
{
    if (!s_sim) { return T1SLink_GetBusStats(out); }
    if (!s_sim_seeded) { sim_seed(); }

    uint32_t tx = 0u, rx = 0u, crc = 0u, sym = 0u;
    uint8_t  online = 0u;
    for (unsigned i = 0; i < SIM_N; i++)
    {
        tx  += s_sim_rt[i].tx;
        rx  += s_sim_rt[i].rx;
        crc += s_sim_rt[i].crc;
        sym += s_sim_rt[i].sym;
        if (SIM_NODE[i].present) { online++; }
    }
    uint32_t frames = tx + rx;

    out->util_permille = s_sim_util;
    out->tx_total      = tx;
    out->rx_total      = rx;
    out->crc_total     = crc;
    out->sym_total     = sym;
    out->err_rate_ppm  = (frames != 0u)
        ? (uint32_t)(((uint64_t)(crc + sym) * 1000000u) / frames) : 0u;
    out->uptime_s      = s_sim_uptime;
    out->nodes_online  = online;
    out->nodes_total   = (uint8_t)SIM_N;
    return true;
}

/* ── widget handles for the refresh ─────────────────────────────────────────*/
static leLabelWidget *s_kpi_util, *s_kpi_tx, *s_kpi_txr, *s_kpi_rx,
                     *s_kpi_crc, *s_kpi_sym, *s_kpi_err, *s_kpi_nodes, *s_kpi_up,
                     *s_kpi_upsub;   /* "10BASE-T1S" / "SIMULATED" */

typedef struct {
    bool           used;
    leLabelWidget *txtot, *rxtot, *txr, *rxr, *crc, *sym, *hbage, *status;
    leWidget      *statusdot;
} row_t;
static row_t s_row[MAX_ROWS];

/* Chart widgets resized on each refresh: the TX-rate bars (bottom-anchored, so both
 * y and height move) and the error bars (width only), plus their value labels. */
static leWidget      *s_bar[MAX_ROWS];
static leWidget      *s_ebar[MAX_ROWS];
static leLabelWidget *s_etot[MAX_ROWS], *s_esplit[MAX_ROWS];
static int            s_etrack_w = 1;

static volatile bool s_shown;
static StackType_t   s_task_stack[1024];
static StaticTask_t  s_task_tcb;

/* ── one KPI tile: card + caption + value (+ optional sub-line) ──────────────*/
static leLabelWidget *kpi(int x, int w, const char *caption, const leScheme *vscheme,
                          leLabelWidget **sub_out, const char *sub_static,
                          const leScheme *sub_scheme)
{
    add_card(x, KPI_Y, w, KPI_H);
    set_text(add_label(x + PAD, KPI_Y + 10, w - 2 * PAD, 16,
                       (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500,
                       LE_HALIGN_LEFT), caption);
    leLabelWidget *val = add_label(x + PAD, KPI_Y + 32, w - 2 * PAD, 28,
                                   (const leFont *)&DejaVuSansMonoBold_24, vscheme, LE_HALIGN_LEFT);
    if (sub_out != NULL)
    {
        *sub_out = add_label(x + PAD, KPI_Y + 64, w - 2 * PAD, 14,
                             (const leFont *)&DejaVuSansMono_12,
                             (sub_scheme != NULL) ? sub_scheme : &SCHEME_TEXT_ZINC_500,
                             LE_HALIGN_LEFT);
        if (sub_static != NULL) { set_text(*sub_out, sub_static); }
    }
    return val;
}

void ScreenBus_InitSurface(void)
{
    UiSurface_Set(CANVAS_BUS, BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565, s_fb);
}

static void refresh_all(void);
static void bus_task(void *param);

void ScreenBus_Setup(void)
{
    gfxcSetWindowPosition(CANVAS_BUS, 0, 0);
    gfxcSetWindowSize(CANVAS_BUS, BASE_W, BASE_H);
    Marvin_PANEL_BUS->fn->setBackgroundType(Marvin_PANEL_BUS, LE_WIDGET_BACKGROUND_FILL);

    /* Shared titlebar (hamburger + logos), same as the other base views. */
    Titlebar_Add(Marvin_PANEL_BUS);
    ScreenBus_SetInput(false);

    /* ── KPI row: the wide utilization card, then seven equal tiles ─────────
     * Child paint order matters: card, then the gauge arc, then the % readout on
     * top of it (as in the mockup). */
    add_card(CONTENT_X, KPI_Y, GAUGE_W, KPI_H);
    {
        leWidget *g = next_widget();
        g->fn->setPosition(g, CONTENT_X + PAD, KPI_Y + 20);
        g->fn->setSize(g, 76, 44);
        g->fn->setBackgroundType(g, LE_WIDGET_BACKGROUND_NONE);
        Gauge_Enable(g, 7u, &SCHEME_FILL_ZINC_700);
        Marvin_PANEL_BUS->fn->addChild(Marvin_PANEL_BUS, g);
    }
    s_kpi_util = add_label(CONTENT_X + PAD, KPI_Y + 46, 76, 16,
                           (const leFont *)&DejaVuSansMonoBold_14, &SCHEME_TEXT_CYAN_400,
                           LE_HALIGN_CENTER);
    set_text(add_label(CONTENT_X + 106, KPI_Y + 28, 84, 16, (const leFont *)&DejaVuSansMono_12,
                       &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT), "BUS");
    set_text(add_label(CONTENT_X + 106, KPI_Y + 44, 84, 16, (const leFont *)&DejaVuSansMono_12,
                       &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT), "UTILIZATION");

    int x = CONTENT_X + GAUGE_W + GAP;
    s_kpi_tx    = kpi(x, KPI_W, "TOTAL TX",   &SCHEME_TEXT_ZINC_200,   &s_kpi_txr, NULL,
                      &SCHEME_TEXT_GREEN_400);            x += KPI_W + GAP;
    s_kpi_rx    = kpi(x, KPI_W, "TOTAL RX",   &SCHEME_TEXT_ZINC_200,   NULL, NULL, NULL);
                                                          x += KPI_W + GAP;
    s_kpi_crc   = kpi(x, KPI_W, "CRC ERRORS", &SCHEME_TEXT_YELLOW_400, NULL, NULL, NULL);
                                                          x += KPI_W + GAP;
    s_kpi_sym   = kpi(x, KPI_W, "SYMBOL ERR", &SCHEME_TEXT_YELLOW_400, NULL, NULL, NULL);
                                                          x += KPI_W + GAP;
    s_kpi_err   = kpi(x, KPI_W, "ERROR RATE", &SCHEME_TEXT_ZINC_200,   NULL, NULL, NULL);
                                                          x += KPI_W + GAP;
    s_kpi_nodes = kpi(x, KPI_W, "NODES",      &SCHEME_TEXT_GREEN_400,  NULL, "ONLINE", NULL);
                                                          x += KPI_W + GAP;
    s_kpi_up    = kpi(x, (CONTENT_X + CONTENT_W) - x, "UPTIME", &SCHEME_TEXT_VIOLET_400,
                      &s_kpi_upsub, "10BASE-T1S", NULL);

    /* ── node table: one card holding the header, a rule, then the rows ───── */
    add_card(CONTENT_X, TBL_Y, CONTENT_W, TBL_H);
    for (int c = 0; c < C_COUNT; c++)
    {
        set_text(add_label(COL[c].x, TBL_Y + 10, COL[c].w, 16,
                           (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500,
                           LE_HALIGN_LEFT), COL[c].hdr);
    }
    add_rule(CONTENT_X, TBL_Y + TBL_HDR_H, CONTENT_W, &SCHEME_FILL_ZINC_700);

    /* Row 0 = marvin (self), rows 1.. = the follower node table. */
    uint8_t nfollow = bus_node_count();
    uint8_t nrows   = (uint8_t)(1u + nfollow);
    if (nrows > MAX_ROWS) { nrows = MAX_ROWS; }

    int row0_y = TBL_Y + TBL_HDR_H + 1;
    for (uint8_t r = 0; r < nrows; r++)
    {
        int  y    = row0_y + r * ROW_H;
        bool self = (r == 0u);
        T1SLink_NodeStats st;
        if (!(self ? bus_self(&st) : bus_node((uint8_t)(r - 1u), &st)))
        {
            continue;
        }

        const leScheme *nsc = self ? &SCHEME_NODE_MARVIN : node_scheme(st.type);
        char tmp[CAP];

        /* NODE: colour dot + capitalized node name, both in the node's colour. */
        add_dot(COL[C_NODE].x, y + (ROW_H - DOT_NODE) / 2, DOT_NODE, nsc);
        (void)snprintf(tmp, sizeof tmp, "%s", (st.type != NULL) ? st.type : "?");
        if (tmp[0] >= 'a' && tmp[0] <= 'z') { tmp[0] = (char)(tmp[0] - 32); }
        set_text(add_label(COL[C_NODE].x + DOT_NODE + 8, y,
                           COL[C_NODE].w - DOT_NODE - 8, ROW_H,
                           (const leFont *)&DejaVuSansMonoBold_14, nsc, LE_HALIGN_LEFT), tmp);

        (void)snprintf(tmp, sizeof tmp, "0x%02X", (unsigned)st.node_id);
        set_text(add_label(COL[C_ADDR].x, y, COL[C_ADDR].w, ROW_H,
                           (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500,
                           LE_HALIGN_LEFT), tmp);

        add_badge(COL[C_ROLE].x, y + (ROW_H - BADGE_H) / 2,
                  self ? "COORD" : "NODE",
                  self ? &SCHEME_TEXT_CYAN_400 : &SCHEME_TEXT_ZINC_500);

        row_t *w = &s_row[r];
        w->used  = true;
        #define CELL(col, scheme) \
            add_label(COL[col].x, y, COL[col].w, ROW_H, \
                      (const leFont *)&DejaVuSansMono_14, (scheme), LE_HALIGN_LEFT)
        w->txtot = CELL(C_TXTOT, &SCHEME_TEXT_ZINC_200);
        w->rxtot = CELL(C_RXTOT, &SCHEME_TEXT_ZINC_200);
        w->txr   = CELL(C_TXR,   &SCHEME_TEXT_GREEN_400);
        w->rxr   = CELL(C_RXR,   &SCHEME_TEXT_BLUE_400);
        w->crc   = CELL(C_CRC,   &SCHEME_TEXT_ZINC_500);
        w->sym   = CELL(C_SYM,   &SCHEME_TEXT_ZINC_500);
        w->hbage = CELL(C_HBAGE, &SCHEME_TEXT_ZINC_400);
        #undef CELL

        w->statusdot = add_dot(COL[C_STATUS].x, y + (ROW_H - DOT_STAT) / 2, DOT_STAT,
                               &SCHEME_FILL_GREEN_400);
        w->status = add_label(COL[C_STATUS].x + DOT_STAT + 6, y,
                              COL[C_STATUS].w - DOT_STAT - 6, ROW_H,
                              (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_GREEN_400,
                              LE_HALIGN_LEFT);

        /* Row separator, all but the last (mockup makes the last transparent). */
        if (r + 1u < nrows)
        {
            add_rule(CONTENT_X, y + ROW_H - 1, CONTENT_W, &SCHEME_FILL_ZINC_800);
        }
    }

    /* ── chart row: three cards ───────────────────────────────────────────── */
    static const char *CHART_TITLE[3] = {
        "BUS UTILIZATION HISTORY", "TX RATE BY NODE", "ERROR COUNT BY NODE",
    };
    int cx[3];
    for (int i = 0; i < 3; i++)
    {
        cx[i] = CONTENT_X + i * (CHART_W + GAP);
        add_card(cx[i], CHART_Y, CHART_W, CHART_H);
        set_text(add_label(cx[i] + GAP, CHART_Y + GAP, CHART_W - 2 * GAP, 16,
                           (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500,
                           LE_HALIGN_LEFT), CHART_TITLE[i]);
    }

    /* 1) Utilization history: y-axis ticks, the sparkline plot, and the time hints.
     *    Scale matches the mockup's 0..60% Y domain. */
    {
        int px = cx[0] + GAP + AXIS_W;
        int pw = CHART_W - 2 * GAP - AXIS_W;
        for (int i = 0; i < 3; i++)   /* 60 / 30 / 0 top-to-bottom */
        {
            char t[8];
            (void)snprintf(t, sizeof t, "%d", 60 - i * 30);
            set_text(add_label(cx[0] + GAP, PLOT_Y + (PLOT_H - 12) * i / 2, AXIS_W - 4, 12,
                               (const leFont *)&DejaVuSansMono_9, &SCHEME_TEXT_ZINC_600,
                               LE_HALIGN_RIGHT), t);
        }
        leWidget *spark = next_widget();
        spark->fn->setPosition(spark, px, PLOT_Y);
        spark->fn->setSize(spark, pw, PLOT_H);
        spark->fn->setScheme(spark, &SCHEME_NODE_MARVIN);   /* mono cyan = line colour */
        spark->fn->setBackgroundType(spark, LE_WIDGET_BACKGROUND_NONE);
        Sparkline_Enable(spark);
        Sparkline_SetScale(600u);
        Marvin_PANEL_BUS->fn->addChild(Marvin_PANEL_BUS, spark);

        set_text(add_label(px, CHART_Y + CHART_H - 20, pw / 2, 14,
                           (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_600,
                           LE_HALIGN_LEFT), "<- 40s ago");   /* 40 samples @ 1 Hz */
        set_text(add_label(px + pw / 2, CHART_Y + CHART_H - 20, pw / 2, 14,
                           (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_600,
                           LE_HALIGN_RIGHT), "now ->");
    }

    /* 2) TX rate by node: one bottom-anchored bar per row, node-coloured, with the
     *    node's initial beneath it. Heights are set on refresh. */
    {
        int pw   = CHART_W - 2 * GAP;
        int slot = pw / (int)((nrows > 0u) ? nrows : 1u);
        int bw   = (slot > 16) ? (slot - 8) : slot;
        for (uint8_t r = 0; r < nrows; r++)
        {
            T1SLink_NodeStats st;
            if (!((r == 0u) ? bus_self(&st)
                            : bus_node((uint8_t)(r - 1u), &st))) { continue; }
            const leScheme *nsc = (r == 0u) ? &SCHEME_NODE_MARVIN : node_scheme(st.type);
            int bx = cx[1] + GAP + r * slot + (slot - bw) / 2;

            s_bar[r] = add_rect(bx, PLOT_Y + PLOT_H - 1, bw, 1, nsc, SHAPE_BAR);

            /* Full node name under the bar, centred on the whole slot (not just the
             * bar) — at 9px even "Lightshow" fits the ~54px slot. */
            char t[CAP];
            (void)snprintf(t, sizeof t, "%s", (st.type != NULL) ? st.type : "?");
            if (t[0] >= 'a' && t[0] <= 'z') { t[0] = (char)(t[0] - 32); }
            set_text(add_label(cx[1] + GAP + r * slot, PLOT_Y + PLOT_H + 3, slot, 12,
                               (const leFont *)&DejaVuSansMono_9, &SCHEME_TEXT_ZINC_500,
                               LE_HALIGN_CENTER), t);
        }
    }

    /* 3) Error count by node: name, a pill track with a node-coloured fill, the
     *    total, and the CRC/SYM split. Fill widths are set on refresh. */
    {
        int rows_y = CHART_Y + 34;
        int row_h  = (CHART_H - 42) / (int)((nrows > 0u) ? nrows : 1u);
        int name_w = 72, tot_w = 28, split_w = 108;
        int track_x = cx[2] + GAP + name_w + 6;
        int track_w = CHART_W - 2 * GAP - name_w - tot_w - split_w - 18;

        for (uint8_t r = 0; r < nrows; r++)
        {
            T1SLink_NodeStats st;
            if (!((r == 0u) ? bus_self(&st)
                            : bus_node((uint8_t)(r - 1u), &st))) { continue; }
            const leScheme *nsc = (r == 0u) ? &SCHEME_NODE_MARVIN : node_scheme(st.type);
            int y = rows_y + r * row_h;
            char t[CAP];

            (void)snprintf(t, sizeof t, "%s", (st.type != NULL) ? st.type : "?");
            if (t[0] >= 'a' && t[0] <= 'z') { t[0] = (char)(t[0] - 32); }
            set_text(add_label(cx[2] + GAP, y, name_w, 14, (const leFont *)&DejaVuSansMono_12,
                               nsc, LE_HALIGN_LEFT), t);

            add_rect(track_x, y + 3, track_w, EBAR_H, &SCHEME_FILL_ZINC_800, SHAPE_PILL);
            s_ebar[r]  = add_rect(track_x, y + 3, 1, EBAR_H, nsc, SHAPE_PILL);
            s_etot[r]  = add_label(track_x + track_w + 6, y, tot_w, 14,
                                   (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_400,
                                   LE_HALIGN_RIGHT);
            s_esplit[r] = add_label(track_x + track_w + tot_w + 10, y, split_w, 14,
                                    (const leFont *)&DejaVuSansMono_9, &SCHEME_TEXT_ZINC_600,
                                    LE_HALIGN_LEFT);
        }
        s_etrack_w = track_w;
    }


    refresh_all();   /* seed values (render tasks suspended during boot Setup) */

    (void)xTaskCreateStatic(bus_task, "BusStats",
                            (uint32_t)(sizeof s_task_stack / sizeof s_task_stack[0]),
                            NULL, 2u, s_task_stack, &s_task_tcb);
}

/* Read the telemetry and rewrite all dynamic labels + threshold schemes. Caller
 * holds the render lock (or is at boot with the render tasks suspended). */
static void refresh_all(void)
{
    char tmp[CAP];

    if (s_sim) { sim_advance(); }

    T1SLink_BusStats bs;
    if (bus_stats(&bs))
    {
        (void)snprintf(tmp, sizeof tmp, "%lu%%", (unsigned long)(bs.util_permille / 10u));
        set_text(s_kpi_util, tmp);
        s_kpi_util->fn->setScheme(s_kpi_util,
            (bs.util_permille > 700u) ? &SCHEME_TEXT_RED_400 :
            (bs.util_permille > 450u) ? &SCHEME_TEXT_YELLOW_400 : &SCHEME_TEXT_CYAN_400);

        /* Gauge sweep + history sample. The gauge/sparkline read colours from a
         * scheme's BASE, so these are the mono colour-carrier schemes (NODE_MARVIN is
         * cyan #22D3EE, NODE_LIGHTSHOW is red #F87171 — both exactly the mockup's). */
        Gauge_Set(bs.util_permille,
                  (bs.util_permille > 700u) ? &SCHEME_NODE_LIGHTSHOW :
                  (bs.util_permille > 450u) ? &SCHEME_FILL_YELLOW_400 : &SCHEME_NODE_MARVIN);
        Sparkline_Push(bs.util_permille);

        fmt_count(bs.tx_total, tmp, sizeof tmp); set_text(s_kpi_tx, tmp);
        fmt_count(bs.rx_total, tmp, sizeof tmp); set_text(s_kpi_rx, tmp);

        (void)snprintf(tmp, sizeof tmp, "%lu", (unsigned long)bs.crc_total);
        set_text(s_kpi_crc, tmp);
        s_kpi_crc->fn->setScheme(s_kpi_crc, (bs.crc_total > 50u) ? &SCHEME_TEXT_RED_400
                                                                 : &SCHEME_TEXT_YELLOW_400);
        (void)snprintf(tmp, sizeof tmp, "%lu", (unsigned long)bs.sym_total);
        set_text(s_kpi_sym, tmp);
        s_kpi_sym->fn->setScheme(s_kpi_sym, (bs.sym_total > 20u) ? &SCHEME_TEXT_RED_400
                                                                 : &SCHEME_TEXT_YELLOW_400);

        (void)snprintf(tmp, sizeof tmp, "%lu.%04lu%%",
                       (unsigned long)(bs.err_rate_ppm / 10000u),
                       (unsigned long)(bs.err_rate_ppm % 10000u));
        set_text(s_kpi_err, tmp);

        (void)snprintf(tmp, sizeof tmp, "%u / %u",
                       (unsigned)bs.nodes_online, (unsigned)bs.nodes_total);
        set_text(s_kpi_nodes, tmp);

        unsigned s = bs.uptime_s;
        (void)snprintf(tmp, sizeof tmp, "%u:%02u:%02u", s / 3600u, (s % 3600u) / 60u, s % 60u);
        set_text(s_kpi_up, tmp);

        /* Never let the screen imply these are live numbers. */
        set_text(s_kpi_upsub, s_sim ? "SIMULATED" : "10BASE-T1S");
        s_kpi_upsub->fn->setScheme(s_kpi_upsub, s_sim ? &SCHEME_TEXT_YELLOW_400
                                                      : &SCHEME_TEXT_ZINC_500);
    }

    /* TOTAL TX's green sub-line = aggregate TX rate (marvin + every node). */
    {
        uint32_t txr = 0u;
        T1SLink_NodeStats s2;
        if (bus_self(&s2)) { txr += s2.tx_rate; }
        for (uint8_t i = 0; i < bus_node_count(); i++)
        {
            if (bus_node(i, &s2)) { txr += s2.tx_rate; }
        }
        fmt_rate(txr, tmp, sizeof tmp);
        set_text(s_kpi_txr, tmp);
    }

    for (uint8_t r = 0; r < MAX_ROWS; r++)
    {
        row_t *w = &s_row[r];
        if (!w->used) { continue; }
        T1SLink_NodeStats st;
        if (!((r == 0u) ? bus_self(&st)
                        : bus_node((uint8_t)(r - 1u), &st)))
        {
            continue;
        }

        fmt_count(st.tx_count, tmp, sizeof tmp); set_text(w->txtot, tmp);
        fmt_count(st.rx_count, tmp, sizeof tmp); set_text(w->rxtot, tmp);
        fmt_rate(st.tx_rate, tmp, sizeof tmp);   set_text(w->txr, tmp);
        fmt_rate(st.rx_rate, tmp, sizeof tmp);   set_text(w->rxr, tmp);

        (void)snprintf(tmp, sizeof tmp, "%u", (unsigned)st.crc_err); set_text(w->crc, tmp);
        w->crc->fn->setScheme(w->crc, crc_scheme(st.crc_err));
        (void)snprintf(tmp, sizeof tmp, "%u", (unsigned)st.sym_err); set_text(w->sym, tmp);
        w->sym->fn->setScheme(w->sym, sym_scheme(st.sym_err));

        if (r == 0u) { set_text(w->hbage, "-"); }
        else
        {
            (void)snprintf(tmp, sizeof tmp, "%lu ms", (unsigned long)st.age_ms);
            set_text(w->hbage, tmp);
        }

        bool online = (r == 0u) ? true : st.present;
        set_text(w->status, online ? "ONLINE" : "OFFLINE");
        w->status->fn->setScheme(w->status, online ? &SCHEME_TEXT_GREEN_400
                                                   : &SCHEME_TEXT_RED_400);
        w->statusdot->fn->setScheme(w->statusdot, online ? &SCHEME_FILL_GREEN_400
                                                         : &SCHEME_FILL_ZINC_600);
    }

    /* ── charts: rescale the bars against the current maxima ─────────────── */
    {
        uint32_t max_rate = 1u, max_err = 1u;
        T1SLink_NodeStats st;
        for (uint8_t r = 0; r < MAX_ROWS; r++)
        {
            if (!s_row[r].used) { continue; }
            if (!((r == 0u) ? bus_self(&st)
                            : bus_node((uint8_t)(r - 1u), &st))) { continue; }
            if (st.tx_rate > max_rate) { max_rate = st.tx_rate; }
            uint32_t e = (uint32_t)st.crc_err + st.sym_err;
            if (e > max_err) { max_err = e; }
        }

        for (uint8_t r = 0; r < MAX_ROWS; r++)
        {
            if (!s_row[r].used) { continue; }
            if (!((r == 0u) ? bus_self(&st)
                            : bus_node((uint8_t)(r - 1u), &st))) { continue; }

            /* TX-rate bar: bottom-anchored, so both height and y move. */
            if (s_bar[r] != NULL)
            {
                int h = (int)(((uint64_t)st.tx_rate * (uint32_t)(PLOT_H - 2)) / max_rate);
                if (h < 1) { h = 1; }
                s_bar[r]->fn->setSize(s_bar[r], s_bar[r]->fn->getWidth(s_bar[r]), (uint32_t)h);
                s_bar[r]->fn->setPosition(s_bar[r], s_bar[r]->rect.x, PLOT_Y + PLOT_H - h);
            }

            /* Error bar: width only, plus the total and the CRC/SYM split. A pill
             * narrower than it is tall reads as a sliver rather than a rounded end, so
             * any non-zero count is at least one full end-cap wide; zero stays below
             * the pill paint's minimum and so draws nothing, leaving a bare track. */
            uint32_t e = (uint32_t)st.crc_err + st.sym_err;
            if (s_ebar[r] != NULL)
            {
                int w = (int)(((uint64_t)e * (uint32_t)s_etrack_w) / max_err);
                if (e != 0u && w < EBAR_H) { w = EBAR_H; }
                if (w < 1) { w = 1; }
                s_ebar[r]->fn->setSize(s_ebar[r], (uint32_t)w, (uint32_t)EBAR_H);
            }
            (void)snprintf(tmp, sizeof tmp, "%lu", (unsigned long)e);
            set_text(s_etot[r], tmp);
            (void)snprintf(tmp, sizeof tmp, "(%u CRC / %u SYM)",
                           (unsigned)st.crc_err, (unsigned)st.sym_err);
            set_text(s_esplit[r], tmp);
        }
    }

    /* Transparent labels don't repaint their backdrop on invalidate, and the bars
     * just moved, so repaint the whole panel once to erase old glyphs/geometry and
     * redraw every cell + plot. */
    Marvin_PANEL_BUS->fn->invalidate(Marvin_PANEL_BUS);
}

/* ~1 Hz refresh, only while the bus view is the shown base view. */
static void bus_task(void *param)
{
    (void)param;
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!s_shown) { continue; }
        UiManager_RenderLock();
        refresh_all();
        UiManager_RenderUnlock();
    }
}

void ScreenBus_SetInput(bool on)
{
    if (on) { Marvin_PANEL_BUS->flags |=  LE_WIDGET_ENABLED; }
    else    { Marvin_PANEL_BUS->flags &= ~LE_WIDGET_ENABLED; }
}

void ScreenBus_SetShown(bool shown)
{
    s_shown = shown;
}

void ScreenBus_SetSimulated(bool on)
{
    s_sim = on;
}

bool ScreenBus_Simulated(void)
{
    return s_sim;
}
