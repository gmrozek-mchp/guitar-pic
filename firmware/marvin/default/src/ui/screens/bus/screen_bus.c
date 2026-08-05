#include "ui/screens/bus/screen_bus.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "ui/ui_manager.h"   /* CANVAS_BUS, BASE_W, BASE_H, RenderLock/Unlock */
#include "ui/titlebar.h"     /* shared hamburger + logos titlebar */
#include "ui/widgets/panel_aa/widget_panel_aa.h"
#include "net/t1s/t1s_link.h"

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
 * Still to come (mockup has them, we don't yet): the semicircle utilization gauge
 * (shown as a threshold-coloured % for now) and the three chart bodies — their
 * cards + titles are laid out so adding the drawing is self-contained. */

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

#define DOT_NODE   8           /* w-2  */
#define DOT_STAT   6           /* w-1.5 */
#define BADGE_W    52
#define BADGE_H    20

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
#define LBL_MAX  160
#define WGT_MAX   64           /* 12 cards + 14 dots + 7 rules + 7 badge fills */

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

static leLabelWidget *add_label(int x, int y, int w, int h, const leFont *font,
                                const leScheme *scheme, leHAlignment ha)
{
    configASSERT(s_nlbl < LBL_MAX);
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

/* A filled AA circle. Never sets cornerRadius: Legato's stock rounded-rect paint
 * hangs once the radius reaches half the widget size (see PanelAA_EnableDot). */
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

/* ── widget handles for the refresh ─────────────────────────────────────────*/
static leLabelWidget *s_kpi_util, *s_kpi_tx, *s_kpi_txr, *s_kpi_rx,
                     *s_kpi_crc, *s_kpi_sym, *s_kpi_err, *s_kpi_nodes, *s_kpi_up;

typedef struct {
    bool           used;
    leLabelWidget *txtot, *rxtot, *txr, *rxr, *crc, *sym, *hbage, *status;
    leWidget      *statusdot;
} row_t;
static row_t s_row[MAX_ROWS];

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
    gfxcSetPixelBuffer(CANVAS_BUS, BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565, s_fb);
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

    /* ── KPI row: the wide utilization card, then seven equal tiles ───────── */
    add_card(CONTENT_X, KPI_Y, GAUGE_W, KPI_H);
    s_kpi_util = add_label(CONTENT_X + PAD, KPI_Y + 30, 88, 28,
                           (const leFont *)&DejaVuSansMonoBold_24, &SCHEME_TEXT_CYAN_400,
                           LE_HALIGN_LEFT);
    set_text(add_label(CONTENT_X + 112, KPI_Y + 26, 76, 16, (const leFont *)&DejaVuSansMono_12,
                       &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT), "BUS");
    set_text(add_label(CONTENT_X + 112, KPI_Y + 44, 76, 16, (const leFont *)&DejaVuSansMono_12,
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
                      NULL, "10BASE-T1S", NULL);

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
    uint8_t nfollow = T1SLink_NodeTableCount();
    uint8_t nrows   = (uint8_t)(1u + nfollow);
    if (nrows > MAX_ROWS) { nrows = MAX_ROWS; }

    int row0_y = TBL_Y + TBL_HDR_H + 1;
    for (uint8_t r = 0; r < nrows; r++)
    {
        int  y    = row0_y + r * ROW_H;
        bool self = (r == 0u);
        T1SLink_NodeStats st;
        if (!(self ? T1SLink_GetSelfStats(&st) : T1SLink_GetNodeStats((uint8_t)(r - 1u), &st)))
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

    /* ── chart row: three titled cards; the plots themselves come later ───── */
    static const char *CHART_TITLE[3] = {
        "BUS UTILIZATION HISTORY", "TX RATE BY NODE", "ERROR COUNT BY NODE",
    };
    for (int i = 0; i < 3; i++)
    {
        int cx = CONTENT_X + i * (CHART_W + GAP);
        add_card(cx, CHART_Y, CHART_W, CHART_H);
        set_text(add_label(cx + GAP, CHART_Y + GAP, CHART_W - 2 * GAP, 16,
                           (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500,
                           LE_HALIGN_LEFT), CHART_TITLE[i]);
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

    T1SLink_BusStats bs;
    if (T1SLink_GetBusStats(&bs))
    {
        (void)snprintf(tmp, sizeof tmp, "%lu%%", (unsigned long)(bs.util_permille / 10u));
        set_text(s_kpi_util, tmp);
        s_kpi_util->fn->setScheme(s_kpi_util,
            (bs.util_permille > 700u) ? &SCHEME_TEXT_RED_400 :
            (bs.util_permille > 450u) ? &SCHEME_TEXT_YELLOW_400 : &SCHEME_TEXT_CYAN_400);

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
    }

    /* TOTAL TX's green sub-line = aggregate TX rate (marvin + every node). */
    {
        uint32_t txr = 0u;
        T1SLink_NodeStats s2;
        if (T1SLink_GetSelfStats(&s2)) { txr += s2.tx_rate; }
        for (uint8_t i = 0; i < T1SLink_NodeTableCount(); i++)
        {
            if (T1SLink_GetNodeStats(i, &s2)) { txr += s2.tx_rate; }
        }
        fmt_rate(txr, tmp, sizeof tmp);
        set_text(s_kpi_txr, tmp);
    }

    for (uint8_t r = 0; r < MAX_ROWS; r++)
    {
        row_t *w = &s_row[r];
        if (!w->used) { continue; }
        T1SLink_NodeStats st;
        if (!((r == 0u) ? T1SLink_GetSelfStats(&st)
                        : T1SLink_GetNodeStats((uint8_t)(r - 1u), &st)))
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

    /* Transparent labels don't repaint their backdrop on invalidate, so repaint the
     * whole panel once to erase old glyphs and redraw every cell. */
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
