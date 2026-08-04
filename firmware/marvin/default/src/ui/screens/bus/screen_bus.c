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

/* 10BASE-T1S bus statistics — KPI tiles + per-node table, built programmatically
 * into the MGS layer-6 panel (Marvin_PANEL_BUS). Backed by the T1SLink telemetry
 * API. Refreshed ~1 Hz only while shown. The bus-utilization sparkline and the
 * TX/error bar charts from the mockup are a later addition. */

#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))
static uint16_t FB_NOCACHE s_fb[BASE_W * BASE_H];

/* ── layout ─────────────────────────────────────────────────────────────────*/
#define MARGIN     16
#define CARD_R     6

/* Content sits below the shared titlebar (occupies the top ~65 px). */
#define KPI_Y      76
#define KPI_H      104
#define KPI_N      8
#define KPI_GAP    8
#define KPI_W      ((BASE_W - 2 * MARGIN - (KPI_N - 1) * KPI_GAP) / KPI_N)   /* 149 */

#define TBL_HDR_Y  200
#define TBL_ROW0_Y 232
#define ROW_H      44
#define MAX_ROWS   10
#define DOT        12

/* Column x / width (left-aligned monospace), sum < BASE_W - MARGIN. */
enum { C_NODE, C_ADDR, C_ROLE, C_TXTOT, C_RXTOT, C_TXR, C_RXR, C_CRC, C_SYM, C_LAT, C_STATUS, C_COUNT };
static const struct { const char *hdr; int x, w; } COL[C_COUNT] = {
    { "NODE",     16,  176 }, { "ADDR",    192, 64  }, { "ROLE",    256, 88  },
    { "TX TOTAL", 344, 128 }, { "RX TOTAL",472, 128 }, { "TX RATE", 600, 108 },
    { "RX RATE",  708, 108 }, { "CRC ERR", 816, 80  }, { "SYM ERR", 896, 80  },
    { "LATENCY",  976, 104 }, { "STATUS",  1080,150 },
};

/* ── static widget storage ──────────────────────────────────────────────────
 * Widgets live in BSS (no Legato pool / LE_MALLOC): leLabelWidget_Constructor /
 * leWidget_Constructor build them in place, exactly as leLabelWidget_New would
 * after LE_MALLOC. The screen's widget set is fixed, so the pools are sized to it
 * (configASSERT catches undersizing at bring-up). Labels are transparent; refresh
 * rewrites the string then invalidates the whole panel so the opaque card backdrop
 * repaints under them. */
#define CAP       16
#define LBL_MAX   160    /* title + 8 tiles(cap+val+sub) + 11 headers + rows*11 */
#define WGT_MAX   48     /* 8 cards + per-row node dot + status dot */

static leChar        s_buf[LBL_MAX][CAP];
static leFixedString s_fs[LBL_MAX];
static leLabelWidget s_lbl[LBL_MAX];
static unsigned      s_nlbl;
static leWidget      s_wgt[WGT_MAX];
static unsigned      s_nwgt;

static leLabelWidget *add_label(leWidget *parent, int x, int y, int w, int h,
                                const leFont *font, const leScheme *scheme, leHAlignment ha)
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

    parent->fn->addChild(parent, (leWidget *)l);
    return l;
}

static void set_text(leLabelWidget *l, const char *s)
{
    if (l != NULL) { (void)lestring_set_utf8(l->fn->getString(l), s); }
}

static leWidget *add_card(int x, int y, int w, int h)
{
    configASSERT(s_nwgt < WGT_MAX);
    leWidget *p = &s_wgt[s_nwgt++];
    leWidget_Constructor(p);
    p->fn->setPosition(p, x, y);
    p->fn->setSize(p, w, h);
    p->fn->setScheme(p, &SCHEME_FILL_ZINC_900);
    p->fn->setBackgroundType(p, LE_WIDGET_BACKGROUND_FILL);
    p->fn->setCornerRadius(p, CARD_R);
    PanelAA_EnableRoundImage(p);
    Marvin_PANEL_BUS->fn->addChild(Marvin_PANEL_BUS, p);
    return p;
}

static leWidget *add_dot(int x, int y, int d, const leScheme *scheme)
{
    configASSERT(s_nwgt < WGT_MAX);
    leWidget *p = &s_wgt[s_nwgt++];
    leWidget_Constructor(p);
    p->fn->setPosition(p, x, y);
    p->fn->setSize(p, d, d);
    p->fn->setScheme(p, scheme);
    p->fn->setBackgroundType(p, LE_WIDGET_BACKGROUND_FILL);
    PanelAA_EnableDot(p);   /* square fill + AA circle (no cornerRadius: stock rounding hangs at r==size/2) */
    Marvin_PANEL_BUS->fn->addChild(Marvin_PANEL_BUS, p);
    return p;
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
    leLabelWidget *txtot, *rxtot, *txr, *rxr, *crc, *sym, *lat, *status;
    leWidget      *statusdot;
} row_t;
static row_t s_row[MAX_ROWS];

static volatile bool s_shown;
static StackType_t   s_task_stack[1024];
static StaticTask_t  s_task_tcb;

/* ── one KPI tile: card + caption + value (+ optional sub) ───────────────────*/
static leLabelWidget *kpi(int idx, const char *caption, const leFont *vfont,
                          const leScheme *vscheme, leLabelWidget **sub_out, const char *sub_static)
{
    int x = MARGIN + idx * (KPI_W + KPI_GAP);
    add_card(x, KPI_Y, KPI_W, KPI_H);
    leLabelWidget *cap = add_label(Marvin_PANEL_BUS, x + 12, KPI_Y + 12, KPI_W - 24, 18,
                                   (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT);
    set_text(cap, caption);
    leLabelWidget *val = add_label(Marvin_PANEL_BUS, x + 12, KPI_Y + 36, KPI_W - 24, 32,
                                   vfont, vscheme, LE_HALIGN_LEFT);
    if (sub_out != NULL) {
        *sub_out = add_label(Marvin_PANEL_BUS, x + 12, KPI_Y + 74, KPI_W - 24, 16,
                             (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT);
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

    /* Shared titlebar (hamburger + logos), same as the dashboard/wiimotes chrome. */
    Titlebar_Add(Marvin_PANEL_BUS);

    ScreenBus_SetInput(false);

    /* KPI tiles. */
    s_kpi_util  = kpi(0, "BUS UTIL",   (const leFont *)&DejaVuSansMonoBold_24, &SCHEME_TEXT_CYAN_400,  NULL, NULL);
    s_kpi_tx    = kpi(1, "TOTAL TX",   (const leFont *)&DejaVuSansMonoBold_24, &SCHEME_TEXT_ZINC_200,  &s_kpi_txr, NULL);
    s_kpi_rx    = kpi(2, "TOTAL RX",   (const leFont *)&DejaVuSansMonoBold_24, &SCHEME_TEXT_ZINC_200,  NULL, NULL);
    s_kpi_crc   = kpi(3, "CRC ERRORS", (const leFont *)&DejaVuSansMonoBold_24, &SCHEME_TEXT_YELLOW_400,NULL, NULL);
    s_kpi_sym   = kpi(4, "SYMBOL ERR", (const leFont *)&DejaVuSansMonoBold_24, &SCHEME_TEXT_YELLOW_400,NULL, NULL);
    s_kpi_err   = kpi(5, "ERROR RATE", (const leFont *)&DejaVuSansMonoBold_24, &SCHEME_TEXT_ZINC_200,  NULL, NULL);
    s_kpi_nodes = kpi(6, "NODES",      (const leFont *)&DejaVuSansMonoBold_24, &SCHEME_TEXT_GREEN_400, NULL, "ONLINE");
    s_kpi_up    = kpi(7, "UPTIME",     (const leFont *)&DejaVuSansMonoBold_24, &SCHEME_TEXT_VIOLET_400,NULL, "10BASE-T1S");
    s_kpi_txr->fn->setScheme(s_kpi_txr, &SCHEME_TEXT_GREEN_400);   /* TX-rate sub is green */

    /* Table header. */
    for (int c = 0; c < C_COUNT; c++) {
        set_text(add_label(Marvin_PANEL_BUS, COL[c].x, TBL_HDR_Y, COL[c].w, 20,
                           (const leFont *)&DejaVuSansMono_14, &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT), COL[c].hdr);
    }

    /* Table rows: row 0 = marvin (self), rows 1.. = follower node table. */
    uint8_t nfollow = T1SLink_NodeTableCount();
    uint8_t nrows   = (uint8_t)(1u + nfollow);
    if (nrows > MAX_ROWS) { nrows = MAX_ROWS; }

    for (uint8_t r = 0; r < nrows; r++) {
        int y = TBL_ROW0_Y + r * ROW_H;
        bool self = (r == 0u);
        T1SLink_NodeStats st;
        bool ok = self ? T1SLink_GetSelfStats(&st) : T1SLink_GetNodeStats((uint8_t)(r - 1u), &st);
        if (!ok) { continue; }

        const leScheme *nsc = self ? &SCHEME_NODE_MARVIN : node_scheme(st.type);
        char tmp[CAP];

        add_dot(COL[C_NODE].x, y + (ROW_H - DOT) / 2, DOT, nsc);
        leLabelWidget *nm = add_label(Marvin_PANEL_BUS, COL[C_NODE].x + DOT + 8, y, COL[C_NODE].w - DOT - 8, ROW_H,
                                      (const leFont *)&DejaVuSansMonoBold_18, nsc, LE_HALIGN_LEFT);
        /* capitalize first letter for the label */
        (void)snprintf(tmp, sizeof tmp, "%s", (st.type != NULL) ? st.type : "?");
        if (tmp[0] >= 'a' && tmp[0] <= 'z') { tmp[0] = (char)(tmp[0] - 32); }
        set_text(nm, tmp);

        (void)snprintf(tmp, sizeof tmp, "0x%02X", (unsigned)st.node_id);
        set_text(add_label(Marvin_PANEL_BUS, COL[C_ADDR].x, y, COL[C_ADDR].w, ROW_H,
                          (const leFont *)&DejaVuSansMono_14, &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT), tmp);

        set_text(add_label(Marvin_PANEL_BUS, COL[C_ROLE].x, y, COL[C_ROLE].w, ROW_H,
                          (const leFont *)&DejaVuSansMonoBold_14, self ? &SCHEME_TEXT_CYAN_400 : &SCHEME_TEXT_ZINC_500,
                          LE_HALIGN_LEFT), self ? "COORD" : "NODE");

        row_t *w = &s_row[r];
        w->used   = true;
        w->txtot  = add_label(Marvin_PANEL_BUS, COL[C_TXTOT].x, y, COL[C_TXTOT].w, ROW_H, (const leFont *)&DejaVuSansMono_16, &SCHEME_TEXT_ZINC_200, LE_HALIGN_LEFT);
        w->rxtot  = add_label(Marvin_PANEL_BUS, COL[C_RXTOT].x, y, COL[C_RXTOT].w, ROW_H, (const leFont *)&DejaVuSansMono_16, &SCHEME_TEXT_ZINC_200, LE_HALIGN_LEFT);
        w->txr    = add_label(Marvin_PANEL_BUS, COL[C_TXR].x,   y, COL[C_TXR].w,   ROW_H, (const leFont *)&DejaVuSansMono_16, &SCHEME_TEXT_GREEN_400, LE_HALIGN_LEFT);
        w->rxr    = add_label(Marvin_PANEL_BUS, COL[C_RXR].x,   y, COL[C_RXR].w,   ROW_H, (const leFont *)&DejaVuSansMono_16, &SCHEME_TEXT_BLUE_400, LE_HALIGN_LEFT);
        w->crc    = add_label(Marvin_PANEL_BUS, COL[C_CRC].x,   y, COL[C_CRC].w,   ROW_H, (const leFont *)&DejaVuSansMono_16, &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT);
        w->sym    = add_label(Marvin_PANEL_BUS, COL[C_SYM].x,   y, COL[C_SYM].w,   ROW_H, (const leFont *)&DejaVuSansMono_16, &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT);
        w->lat    = add_label(Marvin_PANEL_BUS, COL[C_LAT].x,   y, COL[C_LAT].w,   ROW_H, (const leFont *)&DejaVuSansMono_16, &SCHEME_TEXT_ZINC_400, LE_HALIGN_LEFT);
        w->statusdot = add_dot(COL[C_STATUS].x, y + (ROW_H - 8) / 2, 8, &SCHEME_FILL_GREEN_400);
        w->status = add_label(Marvin_PANEL_BUS, COL[C_STATUS].x + 14, y, COL[C_STATUS].w - 14, ROW_H, (const leFont *)&DejaVuSansMono_14, &SCHEME_TEXT_GREEN_400, LE_HALIGN_LEFT);
    }

    refresh_all();   /* seed values (render tasks suspended during boot Setup) */

    (void)xTaskCreateStatic(bus_task, "BusStats", (uint32_t)(sizeof s_task_stack / sizeof s_task_stack[0]),
                            NULL, 2u, s_task_stack, &s_task_tcb);
}

/* Read the telemetry and rewrite all dynamic labels + threshold schemes. Caller
 * holds the render lock (or is at boot with the render tasks suspended). */
static void refresh_all(void)
{
    char tmp[CAP];

    T1SLink_BusStats bs;
    if (T1SLink_GetBusStats(&bs)) {
        (void)snprintf(tmp, sizeof tmp, "%lu.%lu%%", (unsigned long)(bs.util_permille / 10u), (unsigned long)(bs.util_permille % 10u));
        set_text(s_kpi_util, tmp);
        s_kpi_util->fn->setScheme(s_kpi_util, (bs.util_permille > 700u) ? &SCHEME_TEXT_RED_400 : (bs.util_permille > 450u) ? &SCHEME_TEXT_YELLOW_400 : &SCHEME_TEXT_CYAN_400);

        fmt_count(bs.tx_total, tmp, sizeof tmp); set_text(s_kpi_tx, tmp);
        fmt_count(bs.rx_total, tmp, sizeof tmp); set_text(s_kpi_rx, tmp);

        (void)snprintf(tmp, sizeof tmp, "%lu", (unsigned long)bs.crc_total); set_text(s_kpi_crc, tmp);
        s_kpi_crc->fn->setScheme(s_kpi_crc, (bs.crc_total > 50u) ? &SCHEME_TEXT_RED_400 : &SCHEME_TEXT_YELLOW_400);
        (void)snprintf(tmp, sizeof tmp, "%lu", (unsigned long)bs.sym_total); set_text(s_kpi_sym, tmp);
        s_kpi_sym->fn->setScheme(s_kpi_sym, (bs.sym_total > 20u) ? &SCHEME_TEXT_RED_400 : &SCHEME_TEXT_YELLOW_400);

        (void)snprintf(tmp, sizeof tmp, "%lu.%04lu%%", (unsigned long)(bs.err_rate_ppm / 10000u), (unsigned long)(bs.err_rate_ppm % 10000u));
        set_text(s_kpi_err, tmp);

        (void)snprintf(tmp, sizeof tmp, "%u / %u", (unsigned)bs.nodes_online, (unsigned)bs.nodes_total);
        set_text(s_kpi_nodes, tmp);

        unsigned s = bs.uptime_s;
        (void)snprintf(tmp, sizeof tmp, "%u:%02u:%02u", s / 3600u, (s % 3600u) / 60u, s % 60u);
        set_text(s_kpi_up, tmp);
    }

    /* TOTAL TX tile's green sub = aggregate TX rate (sum over marvin + all nodes). */
    {
        uint32_t txr = 0u;
        T1SLink_NodeStats s2;
        if (T1SLink_GetSelfStats(&s2)) { txr += s2.tx_rate; }
        for (uint8_t i = 0; i < T1SLink_NodeTableCount(); i++) {
            if (T1SLink_GetNodeStats(i, &s2)) { txr += s2.tx_rate; }
        }
        fmt_rate(txr, tmp, sizeof tmp);
        set_text(s_kpi_txr, tmp);
    }

    for (uint8_t r = 0; r < MAX_ROWS; r++) {
        row_t *w = &s_row[r];
        if (!w->used) { continue; }
        T1SLink_NodeStats st;
        bool ok = (r == 0u) ? T1SLink_GetSelfStats(&st) : T1SLink_GetNodeStats((uint8_t)(r - 1u), &st);
        if (!ok) { continue; }

        fmt_count(st.tx_count, tmp, sizeof tmp); set_text(w->txtot, tmp);
        fmt_count(st.rx_count, tmp, sizeof tmp); set_text(w->rxtot, tmp);
        fmt_rate(st.tx_rate, tmp, sizeof tmp);   set_text(w->txr, tmp);
        fmt_rate(st.rx_rate, tmp, sizeof tmp);   set_text(w->rxr, tmp);

        (void)snprintf(tmp, sizeof tmp, "%u", (unsigned)st.crc_err); set_text(w->crc, tmp);
        w->crc->fn->setScheme(w->crc, crc_scheme(st.crc_err));
        (void)snprintf(tmp, sizeof tmp, "%u", (unsigned)st.sym_err); set_text(w->sym, tmp);
        w->sym->fn->setScheme(w->sym, sym_scheme(st.sym_err));

        if (r == 0u) { set_text(w->lat, "-"); }
        else { (void)snprintf(tmp, sizeof tmp, "%lu ms", (unsigned long)st.age_ms); set_text(w->lat, tmp); }

        bool online = (r == 0u) ? true : st.present;
        set_text(w->status, online ? "ONLINE" : "OFFLINE");
        w->status->fn->setScheme(w->status, online ? &SCHEME_TEXT_GREEN_400 : &SCHEME_TEXT_RED_400);
        w->statusdot->fn->setScheme(w->statusdot, online ? &SCHEME_FILL_GREEN_400 : &SCHEME_FILL_ZINC_600);
    }

    /* Transparent labels don't repaint their backdrop on invalidate, so repaint
     * the whole panel once to erase old glyphs and redraw all cells. */
    Marvin_PANEL_BUS->fn->invalidate(Marvin_PANEL_BUS);
}

/* ~1 Hz refresh, only while the bus view is the shown base view. */
static void bus_task(void *param)
{
    (void)param;
    for (;;) {
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
