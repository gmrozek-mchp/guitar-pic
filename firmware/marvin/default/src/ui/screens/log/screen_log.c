#include "ui/screens/log/screen_log.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "log_ring.h"
#include "ui/ui_manager.h"   /* CANVAS_LOG, BASE_W, BASE_H, RenderLock/Unlock */
#include "ui/titlebar.h"     /* shared hamburger + logos titlebar */
#include "ui/widgets/panel_aa/widget_panel_aa.h"
#include "ui/widgets/log_list/widget_log_list.h"

#include "definitions.h"     /* SYS_TIME_*, PLIB_GFX2D_* (probe) */
#include "ui/gfx/ui_surface.h"
#include "ui/gfx/render_probe.h"
#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/string/legato_fixedstring.h"
#include "gfx/legato/widget/legato_widget.h"
#include "gfx/legato/widget/label/legato_widget_label.h"
#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"                 /* DejaVu fonts */
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"    /* Marvin_PANEL_LOG */
#include "util/legato_utf8.h"

/* Activity log — one card holding a column heading strip and a scrollable log table,
 * built programmatically into the MGS layer-9 panel (Marvin_PANEL_LOG) and backed by
 * log_ring.
 *
 * Layout mirrors tools' LogsScreen.tsx mockup with Tailwind units resolved to pixels
 * (gap-3 = 12, rounded = 4, px-4 = 16, text-xs/sm = 12/14), minus its search / filter /
 * export controls. The mockup's separate stats footer is folded into the card header, so
 * the table gets the height instead. */

#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))
static uint16_t FB_NOCACHE s_fb[BASE_W * BASE_H];

/* How often new lines are picked up while the screen is shown. A log is bursty and read
 * at human speed, so this is a latency budget rather than a frame rate: a repaint of the
 * full-width list is the most expensive thing this screen does, and coalescing a boot
 * spew's worth of lines into one repaint is the point. */
#define POLL_MS   500u

/* ── layout ─────────────────────────────────────────────────────────────────
 * Content spans x 16..1264 below the shared titlebar (top ~65px). */
#define MARGIN      16
#define CARD_R       4          /* Tailwind `rounded` */
#define PAD         16          /* card px-4 */
#define CONTENT_X   MARGIN
#define CONTENT_W   (BASE_W - 2 * MARGIN)          /* 1248 */

#define CARD_Y      76
#define CARD_H      (BASE_H - MARGIN - CARD_Y)     /* 708 */

#define HDR_H       36                             /* title + counters row */
#define COLHDR_H    28                             /* column headings strip */
#define TABLE_Y      (CARD_Y + HDR_H + COLHDR_H)    /* 140 */
#define TABLE_H      (CARD_H - HDR_H - COLHDR_H)    /* 644 */

/* Row font and pitch. DejaVuSansMono_16 is 20px tall against _12's 16, and the widget
 * resolves its columns from the font's advance, so the cost of the larger text is message
 * characters rather than a broken layout — about 87 instead of 138. Checked against the
 * tree's 189 distinct log messages: 98.4% still fit whole (median 32 chars, p95 72).
 *
 * ROW_H keeps roughly the font's own height again as padding, as _12 with 36 did. 644/42
 * leaves 15 whole rows and a 14px sliver of the 16th, which is the "more below" cue. */
#define ROW_FONT    DejaVuSansMono_16
#define ROW_H       42

#define CAP         40          /* longest label string: the counters line */

/* ── static widget storage ──────────────────────────────────────────────────
 * Widgets live in BSS (no Legato pool / LE_MALLOC), constructed in place exactly as
 * leX_New would after LE_MALLOC — the screen_bus model. The widget set is fixed and
 * small: the card, the heading strip, four column captions, a title and a counters
 * readout, plus the list. */
#define LBL_MAX      8
#define WGT_MAX      4

static leChar        s_buf[LBL_MAX][CAP];
static leFixedString s_fs[LBL_MAX];
static leLabelWidget s_lbl[LBL_MAX];
static unsigned      s_nlbl;
static leWidget      s_wgt[WGT_MAX];
static unsigned      s_nwgt;

static leWidget      *s_titlebar;
static leWidget      *s_list;
static leLabelWidget *s_counters;

static volatile bool s_shown;

/* Last log_ring sequence number reflected on screen. The poll compares against
 * log_ring_seq() and does nothing at all when it has not moved, so an idle system costs
 * no frames on this screen. */
static uint32_t s_seen_seq;

static StackType_t  s_task_stack[512];
static StaticTask_t s_task_tcb;

/* ── source display names ───────────────────────────────────────────────────
 * log_ring splits "TAG: message" and keeps the tag verbatim; this maps the tags marvin
 * actually emits to something readable on a panel. Deliberately here rather than in
 * log_ring, so `log dump` over serial still shows the raw tags an engineer greps for.
 *
 * An unlisted tag is drawn as-is — a new subsystem gets a reasonable row on the day it is
 * written, and adding it here is a one-line improvement rather than a prerequisite. A line
 * with no tag draws the widget's "Unknown" placeholder.
 *
 * Longest display name is 11 chars, which is what sizes the widget's SOURCE column. */
static const struct { const char *tag, *name; } SOURCE[] = {
    { "TC358743",      "HDMI RX"     },
    { "SETTINGS",      "Settings"    },
    { "QSPI",          "QSPI"        },
    { "QSPI verify",   "QSPI Verify" },
    { "QSPI bench",    "QSPI Bench"  },
    { "ISC_Capture",   "ISC Capture" },
    { "VIDEO",         "Video"       },
    { "SPLASH",        "Splash"      },
    { "ART",           "Album Art"   },
    { "NODEART",       "Node Art"    },
    { "songsel",       "Song Select" },
    { "UI",            "UI"          },
    { "T1S",           "T1S"         },
    { "HM",            "Health"      },
    { "SUP",           "Supervisor"  },
    { "GUARD",         "Mem Guard"   },
    { "GC",            "Game Ctrl"   },
    { "GAME",          "Game"        },
    { "CAT",           "Catalog"     },
    { "RES",           "Results"     },
    { "STG",           "Storage"     },
    { "CON",           "Console"     },
    { "CV",            "CV Detector" },
    { "FBL",           "Fretboard"   },
    { "FX",            "Fauxmote"    },
    { "MC",            "Manual Ctrl" },
    { "TIMING",        "Timing"      },
    { "QR",            "QR Code"     },
    { "PerfLog",       "Perf Log"    },
    { "PERF",          "Perf Log"    },
    { "SINK",          "Perf Sink"   },
    { "freertos heap", "Heap"        },
};
#define SOURCE_COUNT  (sizeof SOURCE / sizeof SOURCE[0])

/* ── row provider ───────────────────────────────────────────────────────────
 * Called from the list's paint for visible rows only. The strings it returns must outlive
 * the call, so one entry's worth of scratch is enough — the widget draws each cell before
 * asking for the next row. */
static log_ring_entry_t s_row_entry;
static char             s_row_time[16];
static char             s_row_src[24];

static bool log_row(void *ctx, int index, loglist_row_t *out)
{
    (void)ctx;

    if (index < 0 || !log_ring_get((uint32_t)index, &s_row_entry)) { return false; }

    uint32_t ms  = s_row_entry.uptime_ms;
    uint32_t sec = ms / 1000u;
    (void)snprintf(s_row_time, sizeof s_row_time, "%02lu:%02lu:%02lu.%03lu",
                   (unsigned long)(sec / 3600u), (unsigned long)((sec / 60u) % 60u),
                   (unsigned long)(sec % 60u),   (unsigned long)(ms % 1000u));

    s_row_src[0] = '\0';
    if (s_row_entry.src_len > 0u)
    {
        uint32_t n = s_row_entry.src_len;
        if (n >= sizeof s_row_src) { n = sizeof s_row_src - 1u; }
        memcpy(s_row_src, s_row_entry.text, n);
        s_row_src[n] = '\0';

        for (uint32_t i = 0u; i < SOURCE_COUNT; i++)
        {
            if (strcmp(s_row_src, SOURCE[i].tag) == 0)
            {
                (void)snprintf(s_row_src, sizeof s_row_src, "%s", SOURCE[i].name);
                break;
            }
        }
    }

    out->time    = s_row_time;
    out->level   = s_row_entry.lvl;
    out->source  = s_row_src;
    out->message = &s_row_entry.text[s_row_entry.msg_off];
    return true;
}

/* ── builders (the screen_bus idiom) ────────────────────────────────────────── */

static leWidget *next_widget(void)
{
    configASSERT(s_nwgt < WGT_MAX);
    leWidget *p = &s_wgt[s_nwgt++];
    leWidget_Constructor(p);
    return p;
}

/* A label box shorter than its font clips a row off the text top and bottom; grow the box
 * to the font height and shift y by the same halved amount so the glyphs fit without
 * moving. Same helper and same reason as screen_bus. */
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

    Marvin_PANEL_LOG->fn->addChild(Marvin_PANEL_LOG, (leWidget *)l);
    return l;
}

static void set_text(leLabelWidget *l, const char *s)
{
    if (l != NULL) { (void)lestring_set_utf8(l->fn->getString(l), s); }
}

/* ── the counters line ──────────────────────────────────────────────────────
 * The mockup's three footer chips as one right-aligned line, so the table keeps the
 * height a footer row would have taken. */
static void refresh_counters(void)
{
    uint32_t err = 0u, warn = 0u, held = 0u;
    char t[CAP];

    log_ring_counts(&err, &warn, &held);
    (void)snprintf(t, sizeof t, "%lu lines - %lu err - %lu warn",
                   (unsigned long)held, (unsigned long)err, (unsigned long)warn);
    set_text(s_counters, t);
}

void ScreenLog_InitSurface(void)
{
    UiSurface_Set(CANVAS_LOG, BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565, s_fb);
}

void ScreenLog_SetInput(bool on)
{
    if (on) { Marvin_PANEL_LOG->flags |=  LE_WIDGET_ENABLED; }
    else    { Marvin_PANEL_LOG->flags &= ~LE_WIDGET_ENABLED; }
}

/* Pick up new lines, only while the log view is the shown base view. */
static void log_task(void *param)
{
    (void)param;
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        if (!s_shown) { continue; }

        uint32_t seq = log_ring_seq();
        if (seq == s_seen_seq) { continue; }

        /* A drag owns the list's scroll offset and repaints it per touch-move; adding a
         * repaint of our own underneath that would cost a frame and fight for the render
         * lock without changing what the operator can see. The lines are not lost — the
         * next poll after the finger lifts picks them up. */
        if (LogList_Dragging(s_list)) { continue; }

        s_seen_seq = seq;

        UiManager_RenderLock();
        LogList_SetCount(s_list, (int)log_ring_count());   /* invalidates the list */
        refresh_counters();
        UiManager_RenderUnlock();
    }
}

void ScreenLog_Setup(void)
{
    gfxcSetWindowPosition(CANVAS_LOG, 0, 0);
    gfxcSetWindowSize(CANVAS_LOG, BASE_W, BASE_H);
    Marvin_PANEL_LOG->fn->setBackgroundType(Marvin_PANEL_LOG, LE_WIDGET_BACKGROUND_FILL);

    /* Shared titlebar (hamburger + logos), same as the other base views. */
    s_titlebar = Titlebar_Add(Marvin_PANEL_LOG);
    ScreenLog_SetInput(false);

    /* The card: zinc-900 fill, 1px border, 4px AA-rounded corners. */
    {
        leWidget *card = next_widget();
        card->fn->setPosition(card, CONTENT_X, CARD_Y);
        card->fn->setSize(card, CONTENT_W, CARD_H);
        card->fn->setScheme(card, &SCHEME_FILL_ZINC_900);
        card->fn->setBackgroundType(card, LE_WIDGET_BACKGROUND_FILL);
        card->fn->setBorderType(card, LE_WIDGET_BORDER_LINE);
        card->fn->setCornerRadius(card, CARD_R);
        PanelAA_Enable(card);
        Marvin_PANEL_LOG->fn->addChild(Marvin_PANEL_LOG, card);
    }

    set_text(add_label(CONTENT_X + PAD, CARD_Y, 240, HDR_H,
                       (const leFont *)&DejaVuSansMonoBold_16, &SCHEME_TEXT_ZINC_300,
                       LE_HALIGN_LEFT), "ACTIVITY LOG");

    s_counters = add_label(CONTENT_X + CONTENT_W - PAD - 320, CARD_Y, 320, HDR_H,
                           (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500,
                           LE_HALIGN_RIGHT);

    /* Column headings strip: zinc-800, the mockup's sticky table head. */
    {
        leWidget *strip = next_widget();
        strip->fn->setPosition(strip, CONTENT_X + 1, CARD_Y + HDR_H);
        strip->fn->setSize(strip, CONTENT_W - 2, COLHDR_H);
        strip->fn->setScheme(strip, &SCHEME_FILL_ZINC_800);
        strip->fn->setBackgroundType(strip, LE_WIDGET_BACKGROUND_FILL);
        Marvin_PANEL_LOG->fn->addChild(Marvin_PANEL_LOG, strip);
    }

    /* The list. Created before its column headings so the headings can be positioned
     * from the widget's own column geometry — the two cannot drift apart. */
    s_list = LogList_New();
    configASSERT(s_list != NULL);
    s_list->fn->setPosition(s_list, CONTENT_X + 1, TABLE_Y);
    s_list->fn->setSize(s_list, CONTENT_W - 2, TABLE_H);
    s_list->fn->setBackgroundType(s_list, LE_WIDGET_BACKGROUND_FILL);
    LogList_SetFont(s_list, (const leFont *)&ROW_FONT);
    LogList_SetRowHeight(s_list, ROW_H);
    LogList_SetEmptyText(s_list, "No log entries yet");
    LogList_SetModel(s_list, (int)log_ring_count(), log_row, NULL);
    Marvin_PANEL_LOG->fn->addChild(Marvin_PANEL_LOG, s_list);

    {
        static const char *const HEADING[LOGLIST_COL_COUNT] = {
            "TIME", "LVL", "SOURCE", "MESSAGE"
        };

        for (int c = 0; c < (int)LOGLIST_COL_COUNT; c++)
        {
            int cx = 0, cw = 0;
            LogList_ColumnRect(s_list, (loglist_col_t)c, &cx, &cw);

            set_text(add_label(s_list->rect.x + cx, CARD_Y + HDR_H, cw, COLHDR_H,
                               (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500,
                               LE_HALIGN_LEFT), HEADING[c]);
        }
    }

    refresh_counters();   /* seed (render tasks are suspended during boot Setup) */

    (void)xTaskCreateStatic(log_task, "ActivityLog",
                            (uint32_t)(sizeof s_task_stack / sizeof s_task_stack[0]),
                            NULL, 2u, s_task_stack, &s_task_tcb);
}

void ScreenLog_SetShown(bool shown)
{
    s_shown = shown;
    Titlebar_SetShown(s_titlebar, shown);

    /* Coming on screen, catch up on everything logged while away — the poll skipped it
     * all, so the sequence number is stale by however long the operator was elsewhere. */
    if (shown && log_ring_seq() != s_seen_seq)
    {
        s_seen_seq = log_ring_seq();
        LogList_SetCount(s_list, (int)log_ring_count());
        refresh_counters();
    }
}

void ScreenLog_SetTextPath(text_path_t path)
{
    if (s_list == NULL) { return; }

    UiManager_RenderLock();
    LogList_SetTextPath(s_list, path);
    UiManager_RenderUnlock();
}

/* ── GFX2D throughput, on this screen's own surface ──────────────────────────
 * The number that decides whether blit-shifted scrolling is worth building.
 *
 * Today a scroll step repaints the whole list, because every row moves. The alternative is
 * to shift the already-painted pixels within the canvas with the 2D engine and repaint only
 * the one row's worth of newly-exposed band — but that trades ~66 ms of text rendering for
 * a full-area copy, so it only wins if the copy is cheap. `PLIB_GFX2D_Copy` on the list rect
 * is exactly that copy, and `PLIB_GFX2D_Fill` over the same rect is the reference: the fill
 * writes, the copy reads and writes, so the ratio between them is what the shift would cost.
 *
 * Measured here rather than in the abstract because the surface is uncached
 * strongly-ordered DDR and the LCDC is reading ~360 MB/s of it concurrently, so datasheet
 * throughput is not the answer.
 *
 * Both operations write into s_fb, which is on screen while this runs. The copy is a no-op
 * shift of the list onto itself, so it writes back the pixels already there; the fill uses
 * the list's own zinc-900 background, so the worst it looks like is an empty list for the
 * moment before the closing invalidate repaints the rows.
 *
 * `full_us` is the whole-list repaint just measured by the caller, so the verdict line
 * compares against a real number rather than a remembered one. */
static void probe_gfx2d(uint32_t full_us, log_probe_fn out, void *ctx)
{
    GFX2D_BUFFER    buf;
    GFX2D_RECTANGLE rect;
    char            line[96];

    const uint32_t hz = SYS_TIME_FrequencyGet();
    if (hz == 0u || s_list == NULL) { return; }

    buf.width  = BASE_W;
    buf.height = BASE_H;
    buf.format = GFX2D_RGB16;   /* RGB565, per gfx2dFormats[] in drv_gfx2d.c */
    buf.dir    = GFX2D_XY00;
    buf.addr   = (uint32_t)(uintptr_t)s_fb;

    rect.x      = (uint32_t)s_list->rect.x;
    rect.y      = (uint32_t)s_list->rect.y;
    rect.width  = (uint32_t)s_list->rect.width;
    rect.height = (uint32_t)s_list->rect.height;

    const uint32_t px   = rect.width * rect.height;
    const uint32_t rows = rect.height / (uint32_t)ROW_H;

    /* The engine is asynchronous — DRV_GFX2D_Fill spins on GetGlobalStatusBusy after
     * programming it. Timing without that spin would measure the register writes and
     * nothing else. */
    UiManager_RenderLock();

    uint64_t t0 = SYS_TIME_Counter64Get();
    (void)PLIB_GFX2D_Copy(&buf, &rect, &buf, &rect);
    while (PLIB_GFX2D_GetGlobalStatusBusy() == true) { }
    uint32_t copy_us = (uint32_t)(((SYS_TIME_Counter64Get() - t0) * 1000000u) / hz);

    t0 = SYS_TIME_Counter64Get();
    (void)PLIB_GFX2D_Fill(&buf, &rect, 0xFF18181Bu);   /* ARGB8888 zinc-900 */
    while (PLIB_GFX2D_GetGlobalStatusBusy() == true) { }
    uint32_t fill_us = (uint32_t)(((SYS_TIME_Counter64Get() - t0) * 1000000u) / hz);

    UiManager_RenderUnlock();

    (void)snprintf(line, sizeof line, "  gfx2d fill %6lu px = %6lu us (%4lu ns/px, w)",
                   (unsigned long)px, (unsigned long)fill_us,
                   (unsigned long)(fill_us * 1000u / px));
    out(ctx, line);
    (void)snprintf(line, sizeof line, "  gfx2d copy %6lu px = %6lu us (%4lu ns/px, r+w)",
                   (unsigned long)px, (unsigned long)copy_us,
                   (unsigned long)(copy_us * 1000u / px));
    out(ctx, line);

    /* What a blit-shifted scroll step would cost: the full-area copy, plus one row's worth
     * of the repaint it replaces. Spelled out so the verdict needs no arithmetic at the
     * terminal — under ~half of full_us means the work is worth doing. */
    if (rows > 0u && full_us > 0u)
    {
        uint32_t shifted = copy_us + (full_us / rows);
        (void)snprintf(line, sizeof line,
                       "  => shift+1row ~%lu us vs %lu us full  (%lu.%01lux)",
                       (unsigned long)shifted, (unsigned long)full_us,
                       (unsigned long)(full_us / shifted),
                       (unsigned long)((full_us * 10u / shifted) % 10u));
        out(ctx, line);
    }

    s_list->fn->invalidate(s_list);   /* undo the fill */
}

/* ── render probe ────────────────────────────────────────────────────────────
 * The list is the whole cost of this screen: it has no partial-damage scheme, so a new
 * line or a drag step repaints all of it. See ui/gfx/render_probe.h for how to read the
 * numbers — subtract a contentless frame of the same rect (Titlebar_ProbeFrameUs) to
 * separate the text rendering from the parent's fill. */
void ScreenLog_Probe(unsigned iters, log_probe_fn out, void *ctx)
{
    if (out == NULL) { return; }

    if (UiManager_BaseCanvas() != CANVAS_LOG)
    {
        out(ctx, "log: not the shown base view (nav to it first)");
        return;
    }
    if (iters == 0u || iters > 200u) { iters = 8u; }

    char line[96];

    uint32_t list_us = RenderProbe_WidgetUs(s_list, iters);
    (void)snprintf(line, sizeof line, "  %-6s %4dx%-3d (%6d px) = %6lu us   %d rows",
                   "list", (int)s_list->rect.width, (int)s_list->rect.height,
                   (int)(s_list->rect.width * s_list->rect.height), (unsigned long)list_us,
                   LogList_VisibleRows(s_list));
    out(ctx, line);

    uint32_t panel_us = RenderProbe_WidgetUs(Marvin_PANEL_LOG, (iters > 4u) ? 4u : iters);
    (void)snprintf(line, sizeof line, "  %-6s %4dx%-3d (%6d px) = %6lu us",
                   "PANEL", (int)BASE_W, (int)BASE_H, (int)(BASE_W * BASE_H),
                   (unsigned long)panel_us);
    out(ctx, line);

    probe_gfx2d(list_us, out, ctx);
}
