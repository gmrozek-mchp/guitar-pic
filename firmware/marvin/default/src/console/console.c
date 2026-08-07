#include "console.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "definitions.h"
#include "log.h"
#include "embedded_cli.h"

#include "game/game_timing.h"
#include "actuator/manual_control.h"
#include "actuator/fretboard_link.h"
#include "detector/detector.h"
#include "detector/cv_marvin_v1.h"
#include "video/video.h"
#include "fret.h"
#include "net/t1s/t1s_link.h"
#include <stdlib.h>                      /* strtoul for the fauxmote btn mask */
#include "net/fauxmote/fauxmote_link.h"
#include "net/fauxmote/mf_proto.h"
#include "storage/storage.h"
#include "health/health_monitor.h"
#include "health/nocache_guard.h"
#include "perf_log/perf_log.h"
#include "perf_log/perf_log_sink.h"
#include "perf_log/perf_log_rx.h"
#include "ui/screens/navigation/screen_navigation.h"
#include "ui/gfx/ui_surface.h"
#include "results/results.h"
#include "game/game_catalog.h"
#include "game/game_art.h"
#include "game/game_selection.h"
#include "game/game_controller.h"
#include "ui/ui_manager.h"
#include "ui/titlebar.h"
#include "ui/screens/wiimotes/screen_wiimotes.h"
#include "ui/screens/bus/screen_bus.h"
#include "flash/qspi_smoke.h"
#include "flash/settings.h"

#define CON_TASK_STACK_WORDS  1024u
#define CON_TASK_PRIORITY     2u      /* low / UI band — human-interactive */

#define CON_RX_THRESHOLD      1u      /* wake on any inbound byte */
#define CON_RX_WAIT_MS        100u    /* bounded so a missed notify can't wedge */

/* embedded-cli internal buffer (static-allocation mode → no malloc). The binding pool
 * dominates it and grows with the command table (~20 bytes per command on this 32-bit
 * target), so the old 1024 left only ~40 bytes of slack at the current count — i.e. the
 * next command added would have turned into a boot assert. 2 KB carries ~50 commands. The
 * exact requirement is checked at init; bump this if that assert ever fires. */
static CLI_UINT s_cli_buf[BYTES_TO_CLI_UINTS(2048)];

static EmbeddedCli *s_cli;

static StackType_t   s_task_stack[CON_TASK_STACK_WORDS];
static StaticTask_t  s_task_tcb;

/* Given from the FLEXCOM2 read callback (ISR) when the RX ring has a byte;
 * woken task drains and feeds the line editor. */
static SemaphoreHandle_t s_rx_notify;
static StaticSemaphore_t s_rx_notify_buf;

/* ---- output ------------------------------------------------------------- */

static void console_write_char(EmbeddedCli *cli, char c)
{
    (void)cli;
    /* TX ring drains at line rate regardless of any listener (the UART clocks
     * bytes onto the wire), so a bounded retry only ever waits for ring space,
     * never for a reader. Cap it so a pathological full-ring can't spin. */
    for (uint32_t tries = 0u; tries < 1000u; tries++)
    {
        if (FLEXCOM2_USART_Write((uint8_t *)&c, 1u) == 1u) { return; }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void console_printf(const char *fmt, ...)
{
    static char buf[160];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    embeddedCliPrint(s_cli, buf);   /* appends newline */
}

/* ---- arg parsing helpers ------------------------------------------------ */

static int parse_onoff(const char *s)
{
    if (s == NULL) { return -1; }
    if (strcmp(s, "on") == 0 || strcmp(s, "1") == 0)  { return 1; }
    if (strcmp(s, "off") == 0 || strcmp(s, "0") == 0) { return 0; }
    return -1;
}

static int parse_detector(const char *s)
{
    if (s == NULL) { return -1; }
    if (strcmp(s, "cv") == 0)  { return DETECTOR_CV_MARVIN_V1; }
    if (strcmp(s, "fretboard") == 0) { return DETECTOR_FRETBOARD; }
    return -1;
}

static int parse_fret(const char *s)
{
    if (s == NULL || s[0] == '\0' || s[1] != '\0') { return -1; }
    switch (s[0])
    {
        case 'g': case 'G': return FRET_GREEN;
        case 'r': case 'R': return FRET_RED;
        case 'y': case 'Y': return FRET_YELLOW;
        case 'b': case 'B': return FRET_BLUE;
        case 'o': case 'O': return FRET_ORANGE;
        default:            return -1;
    }
}

static uint32_t parse_u32(const char *s, uint32_t dflt)
{
    if (s == NULL || s[0] == '\0') { return dflt; }
    uint32_t v = 0u;
    for (const char *p = s; *p != '\0'; p++)
    {
        if (*p < '0' || *p > '9') { return dflt; }
        v = (v * 10u) + (uint32_t)(*p - '0');
    }
    return v;
}

/* Pull up to n non-negative integers out of s, separated by any run of
 * non-digit characters (so "2026-06-23" and "14:03:00" both parse). Returns
 * the count parsed. */
static int parse_ints(const char *s, int *out, int n)
{
    int got = 0;
    if (s == NULL) { return 0; }
    while (got < n && *s != '\0')
    {
        while (*s != '\0' && (*s < '0' || *s > '9')) { s++; }
        if (*s < '0' || *s > '9') { break; }
        int v = 0;
        while (*s >= '0' && *s <= '9') { v = (v * 10) + (*s - '0'); s++; }
        out[got++] = v;
    }
    return got;
}

/* Day of week (0 = Sunday) for a Gregorian date, matching struct tm's tm_wday.
 * Sakamoto's method; m is 1-12. */
static int day_of_week(int y, int m, int d)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) { y -= 1; }
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static const char *const k_wday[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

/* ---- command handlers --------------------------------------------------- */

static void cmd_status(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)args; (void)ctx;

    detector_id_t act = Detector_GetActive();
    Video_FrameInfo vi;
    Video_GetFrameInfo(&vi);

    console_printf("link:       %s", FretboardLink_IsConnected() ? "up" : "down");
    console_printf("active:     %s", (act == DETECTOR_CV_MARVIN_V1) ? "cv" : "fretboard");
    console_printf("detect cv:  %s", Detector_IsEnabled(DETECTOR_CV_MARVIN_V1) ? "on" : "off");
    console_printf("manual:     %s", ManualControl_IsEnabled() ? "on" : "off");
    console_printf("timing:     %s", GameTiming_IsEnabled() ? "on" : "off");
    console_printf("video:      %ux%u frame=%lu",
                   (unsigned)vi.width, (unsigned)vi.height,
                   (unsigned long)vi.frame_count);
}

static void cmd_t1s(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)args; (void)ctx;

    bool synced = false;
    uint8_t txc = 0u, rxc = 0u;
    T1SLink_GetState(&synced, &txc, &rxc);

    console_printf("link:    %s", T1SLink_IsConnected() ? "up" : "down");
    console_printf("synced:  %s", synced ? "yes" : "no");
    console_printf("chipRev: %u", (unsigned)T1SLink_ChipRev());
    console_printf("plca:    coordinator id=%u/%u",
                   (unsigned)T1SLink_NodeId(), (unsigned)T1SLink_NodeCount());
    console_printf("credits: tx=%u rx=%u", (unsigned)txc, (unsigned)rxc);
    console_printf("tx cmds: %lu", (unsigned long)T1SLink_TxCount());
    console_printf("rx frms: %lu", (unsigned long)T1SLink_RxCount());
    console_printf("svc ovr: %lu", (unsigned long)T1SLink_ServiceOverruns());

    T1SLink_BusStats bs;
    if (T1SLink_GetBusStats(&bs))
    {
        console_printf("bus util:%lu.%lu%%  online:%u/%u  up:%lus",
                       (unsigned long)(bs.util_permille / 10u),
                       (unsigned long)(bs.util_permille % 10u),
                       (unsigned)bs.nodes_online, (unsigned)bs.nodes_total,
                       (unsigned long)bs.uptime_s);
        console_printf("tx tot: %lu  rx tot: %lu",
                       (unsigned long)bs.tx_total, (unsigned long)bs.rx_total);
        console_printf("crc err:%lu  sym err:%lu  err:%lu ppm",
                       (unsigned long)bs.crc_total, (unsigned long)bs.sym_total,
                       (unsigned long)bs.err_rate_ppm);
    }
}

static void cmd_nodes(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)args; (void)ctx;

    uint8_t n = T1SLink_NodeTableCount();
    console_printf("id  type      pres  age    txtot   rxtot   tx/s  rx/s  crc  sym");

    T1SLink_NodeStats st;
    if (T1SLink_GetSelfStats(&st))
    {
        console_printf("%-3u %-9s %-4s  %-5lu  %-7lu %-7lu %-5lu %-5lu %-4u %u",
                       (unsigned)st.node_id, st.type, "yes",
                       0ul,
                       (unsigned long)st.tx_count, (unsigned long)st.rx_count,
                       (unsigned long)st.tx_rate, (unsigned long)st.rx_rate,
                       (unsigned)st.crc_err, (unsigned)st.sym_err);
    }
    for (uint8_t i = 0u; i < n; i++)
    {
        if (T1SLink_GetNodeStats(i, &st))
        {
            console_printf("%-3u %-9s %-4s  %-5lu  %-7lu %-7lu %-5lu %-5lu %-4u %u",
                           (unsigned)st.node_id, st.type,
                           st.present ? "yes" : "no",
                           (unsigned long)st.age_ms,
                           (unsigned long)st.tx_count, (unsigned long)st.rx_count,
                           (unsigned long)st.tx_rate, (unsigned long)st.rx_rate,
                           (unsigned)st.crc_err, (unsigned)st.sym_err);
        }
    }
}

static void cmd_time(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *sub = embeddedCliGetToken(args, 1);

    if (sub == NULL)
    {
        struct tm now;
        memset(&now, 0, sizeof(now));
        RTC_TimeGet(&now);
        int wd = (now.tm_wday >= 0 && now.tm_wday <= 6) ? now.tm_wday : 0;
        console_printf("%04d-%02d-%02dT%02d:%02d:%02dZ (%s)",
                       now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
                       now.tm_hour, now.tm_min, now.tm_sec, k_wday[wd]);
        return;
    }

    if (strcmp(sub, "set") == 0)
    {
        int ymd[3], hms[3];
        if (parse_ints(embeddedCliGetToken(args, 2), ymd, 3) != 3 ||
            parse_ints(embeddedCliGetToken(args, 3), hms, 3) != 3)
        {
            console_printf("usage: time set YYYY-MM-DD HH:MM:SS  (UTC)");
            return;
        }
        int Y = ymd[0], Mo = ymd[1], D = ymd[2];
        int h = hms[0], mi = hms[1], s = hms[2];
        if (Y < 1970 || Y > 2099 || Mo < 1 || Mo > 12 || D < 1 || D > 31 ||
            h > 23 || mi > 59 || s > 59)
        {
            console_printf("out of range (expect UTC YYYY-MM-DD HH:MM:SS)");
            return;
        }

        struct tm t;
        memset(&t, 0, sizeof(t));
        t.tm_year = Y - 1900;
        t.tm_mon  = Mo - 1;
        t.tm_mday = D;
        t.tm_hour = h;
        t.tm_min  = mi;
        t.tm_sec  = s;
        t.tm_wday = day_of_week(Y, Mo, D);

        if (RTC_TimeSet(&t))
        {
            console_printf("set %04d-%02d-%02dT%02d:%02d:%02dZ", Y, Mo, D, h, mi, s);
        }
        else
        {
            console_printf("RTC set failed");
        }
        return;
    }

    console_printf("usage: time [set YYYY-MM-DD HH:MM:SS]");
}

static void cmd_player(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *name = embeddedCliGetToken(args, 1);
    if (name == NULL)
    {
        console_printf("player: %s", Results_GetPlayer());
        return;
    }
    Results_SetPlayer(name);
    console_printf("player = %s", Results_GetPlayer());
}

static void cmd_scores(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *setlist = embeddedCliGetToken(args, 1);
    const char *idx     = embeddedCliGetToken(args, 2);
    const char *diff    = embeddedCliGetToken(args, 3);   /* optional */
    if (setlist == NULL || idx == NULL)
    {
        console_printf("usage: scores <main|bonus> <index> [difficulty]");
        return;
    }

    uint8_t index = (uint8_t)parse_u32(idx, 0u);
    results_score_t top[5];
    int n = Results_TopN(setlist, index, diff, top, 5);
    if (n == 0)
    {
        console_printf("no scores for %s #%u%s%s", setlist, (unsigned)index,
                       (diff != NULL) ? " " : "", (diff != NULL) ? diff : "");
        return;
    }
    for (int i = 0; i < n; i++)
    {
        console_printf("%d. %9lu  %-12s %s", i + 1,
                       (unsigned long)top[i].score, top[i].player, top[i].timestamp);
    }
}

static void cmd_results(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    /* Synthetic-row injection for testing the CSV write/read path before the
     * gameplay engine populates real records. */
    const char *sub = embeddedCliGetToken(args, 1);
    if (sub == NULL || strcmp(sub, "add") != 0)
    {
        console_printf("usage: results add <main|bonus> <index> <difficulty> <part> <score>");
        return;
    }
    const char *setlist = embeddedCliGetToken(args, 2);
    const char *idx     = embeddedCliGetToken(args, 3);
    const char *diff    = embeddedCliGetToken(args, 4);
    const char *part    = embeddedCliGetToken(args, 5);
    const char *score   = embeddedCliGetToken(args, 6);
    if (setlist == NULL || idx == NULL || diff == NULL || part == NULL || score == NULL)
    {
        console_printf("usage: results add <main|bonus> <index> <difficulty> <part> <score>");
        return;
    }

    results_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.game       = "gh3-wii";
    rec.setlist    = setlist;
    rec.index      = (uint8_t)parse_u32(idx, 0u);
    rec.song       = "Test, Song";   /* comma exercises the CSV quoting path */
    rec.difficulty = diff;
    rec.part       = part;
    rec.score      = parse_u32(score, 0u);

    console_printf("%s", Results_Append(&rec) ? "added" : "append failed");
}

static void cmd_catalog(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *sub = embeddedCliGetToken(args, 1);

    if (sub == NULL)
    {
        console_printf("usage: catalog <reload|ls|<main|bonus> <index>>");
        return;
    }
    if (strcmp(sub, "reload") == 0)
    {
        bool ok = GameCatalog_Reload();
        console_printf("%s; %d song(s) cached", ok ? "reloaded" : "no catalog",
                       GameCatalog_Count());
        return;
    }
    if (strcmp(sub, "ls") == 0)
    {
        int n = GameCatalog_Count();
        if (n == 0) { console_printf("catalog empty (try: catalog reload)"); return; }
        for (int i = 0; i < n; i++)
        {
            const game_catalog_entry_t *e = GameCatalog_At(i);
            console_printf("%-5s %2u  %-32s %s",
                           (e->setlist == GP_SETLIST_BONUS) ? "bonus" : "main",
                           (unsigned)e->index, e->title, e->artist);
        }
        return;
    }

    /* catalog <main|bonus> <index> */
    const char *idx = embeddedCliGetToken(args, 2);
    int sl = (strcmp(sub, "main") == 0) ? GP_SETLIST_MAIN
           : (strcmp(sub, "bonus") == 0) ? GP_SETLIST_BONUS : -1;
    if (sl < 0 || idx == NULL)
    {
        console_printf("usage: catalog <reload|ls|<main|bonus> <index>>");
        return;
    }

    game_catalog_entry_t e;
    if (!GameCatalog_Lookup((uint8_t)sl, (uint8_t)parse_u32(idx, 0u), &e))
    {
        console_printf("Unknown song");
        return;
    }
    console_printf("%s — %s", e.title, e.artist);
    if (e.album[0] != '\0' && e.year != 0) { console_printf("  album: %s (%u)", e.album, (unsigned)e.year); }
    else if (e.album[0] != '\0')           { console_printf("  album: %s", e.album); }
    else if (e.year != 0)                  { console_printf("  year: %u", (unsigned)e.year); }
    if (e.genre[0] != '\0')      { console_printf("  genre: %s", e.genre); }
    if (e.difficulty[0] != '\0') { console_printf("  difficulty: %s", e.difficulty); }
    if (e.bpm != 0 || e.length_s != 0)
    {
        console_printf("  bpm %u, %u:%02u", (unsigned)e.bpm,
                       (unsigned)(e.length_s / 60u), (unsigned)(e.length_s % 60u));
    }
}

static void cmd_art(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *sub = embeddedCliGetToken(args, 1);

    /* No args (or "ls"): report the cache fill. */
    if (sub == NULL || strcmp(sub, "ls") == 0)
    {
        console_printf("art: %s; %d small, %d large cached",
                       GameArt_IsLoaded() ? "loaded" : "not loaded",
                       GameArt_CountSmall(), GameArt_CountLarge());
        return;
    }

    /* art <main|bonus> <index>: report whether each tier has this cover. */
    const char *idx = embeddedCliGetToken(args, 2);
    int sl = (strcmp(sub, "main") == 0) ? GP_SETLIST_MAIN
           : (strcmp(sub, "bonus") == 0) ? GP_SETLIST_BONUS : -1;
    if (sl < 0 || idx == NULL)
    {
        console_printf("usage: art [ls | <main|bonus> <index>]");
        return;
    }

    uint8_t s = (uint8_t)sl, i = (uint8_t)parse_u32(idx, 0u);
    console_printf("%s-%02u: small %s, large %s",
                   (s == GP_SETLIST_BONUS) ? "bonus" : "main", (unsigned)i,
                   (GameArt_Small(s, i) != NULL) ? "yes" : "no",
                   (GameArt_Large(s, i) != NULL) ? "yes" : "no");
}

static void sd_out(void *ctx, const char *line)
{
    (void)ctx;
    embeddedCliPrint(s_cli, line);
}

static void cmd_sd(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *sub = embeddedCliGetToken(args, 1);
    if (sub == NULL)
    {
        console_printf("usage: sd <info|ls|bench|mount|unmount> [arg]");
    }
    else if (strcmp(sub, "info") == 0)
    {
        Storage_DiagInfo(sd_out, NULL);
    }
    else if (strcmp(sub, "ls") == 0)
    {
        Storage_DiagList(sd_out, NULL, embeddedCliGetToken(args, 2));
    }
    else if (strcmp(sub, "bench") == 0)
    {
        Storage_DiagBench(sd_out, NULL, parse_u32(embeddedCliGetToken(args, 2), 4u));
    }
    else if (strcmp(sub, "mount") == 0)
    {
        console_printf("%s", Storage_Mount() ? "mounted" : "mount failed");
    }
    else if (strcmp(sub, "unmount") == 0)
    {
        console_printf("%s", Storage_Unmount() ? "unmounted" : "unmount failed");
    }
    else
    {
        console_printf("usage: sd <info|ls|bench|mount|unmount> [arg]");
    }
}

static void cmd_health(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)args; (void)ctx;
    HealthMonitor_Report(sd_out, NULL);   /* per-task stack + runtime table */
}

/* `perf dump <canvas> [x y w h]` requests a canvas dump straight from the console,
 * bypassing the perf-log CDC command channel. Useful when a dump is not arriving
 * and it matters whether the request path or the emit path is at fault: the host
 * only has to listen (`marvin-perf screendump --listen`). */
static void cmd_perf_dump(char *args)
{
    const char *a1 = embeddedCliGetToken(args, 2);
    if (a1 == NULL)
    {
        printf("usage: perf dump <canvas 0-6> [x y w h]\r\n");
        return;
    }

    int canvas = atoi(a1);
    const char *ax = embeddedCliGetToken(args, 3);
    const char *ay = embeddedCliGetToken(args, 4);
    const char *aw = embeddedCliGetToken(args, 5);
    const char *ah = embeddedCliGetToken(args, 6);

    uint16_t x = (ax != NULL) ? (uint16_t)atoi(ax) : 0u;
    uint16_t y = (ay != NULL) ? (uint16_t)atoi(ay) : 0u;
    uint16_t w = (aw != NULL) ? (uint16_t)atoi(aw) : 0u;
    uint16_t h = (ah != NULL) ? (uint16_t)atoi(ah) : 0u;

    if (canvas < 0 || canvas > 6)
    {
        printf("perf: canvas must be 0-6\r\n");
        return;
    }

    PerfLog_RequestCanvasDump((uint8_t)canvas, x, y, w, h);
    printf("perf: canvas %d dump requested (%u,%u %ux%u)\r\n",
           canvas, (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h);
}

/* Read the nav-drawer icon rect straight out of its canvas surface and report it
 * over the console. USB is unusable for this — the device link dies after the first
 * host session — so this is the only way to answer "is the corruption in the
 * framebuffer or only on the glass" without a working screendump.
 *
 * Row geometry mirrors the design: buttons 287x56 at x=16, y=113+64n, icon 24x24
 * inset by the button's 16 px imageMargin. */
/* Dump an arbitrary rect of the nav surface, so a defect can be located rather than
 * guessed at. `nav px <x> <y> [w] [h]`. */
static void cmd_nav_px(char *args)
{
    const char *ax = embeddedCliGetToken(args, 2);
    const char *ay = embeddedCliGetToken(args, 3);
    const char *aw = embeddedCliGetToken(args, 4);
    const char *ah = embeddedCliGetToken(args, 5);

    if (ax == NULL || ay == NULL) { printf("usage: nav px <x> <y> [w] [h]\r\n"); return; }

    int x = atoi(ax), y = atoi(ay);
    int w = (aw != NULL) ? atoi(aw) : 16;
    int h = (ah != NULL) ? atoi(ah) : 8;
    if (w < 1 || w > 64) { w = 16; }
    if (h < 1 || h > 64) { h = 8; }

    const void *base = NULL;
    uint16_t sw = 0u, sh = 0u;
    GFXC_COLOR_FORMAT mode = GFX_COLOR_MODE_RGB_565;
    if (!UiSurface_Get(CANVAS_NAVIGATION, &base, &sw, &sh, &mode) || base == NULL)
    {
        printf("nav: no navigation surface\r\n");
        return;
    }
    if (x < 0 || y < 0 || (uint32_t)x + w > sw || (uint32_t)y + h > sh)
    {
        printf("nav: rect outside %ux%u\r\n", (unsigned)sw, (unsigned)sh);
        return;
    }

    const uint16_t *fb = (const uint16_t *)base;
    printf("nav px %d,%d %dx%d (RGB565):\r\n", x, y, w, h);
    for (int yy = 0; yy < h; yy++)
    {
        printf("   ");
        for (int xx = 0; xx < w; xx++)
        {
            printf(" %04X", (unsigned)fb[(uint32_t)(y + yy) * sw + (x + xx)]);
        }
        printf("\r\n");
    }
}

static void cmd_nav_icon(const char *arg)
{
    int row = (arg != NULL) ? atoi(arg) : 0;
    uint16_t ix = 0u, iy = 0u, id = 0u;

    if (row < 0 || !ScreenNavigation_GetIconRect((unsigned)row, &ix, &iy, &id))
    {
        printf("nav: no such drawer row\r\n");
        return;
    }

    const void *base = NULL;
    uint16_t sw = 0u, sh = 0u;
    GFXC_COLOR_FORMAT mode = GFX_COLOR_MODE_RGB_565;

    if (!UiSurface_Get(CANVAS_NAVIGATION, &base, &sw, &sh, &mode) || base == NULL)
    {
        printf("nav: no navigation surface\r\n");
        return;
    }
    if (mode != GFX_COLOR_MODE_RGB_565)
    {
        printf("nav: unexpected colour mode %u\r\n", (unsigned)mode);
        return;
    }

    if ((uint32_t)ix + id > sw || (uint32_t)iy + id > sh)
    {
        printf("nav: icon rect outside %ux%u\r\n", (unsigned)sw, (unsigned)sh);
        return;
    }

    const uint16_t *fb = (const uint16_t *)base;

    /* Build a legend of the distinct RGB565 values present, then print the rect as
     * legend indices. Lossless for the question at hand — a classifier that buckets
     * by "looks dark" hides both which colour a pixel actually is and whether a gap
     * is background or something else. */
    #define NAV_LEGEND_MAX 16u
    #define NAV_DUMP_MAX   32u          /* line buffer below */
    uint16_t pal[NAV_LEGEND_MAX];
    uint32_t cnt[NAV_LEGEND_MAX];
    uint32_t npal = 0u, other = 0u;

    if (id > NAV_DUMP_MAX)
    {
        printf("nav: icon %ux%u too large to dump\r\n", (unsigned)id, (unsigned)id);
        return;
    }

    for (uint16_t yy = 0u; yy < id; yy++)
    {
        for (uint16_t xx = 0u; xx < id; xx++)
        {
            uint16_t v = fb[(uint32_t)(iy + yy) * sw + (ix + xx)];
            uint32_t k;
            for (k = 0u; k < npal; k++) { if (pal[k] == v) { cnt[k]++; break; } }
            if (k == npal)
            {
                if (npal < NAV_LEGEND_MAX) { pal[npal] = v; cnt[npal] = 1u; npal++; }
                else                       { other++; }
            }
        }
    }

    printf("nav icon row %d at %u,%u (%ux%u, RGB565):\r\n",
           row, (unsigned)ix, (unsigned)iy, (unsigned)id, (unsigned)id);

    for (uint16_t yy = 0u; yy < id; yy++)
    {
        char line[NAV_DUMP_MAX + 1u];
        for (uint16_t xx = 0u; xx < id; xx++)
        {
            uint16_t v = fb[(uint32_t)(iy + yy) * sw + (ix + xx)];
            char c = '*';
            for (uint32_t k = 0u; k < npal; k++)
            {
                if (pal[k] == v)
                {
                    c = (char)((k < 10u) ? ('0' + k) : ('a' + (k - 10u)));
                    break;
                }
            }
            line[xx] = c;
        }
        line[id] = '\0';
        printf("  %s\r\n", line);
    }

    printf("  legend (index = RGB565 -> R,G,B, count):\r\n");
    for (uint32_t k = 0u; k < npal; k++)
    {
        uint16_t v = pal[k];
        printf("   %c = %04X -> %3u,%3u,%3u  x%lu\r\n",
               (char)((k < 10u) ? ('0' + k) : ('a' + (k - 10u))), (unsigned)v,
               (unsigned)(((v >> 11) & 0x1Fu) << 3),
               (unsigned)(((v >> 5) & 0x3Fu) << 2),
               (unsigned)((v & 0x1Fu) << 3),
               (unsigned long)cnt[k]);
    }
    if (other != 0u) { printf("   '*' = %lu px beyond legend\r\n", (unsigned long)other); }


    /* Corner values verbatim — the reported defect is a small block up here. */
    printf("  top-left 5x5 RGB565:\r\n");
    for (uint16_t yy = 0u; yy < 5u; yy++)
    {
        printf("   ");
        for (uint16_t xx = 0u; xx < 5u; xx++)
        {
            printf(" %04X", (unsigned)fb[(uint32_t)(iy + yy) * sw + (ix + xx)]);
        }
        printf("\r\n");
    }
}

static void cmd_nav(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;

    const char *sub = embeddedCliGetToken(args, 1);
    const char *val = embeddedCliGetToken(args, 2);

    if (sub != NULL && strcmp(sub, "icon") == 0)
    {
        cmd_nav_icon(val);
        return;
    }

    if (sub != NULL && strcmp(sub, "px") == 0)
    {
        cmd_nav_px(args);
        return;
    }

    if (sub != NULL && strcmp(sub, "slide") == 0 && val != NULL)
    {
        bool on = (strcmp(val, "on") == 0) || (strcmp(val, "1") == 0);
        ScreenNavigation_SetSlide(on);
    }

    printf("nav: slide %s\r\n", ScreenNavigation_GetSlide() ? "on" : "off");
}

static void cmd_perf(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;

    const char *sub = embeddedCliGetToken(args, 1);
    if (sub != NULL && strcmp(sub, "dump") == 0)
    {
        cmd_perf_dump(args);
        return;
    }

    static const char *const REASON[] = {
        "idle", "requested", "running", "done",
        "no-surface", "bad-mode", "outside", "empty", "too-wide",
    };

    perf_log_diag_t d;
    PerfLog_GetDiag(&d);

    const char *reason = (d.canvas_reason < (sizeof REASON / sizeof REASON[0]))
                       ? REASON[d.canvas_reason] : "?";

    printf("perf: drain %s, stack free %lu words\r\n",
           d.running ? "running" : "STOPPED",
           (unsigned long)d.drain_stack_free_words);
    perf_sink_link_t lk;
    PerfLogSinkCdc_GetLinkState(&lk);

    printf("  sink: %s, tx credits %lu, reclaims %lu\r\n",
           d.sink_connected ? "connected" : "no host",
           (unsigned long)d.sink_credits, (unsigned long)d.sink_reclaims);
    printf("  usb tx: submitted %lu completed %lu%s\r\n",
           (unsigned long)PerfLogSinkCdc_WritesSubmitted(),
           (unsigned long)PerfLogSinkCdc_WritesCompleted(),
           (PerfLogSinkCdc_WritesSubmitted() - PerfLogSinkCdc_WritesCompleted()
                > 12u) ? "  <-- STALLED" : "");
    printf("  usb: configured=%d dtr=%d | cfg %lu decfg %lu reset %lu cls %lu\r\n",
           lk.configured ? 1 : 0, lk.dtr ? 1 : 0,
           (unsigned long)lk.n_configured, (unsigned long)lk.n_decfg,
           (unsigned long)lk.n_reset, (unsigned long)lk.n_cls);
    uint32_t g_trips = 0u, g_blk = 0u, g_off = 0u, g_exp = 0u, g_got = 0u;
    NocacheGuard_GetState(&g_trips, &g_blk, &g_off, &g_exp, &g_got);
    if (g_trips == 0u)
    {
        printf("  nocache guard: intact (block0 %p)\r\n", NocacheGuard_BlockAddr(0));
    }
    else
    {
        printf("  nocache guard: CORRUPTED x%lu — block %lu +%lu, want %08lX got %08lX\r\n",
               (unsigned long)g_trips, (unsigned long)g_blk, (unsigned long)g_off,
               (unsigned long)g_exp, (unsigned long)g_got);
    }

    perf_rx_diag_t rx;
    PerfLogRx_GetDiag(&rx);
    bool rx_armed = false; uint32_t rx_fails = 0u;
    PerfLogSinkCdc_GetRxArm(&rx_armed, &rx_fails);
    printf("  rx arm: %s, failures %lu\r\n",
           rx_armed ? "queued" : "NOT QUEUED", (unsigned long)rx_fails);
    printf("  rx: bytes %lu frames %lu dispatched %lu drops %lu | last cmd 0x%02X len %u state %u\r\n",
           (unsigned long)rx.bytes, (unsigned long)rx.frames,
           (unsigned long)rx.dispatched, (unsigned long)rx.drops,
           (unsigned)rx.last_cmd, (unsigned)rx.last_len, (unsigned)rx.state);
    printf("  drops: state %lu strip %lu sink %lu\r\n",
           (unsigned long)d.drop_state, (unsigned long)d.drop_strip,
           (unsigned long)d.drop_sink);
    printf("  canvas dump: %s (canvas %u, %u bands, epoch %lu)\r\n",
           reason, (unsigned)d.canvas_id, (unsigned)d.canvas_bands,
           (unsigned long)d.canvas_epoch);
}

static void cmd_detect(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    int id  = parse_detector(embeddedCliGetToken(args, 1));
    int val = parse_onoff(embeddedCliGetToken(args, 2));
    /* Only the CV detector has a publish enable that anything reads; the
     * fretboard variant was a dead no-op. Handing the game to the fretboard is
     * `active fretboard` (arms it over T1S), not a `detect` toggle. */
    if (id != DETECTOR_CV_MARVIN_V1 || val < 0)
    {
        console_printf("usage: detect cv <on|off>");
        return;
    }
    if (val) { Detector_Enable((detector_id_t)id); }
    else     { Detector_Disable((detector_id_t)id); }
    console_printf("detect cv = %s", val ? "on" : "off");
}

static void cmd_active(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    int id = parse_detector(embeddedCliGetToken(args, 1));
    if (id < 0)
    {
        console_printf("usage: active <cv|fretboard>");
        return;
    }
    Detector_SetActive((detector_id_t)id);

    /* Select which detector owns the game. Arming the fretboard is gated on the
     * gameplay window (GameTiming), not on this selection: outside a song marvin
     * keeps the controller for menu nav / manual control, so the fretboard only
     * arms once a song starts. Re-evaluate the arm bit now so selecting it
     * mid-song takes effect immediately, and selecting cv disarms it at once. */
    FretboardLink_UpdateArm();

    if (id == DETECTOR_FRETBOARD)
    {
        console_printf("active = fretboard (%s)",
                       Detector_FretboardDriving() ? "armed — song active"
                                                    : "armed when a song starts");
    }
    else
    {
        console_printf("active = cv");
    }
}

static void cmd_cvcfg(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *w = embeddedCliGetToken(args, 1);
    if (w == NULL)
    {
        console_printf("cvcfg = %s (usage: cvcfg <1p|2pl>)",
                       CvMarvinV1_GetConfig()->name);
        return;
    }
    if (strcmp(w, "1p") == 0)        { CvMarvinV1_SetConfig(&CV_MARVIN_CFG_1P); }
    else if (strcmp(w, "2pl") == 0)  { CvMarvinV1_SetConfig(&CV_MARVIN_CFG_2P_LEFT); }
    else { console_printf("usage: cvcfg <1p|2pl>"); return; }
    console_printf("cvcfg -> %s", w);
}

static void cmd_timing(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    int val = parse_onoff(embeddedCliGetToken(args, 1));
    if (val < 0)
    {
        console_printf("usage: timing <on|off>");
        return;
    }
    GameTiming_SetEnabled(val != 0);
    console_printf("timing = %s", val ? "on" : "off");
}

static void cmd_manual(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    int val = parse_onoff(embeddedCliGetToken(args, 1));
    if (val < 0)
    {
        console_printf("usage: manual <on|off>");
        return;
    }
    ManualControl_SetEnabled(val != 0);
    console_printf("manual = %s", val ? "on" : "off");
}

static void cmd_fret(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    int fret = parse_fret(embeddedCliGetToken(args, 1));
    int val  = parse_onoff(embeddedCliGetToken(args, 2));
    if (fret < 0 || val < 0)
    {
        console_printf("usage: fret <g|r|y|b|o> <0|1>");
        return;
    }
    if (!ManualControl_IsEnabled())
    {
        console_printf("note: manual mode off — run 'manual on' first");
    }
    ManualControl_SetFret((fret_t)fret, val != 0);
    console_printf("fret %s = %d", embeddedCliGetToken(args, 1), val);
}

static void cmd_strum(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *dir = embeddedCliGetToken(args, 1);
    bool down;
    if      (dir != NULL && strcmp(dir, "down") == 0) { down = true; }
    else if (dir != NULL && strcmp(dir, "up") == 0)   { down = false; }
    else
    {
        console_printf("usage: strum <down|up>");
        return;
    }
    if (!ManualControl_IsEnabled())
    {
        console_printf("note: manual mode off — run 'manual on' first");
    }
    /* One-shot pulse: assert, brief hold, release. */
    ManualControl_SetStrum(down, true);
    vTaskDelay(pdMS_TO_TICKS(40));
    ManualControl_SetStrum(down, false);
    console_printf("strum %s", down ? "down" : "up");
}

static void cmd_backlight(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *tok = embeddedCliGetToken(args, 1);
    if (tok == NULL)
    {
        console_printf("backlight = %u%%  (usage: backlight <0-100>)",
                       (unsigned)UiManager_GetBacklight());
        return;
    }
    uint32_t pct = parse_u32(tok, 101u);   /* 101 = invalid (out of 0-100 range) */
    if (pct > 100u)
    {
        console_printf("usage: backlight <0-100>");
        return;
    }
    UiManager_SetBacklight(pct);                       /* apply live */
    bool saved = Settings_SetBacklight((uint8_t)pct);  /* persist across reboot */
    console_printf("backlight = %u%% (%s)", (unsigned)UiManager_GetBacklight(),
                   saved ? "saved" : "save FAIL");
}

/* Attribute the renderer's load between the titlebar's two damage sources.
 *
 * The frame rate is the number that matters: with LE_PREEMPTION_LEVEL 0 a Legato frame
 * runs to completion inside one LEGATO_Tasks tick, so LEGATO_Tasks' share of the CPU is
 * frames/s x cost-per-frame — and the damaged area barely enters into it. Sampled over
 * the same 500 ms window `health` uses, so the two readings are comparable: run `titlebar`
 * for frames/s, `health` for the per-task share, then toggle and repeat. */
#define RENDER_WINDOW_MS  500u

static void cmd_titlebar(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *tok = embeddedCliGetToken(args, 1);
    const char *val = embeddedCliGetToken(args, 2);

    if (tok != NULL && strcmp(tok, "probe") == 0)
    {
        /* Constant-content rects of increasing area (see Titlebar_ProbeFrameUs): the
         * intercept is the fixed per-frame overhead, the slope the per-pixel cost. */
        static const struct { uint32_t w, h; } SWEEP[] = {
            { 1u, 1u }, { 12u, 12u }, { 64u, 32u }, { 260u, 40u },
        };
        #define SWEEP_N  (sizeof(SWEEP) / sizeof(SWEEP[0]))

        unsigned n = (val != NULL) ? (unsigned)atoi(val) : 20u;
        uint32_t us[SWEEP_N];
        if (n == 0u || n > 200u) { n = 20u; }

        for (size_t i = 0u; i < SWEEP_N; i++)
        {
            us[i] = Titlebar_ProbeFrameUs(n, SWEEP[i].w, SWEEP[i].h);
        }
        if (us[0] == 0u)
        {
            console_printf("probe: no titlebar on screen");
            return;
        }

        for (size_t i = 0u; i < SWEEP_N; i++)
        {
            console_printf("  %3lux%-3lu (%6lu px) = %6lu us",
                           (unsigned long)SWEEP[i].w, (unsigned long)SWEEP[i].h,
                           (unsigned long)(SWEEP[i].w * SWEEP[i].h),
                           (unsigned long)us[i]);
        }

        uint32_t px_lo = SWEEP[0].w * SWEEP[0].h;
        uint32_t px_hi = SWEEP[SWEEP_N - 1u].w * SWEEP[SWEEP_N - 1u].h;
        uint32_t d_us  = (us[SWEEP_N - 1u] > us[0]) ? (us[SWEEP_N - 1u] - us[0]) : 0u;
        uint32_t fixed = us[0];
        uint32_t ns_px = (d_us * 1000u) / (px_hi - px_lo);
        console_printf("probe (%u iters): fixed ~%luus/frame, %lu ns/px over %lu px",
                       n, (unsigned long)fixed, (unsigned long)ns_px,
                       (unsigned long)(px_hi - px_lo));

        /* Now the real parts, each minus what an empty rect of the same size would cost,
         * so the remainder is that widget's own drawing. */
        static const struct { const char *name; Titlebar_Part part; uint32_t px; } PART[] = {
            { "dot   12x12", TITLEBAR_PART_DOT,   12u * 12u },
            { "plot   80x28", TITLEBAR_PART_PLOT,  80u * 28u },
            { "card  158x42", TITLEBAR_PART_CARD, 158u * 42u },
        };
        for (size_t i = 0u; i < (sizeof(PART) / sizeof(PART[0])); i++)
        {
            uint32_t tot  = Titlebar_ProbePartUs(n, PART[i].part);
            uint32_t base = fixed + (PART[i].px * ns_px) / 1000u;
            console_printf("  %s = %6lu us   (drawing %6lu us)",
                           PART[i].name, (unsigned long)tot,
                           (unsigned long)((tot > base) ? (tot - base) : 0u));
        }
        #undef SWEEP_N
        return;
    }

    if (tok != NULL)
    {
        bool on;
        if (val == NULL || (strcmp(val, "on") != 0 && strcmp(val, "off") != 0))
        {
            console_printf("usage: titlebar [pulse <on|off> | tiles <on|off> | probe [iters]]");
            return;
        }
        on = (strcmp(val, "on") == 0);

        if      (strcmp(tok, "pulse") == 0) { Titlebar_SetPulseEnabled(on); }
        else if (strcmp(tok, "tiles") == 0) { Titlebar_SetTilesEnabled(on); }
        else
        {
            console_printf("usage: titlebar [pulse <on|off> | tiles <on|off> | probe [iters]]");
            return;
        }
    }

    size_t f0 = UiManager_FrameCount();
    vTaskDelay(pdMS_TO_TICKS(RENDER_WINDOW_MS));
    size_t f1 = UiManager_FrameCount();

    Titlebar_Status st;
    Titlebar_GetStatus(&st);

    console_printf("titlebar: pulse=%s tiles=%s onscreen=%s  cpu=%lu.%lu%% bus=%lu.%lu%%",
                   st.pulse ? "on" : "off", st.tiles ? "on" : "off",
                   st.live ? "yes" : "no",
                   (unsigned long)(st.cpu_permille / 10u), (unsigned long)(st.cpu_permille % 10u),
                   (unsigned long)(st.bus_permille / 10u), (unsigned long)(st.bus_permille % 10u));
    console_printf("renderer: %lu frames in %ums (%lu/s)",
                   (unsigned long)(f1 - f0), (unsigned)RENDER_WINDOW_MS,
                   (unsigned long)((f1 - f0) * 1000u / RENDER_WINDOW_MS));
}

/* Render cost of the wiimotes screen's custom-painted widgets — whammy and tilt are the
 * last per-pixel float paints on a surface the user drags. Compare against `titlebar probe`
 * run on the same screen, which gives the fixed frame cost and ns/px floor. */
static void cmd_wiimotes(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *tok = embeddedCliGetToken(args, 1);
    const char *val = embeddedCliGetToken(args, 2);

    if (tok == NULL || strcmp(tok, "probe") != 0)
    {
        console_printf("usage: wiimotes probe [iters]");
        return;
    }

    ScreenWiimotes_Probe((val != NULL) ? (unsigned)atoi(val) : 20u, sd_out, NULL);
}

/* Render cost of the bus screen's custom widgets — all of them repaint at 1 Hz. */
static void cmd_bus(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *tok = embeddedCliGetToken(args, 1);
    const char *val = embeddedCliGetToken(args, 2);

    if (tok == NULL || strcmp(tok, "probe") != 0)
    {
        console_printf("usage: bus probe [iters]");
        return;
    }

    ScreenBus_Probe((val != NULL) ? (unsigned)atoi(val) : 20u, sd_out, NULL);
}

static void cmd_gamma(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *tok = embeddedCliGetToken(args, 1);
    if (tok == NULL)
    {
        console_printf("video levels = %s  (usage: gamma <on|off>)",
                       UiManager_GetVideoLevels() ? "on" : "off");
        return;
    }
    if (strcmp(tok, "on") == 0)       { UiManager_SetVideoLevels(true); }
    else if (strcmp(tok, "off") == 0) { UiManager_SetVideoLevels(false); }
    else { console_printf("usage: gamma <on|off>"); return; }
    console_printf("video levels = %s", UiManager_GetVideoLevels() ? "on" : "off");
}

static void cmd_qspi(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *sub = embeddedCliGetToken(args, 1);
    if ((sub != NULL) && (strcmp(sub, "bench") == 0))
    {
        QspiSmoke_Bench(parse_u32(embeddedCliGetToken(args, 2), 4u));
        console_printf("qspi bench: done (see log)");
        return;
    }
    if ((sub != NULL) && (strcmp(sub, "verify") == 0))
    {
        bool vok = QspiSmoke_Verify(parse_u32(embeddedCliGetToken(args, 2), 256u),
                                    parse_u32(embeddedCliGetToken(args, 3), 4u));
        console_printf("qspi verify: %s (see log)", vok ? "PASS" : "FAIL");
        return;
    }
    bool ok = QspiSmoke_Run();
    console_printf("qspi smoke test: %s", ok ? "PASS" : "FAIL");
}

static void cmd_settings(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *sub = embeddedCliGetToken(args, 1);
    if (sub == NULL)
    {
        const settings_t *s = Settings_Get();
        console_printf("settings: v%u backlight=%u%%  (usage: settings [dump|save|wipe|backlight <pct>])",
                       (unsigned)s->version, (unsigned)s->backlight_pct);
        return;
    }
    if (strcmp(sub, "dump") == 0)
    {
        Settings_Dump();
        console_printf("settings dump: see log");
        return;
    }
    if (strcmp(sub, "save") == 0)
    {
        console_printf("settings save: %s", Settings_Save() ? "ok" : "FAIL");
        return;
    }
    if (strcmp(sub, "wipe") == 0)
    {
        console_printf("settings wipe: %s", Settings_Wipe() ? "ok" : "FAIL");
        return;
    }
    if (strcmp(sub, "stress") == 0)
    {
        uint32_t n = parse_u32(embeddedCliGetToken(args, 2), 200u);
        console_printf("settings stress: %s (see log)", Settings_Stress(n) ? "PASS" : "FAIL");
        return;
    }
    console_printf("usage: settings [dump|save|wipe|stress [n]]  (set backlight via `backlight <pct>`)");
}

static void cmd_fauxmote(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *sub = embeddedCliGetToken(args, 1);

    if (sub == NULL || strcmp(sub, "status") == 0)
    {
#if (MARVIN_FAUXMOTE_TRANSPORT == FAUXMOTE_TRANSPORT_T1S)
        console_printf("transport: T1S (0x88B7)  tx=%lu rx=%lu",
                       (unsigned long)T1SLink_CtrlTxCount(),
                       (unsigned long)T1SLink_CtrlRxCount());
#else
        console_printf("transport: UART (FLEXCOM5)");
#endif
        uint8_t flags, slot, mode, res;
        uint32_t age;
        if (!Fauxmote_GetStatus(&flags, &slot, &mode, &res, &age))
        {
            console_printf("fauxmote: no STATUS received yet");
            return;
        }
        console_printf("age:       %lu ms", (unsigned long)age);
        console_printf("connected: %s", (flags & MF_ST_CONNECTED) ? "yes" : "no");
        console_printf("assigned:  %s (slot %u)", (flags & MF_ST_ASSIGNED) ? "yes" : "no", slot);
        console_printf("ext:       %s", (flags & MF_ST_EXT_ATTACHED) ? "yes" : "no");
        console_printf("pairing:   %s", (flags & MF_ST_PAIRING) ? "yes" : "no");
        console_printf("bonded:    %s", (flags & MF_ST_BONDED) ? "yes" : "no");
        console_printf("mode:      0x%02x  last_result=%u", mode, res);
        return;
    }
    if (strcmp(sub, "pair") == 0)      { Fauxmote_SendCmd(MF_CMD_PAIR);      console_printf("fauxmote: pair");      return; }
    if (strcmp(sub, "stop") == 0)      { Fauxmote_SendCmd(MF_CMD_STOP);      console_printf("fauxmote: stop");      return; }
    if (strcmp(sub, "reconnect") == 0) { Fauxmote_SendCmd(MF_CMD_RECONNECT); console_printf("fauxmote: reconnect"); return; }
    if (strcmp(sub, "unlink") == 0)    { Fauxmote_SendCmd(MF_CMD_UNLINK);    console_printf("fauxmote: unlink");    return; }
    if (strcmp(sub, "ext") == 0)
    {
        int v = parse_onoff(embeddedCliGetToken(args, 2));
        if (v < 0) { console_printf("usage: fauxmote ext <on|off>"); return; }
        Fauxmote_SendCmd(v ? MF_CMD_EXT_ATTACH : MF_CMD_EXT_DETACH);
        console_printf("fauxmote: ext %s", v ? "on" : "off");
        return;
    }
    if (strcmp(sub, "btn") == 0)
    {
        const char *m = embeddedCliGetToken(args, 2);
        if (m == NULL) { console_printf("usage: fauxmote btn <mask>  (e.g. 0x21 = green+strumdown; 0 = release)"); return; }
        if (GameTiming_IsEnabled())
        {
            console_printf("note: timing on — the pipeline will overwrite this mask; run 'timing off' first");
        }
        uint8_t mask = (uint8_t)strtoul(m, NULL, 0);
        Fauxmote_SendGuitar(mask, MF_WHAMMY_REST, 0u);
        console_printf("fauxmote: guitar mask=0x%02x", mask);
        return;
    }
    if (strcmp(sub, "pointer") == 0)
    {
        const char *xs = embeddedCliGetToken(args, 2);
        if (xs != NULL && strcmp(xs, "off") == 0)
        {
            Fauxmote_SendPointer(0u, 0u, false);
            console_printf("fauxmote: pointer off");
            return;
        }
        const char *ys = embeddedCliGetToken(args, 3);
        if (xs == NULL || ys == NULL)
        {
            console_printf("usage: fauxmote pointer <x> <y> | off   (x,y 0..255, 0,0=top-left)");
            return;
        }
        uint8_t x = (uint8_t)strtoul(xs, NULL, 0);
        uint8_t y = (uint8_t)strtoul(ys, NULL, 0);
        Fauxmote_SendPointer(x, y, true);
        console_printf("fauxmote: pointer %u,%u", (unsigned)x, (unsigned)y);
        return;
    }
    console_printf("usage: fauxmote [status|pair|stop|reconnect|unlink|ext <on|off>|btn <mask>|pointer <x> <y>|off]");
}

static void cmd_play(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *sub = embeddedCliGetToken(args, 1);

    if (sub != NULL && strcmp(sub, "stop") == 0)
    {
        GameController_Stop();
        console_printf("play: stop requested");
        return;
    }
    if (sub != NULL && strcmp(sub, "status") == 0)
    {
        console_printf("play: %s", GameController_IsBusy() ? "busy" : "idle");
        return;
    }
    if (sub != NULL && strcmp(sub, "attach") == 0)
    {
        /* No navigation: the operator set the game up by hand (e.g. a 2-player
         * match). Wait for gameplay, auto-pick the highway, actuate the song. */
        GameController_StartAttach();
        console_printf("play: attach — waiting for a gameplay screen");
        return;
    }

    const game_selection_t *s = GameSelection_Get();
    if (!s->valid)
    {
        console_printf("play: no song selected yet (pick one in song-select first)");
        return;
    }
    GameController_Start();
    console_printf("play: starting (setlist %u, song #%u, difficulty %u)",
                   (unsigned)s->setlist, (unsigned)s->index, (unsigned)s->difficulty);
}

static int8_t parse_pos_i8(const char *s)
{
    long v = strtol(s, NULL, 0);
    if (v >  127) { v =  127; }
    if (v < -127) { v = -127; }
    return (int8_t)v;
}

static void cmd_lemmy(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *a = embeddedCliGetToken(args, 1);

    if (a != NULL && strcmp(a, "center") == 0)
    {
        if (!T1SLink_SendToLemmy(0, 0)) { console_printf("lemmy: link down"); return; }
        console_printf("lemmy: neck=0 jaw=0");
        return;
    }

    const char *b = embeddedCliGetToken(args, 2);

    /* Nod control channel (0x88B9): enable/disable + tuning. */
    if (a != NULL && strcmp(a, "nod") == 0)
    {
        if (b == NULL || (strcmp(b, "on") != 0 && strcmp(b, "off") != 0))
        {
            console_printf("usage: lemmy nod <on|off>");
            return;
        }
        uint8_t on = (strcmp(b, "on") == 0) ? 1u : 0u;
        if (!T1SLink_SendLemmyCtrl(T1S_ANIM_CTRL_NOD_EN, on)) { console_printf("lemmy: link down"); return; }
        console_printf("lemmy: nod %s", on ? "on" : "off");
        return;
    }
    if (a != NULL && strcmp(a, "trim") == 0)
    {
        if (b == NULL) { console_printf("usage: lemmy trim <-127..127>"); return; }
        int8_t trim = parse_pos_i8(b);
        if (!T1SLink_SendLemmyCtrl(T1S_ANIM_CTRL_NOD_TRIM, (uint8_t)trim)) { console_printf("lemmy: link down"); return; }
        console_printf("lemmy: trim=%d", (int)trim);
        return;
    }
    if (a != NULL && strcmp(a, "osc") == 0)
    {
        if (b == NULL || (strcmp(b, "0") != 0 && strcmp(b, "1") != 0))
        {
            console_printf("usage: lemmy osc <0|1>");
            return;
        }
        uint8_t osc = (strcmp(b, "1") == 0) ? 1u : 0u;
        if (!T1SLink_SendLemmyCtrl(T1S_ANIM_CTRL_NOD_OSC, osc)) { console_printf("lemmy: link down"); return; }
        console_printf("lemmy: osc=%u", (unsigned)osc);
        return;
    }

    if (a == NULL || b == NULL)
    {
        console_printf("usage: lemmy <neck> <jaw> | center | nod <on|off> | trim <n> | osc <0|1>");
        return;
    }
    int8_t neck = parse_pos_i8(a);
    int8_t jaw  = parse_pos_i8(b);
    if (!T1SLink_SendToLemmy(neck, jaw)) { console_printf("lemmy: link down"); return; }
    console_printf("lemmy: neck=%d jaw=%d", (int)neck, (int)jaw);
}

static void cmd_lightshow(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *a = embeddedCliGetToken(args, 1);

    if (a == NULL || (strcmp(a, "on") != 0 && strcmp(a, "off") != 0))
    {
        console_printf("usage: lightshow <on|off>");
        return;
    }
    uint8_t on = (strcmp(a, "on") == 0) ? 1u : 0u;
    if (!T1SLink_SendLightshowCtrl(T1S_LIGHT_CTRL_OUTPUT_EN, on)) {
        console_printf("lightshow: link down");
        return;
    }
    console_printf("lightshow: output %s", on ? "on" : "off");
}

static void cmd_fretboard(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *a = embeddedCliGetToken(args, 1);

    if (a != NULL && (strcmp(a, "arm") == 0 || strcmp(a, "disarm") == 0)) {
        uint8_t on = (strcmp(a, "arm") == 0) ? 1u : 0u;
        if (!T1SLink_SendFretboardCtrl(T1S_DET_CTRL_ARM, on)) {
            console_printf("fretboard: link down");
            return;
        }
        console_printf("fretboard: actuation %s", on ? "armed" : "disarmed");
        return;
    }

    if (a != NULL && strcmp(a, "stream") == 0) {
        const char *b = embeddedCliGetToken(args, 2);
        if (b == NULL || (strcmp(b, "on") != 0 && strcmp(b, "off") != 0)) {
            console_printf("usage: fretboard stream <on|off>");
            return;
        }
        uint8_t on = (strcmp(b, "on") == 0) ? 1u : 0u;
        if (!T1SLink_SendFretboardCtrl(T1S_DET_CTRL_STREAM, on)) {
            console_printf("fretboard: link down");
            return;
        }
        console_printf("fretboard: data stream %s", on ? "on" : "off");
        return;
    }

    if (a != NULL && strcmp(a, "model") == 0) {
        /* Selection index must match the fretboard's MODEL_SEL_* enum
         * (easy/medium/hard/expert/auto). marvin doesn't share that header, so
         * the mapping is duplicated here. */
        static const char *const names[] = { "easy", "medium", "hard", "expert", "auto" };
        const char *b = embeddedCliGetToken(args, 2);
        if (b != NULL) {
            for (uint8_t i = 0u; i < (sizeof(names) / sizeof(names[0])); i++) {
                if (strcmp(b, names[i]) == 0) {
                    if (!T1SLink_SendFretboardCtrl(T1S_DET_CTRL_MODEL, i)) {
                        console_printf("fretboard: link down");
                        return;
                    }
                    console_printf("fretboard: model %s", names[i]);
                    return;
                }
            }
        }
        console_printf("usage: fretboard model <easy|medium|hard|expert|auto>");
        return;
    }

    console_printf("usage: fretboard <arm|disarm|stream on|off|model <difficulty>>");
}

/* The command table lives at file scope so CMD_COUNT can size the CLI's binding pool.
 * It used to be local to register_commands with maxBindingCount hard-coded alongside, and
 * the two drifted: 29 commands against a pool of 26 silently dropped the last three
 * (`gamma`, `qspi`, `settings`) — embeddedCliAddBinding just returns false, and the call
 * site discarded it. Deriving the pool from the table is what stops that recurring; the
 * configASSERT below is the backstop if it happens anyway. */
static const CliCommandBinding bindings[] = {
        { "status", "Print link / detector / mode / video state", false, NULL, cmd_status },
        { "t1s",    "Print T1S link / sync / PLCA / traffic counters",  false, NULL, cmd_t1s },
        { "nodes",  "List T1S nodes + heartbeat presence / last-seen",  false, NULL, cmd_nodes },
        { "sd",     "sd <info|ls|bench|mount|unmount> [arg]: SD-card bring-up", true, NULL, cmd_sd },
        { "health", "Print the per-task stack high-water + runtime table",     false, NULL, cmd_health },
        { "time",   "time [set YYYY-MM-DD HH:MM:SS]: read/set the RTC (UTC)",   true, NULL, cmd_time },
        { "player", "player [name]: show/set the current player",              true, NULL, cmd_player },
        { "scores", "scores <main|bonus> <index> [difficulty]: top scores",    true, NULL, cmd_scores },
        { "results","results add <set> <idx> <diff> <part> <score>: test row", true, NULL, cmd_results },
        { "catalog","catalog <reload|ls|<main|bonus> <index>>: song labels",    true, NULL, cmd_catalog },
        { "art",    "art [ls | <main|bonus> <index>]: album-art cache status",  true, NULL, cmd_art },
        { "detect", "detect cv <on|off>: enable/disable the CV detector",        true, NULL, cmd_detect },
        { "active", "active <cv|fretboard>: hand game control to a detector",     true, NULL, cmd_active },
        { "cvcfg",  "cvcfg <1p|2pl>: select CV detector highway geometry",  true, NULL, cmd_cvcfg },
        { "timing", "timing <on|off>: chord/strum scheduler output enable", true, NULL, cmd_timing },
        { "manual", "manual <on|off>: manual-control actuation mode",      true, NULL, cmd_manual },
        { "play",   "play [attach|stop|status]: auto-navigate + CV-play the selected song; 'attach' = play a manually-started game (e.g. 2p)", true, NULL, cmd_play },
        { "fauxmote","fauxmote [status|pair|stop|reconnect|unlink|ext <on|off>|btn <mask>|pointer <x> <y>|off]", true, NULL, cmd_fauxmote },
        { "lemmy",  "lemmy <neck> <jaw>|center: servo pos; nod <on|off>|trim <n>|osc <0|1>: nod control", true, NULL, cmd_lemmy },
        { "lightshow","lightshow <on|off>: enable/disable the LED output",   true, NULL, cmd_lightshow },
        { "fretboard","fretboard <arm|disarm|stream on|off|model <difficulty>>: gate actuation / data stream, select inference model", true, NULL, cmd_fretboard },
        { "fret",   "fret <g|r|y|b|o> <0|1>: press/release a fret",        true, NULL, cmd_fret },
        { "strum",  "strum <down|up>: one strum pulse",                    true, NULL, cmd_strum },
        { "backlight","backlight <0-100>: set LCD backlight brightness %",  true, NULL, cmd_backlight },
        { "perf",     "perf [dump <canvas> [x y w h]]: perf-log state, or request a canvas dump", true, NULL, cmd_perf },
        { "nav",      "nav [slide on|off | icon <row> | px <x> <y> [w h]]: drawer slide / pixel dump", true, NULL, cmd_nav },
        { "bus",     "bus probe [iters]: render cost of the gauge / sparkline / TX bars", true, NULL, cmd_bus },
        { "wiimotes","wiimotes probe [iters]: render cost of the whammy / tilt / fret widgets", true, NULL, cmd_wiimotes },
        { "gamma",  "gamma <on|off>: toggle HEO video levels expansion (A/B)", true, NULL, cmd_gamma },
        { "titlebar","titlebar [pulse <on|off> | tiles <on|off> | probe [iters]]: metric-tile / LED render load, frames/s, per-frame cost", true, NULL, cmd_titlebar },
        { "qspi",   "qspi [bench [MB]|verify [KB] [passes]]: SST26 smoke / bench / integrity stress", true, NULL, cmd_qspi },
        { "settings","settings [dump|save|wipe|stress [n]]: persistent settings (QSPI)", true, NULL, cmd_settings },
};
#define CMD_COUNT  (sizeof(bindings) / sizeof(bindings[0]))

static void register_commands(void)
{
    for (size_t i = 0u; i < CMD_COUNT; i++)
    {
        /* A false return means the binding pool is full: this command and every one after
         * it is absent from `help` and unreachable at the prompt. Reported at ERROR rather
         * than asserted because configASSERT is a no-op in this build (FreeRTOSConfig.h
         * never defines it) — and wrapping the call in one would delete the call. */
        if (!embeddedCliAddBinding(s_cli, bindings[i]))
        {
            LOG_ERROR("CON: binding pool full (%u) — '%s' and later commands dropped\r\n",
                      (unsigned)CMD_COUNT, bindings[i].name);
            return;
        }
    }
}

/* ---- transport ---------------------------------------------------------- */

static void rx_event_handler(FLEXCOM_USART_EVENT event, uintptr_t context)
{
    (void)context;
    BaseType_t hpw = pdFALSE;

    switch (event)
    {
        case FLEXCOM_USART_EVENT_READ_THRESHOLD_REACHED:
        case FLEXCOM_USART_EVENT_READ_BUFFER_FULL:
            (void)xSemaphoreGiveFromISR(s_rx_notify, &hpw);
            break;
        case FLEXCOM_USART_EVENT_READ_ERROR:
            (void)FLEXCOM2_USART_ErrorGet();
            (void)xSemaphoreGiveFromISR(s_rx_notify, &hpw);
            break;
        default:
            break;
    }

    portYIELD_FROM_ISR(hpw);
}

static void console_task(void *param)
{
    (void)param;

    LOG_INFO("CON: console started (FLEXCOM2)\r\n");

    for (;;)
    {
        if (FLEXCOM2_USART_ReadCountGet() == 0u)
        {
            (void)xSemaphoreTake(s_rx_notify, pdMS_TO_TICKS(CON_RX_WAIT_MS));
        }

        uint8_t c;
        while (FLEXCOM2_USART_Read(&c, 1u) == 1u)
        {
            embeddedCliReceiveChar(s_cli, (char)c);
        }

        embeddedCliProcess(s_cli);
    }
}

void Console_Initialize(void)
{
    EmbeddedCliConfig *cfg = embeddedCliDefaultConfig();
    cfg->invitation        = "marvin> ";
    cfg->rxBufferSize      = 64u;
    cfg->cmdBufferSize     = 64u;
    cfg->historyBufferSize = 128u;
    cfg->maxBindingCount   = (uint16_t)CMD_COUNT;
    cfg->enableAutoComplete = true;
    cfg->cliBuffer         = s_cli_buf;
    cfg->cliBufferSize     = sizeof(s_cli_buf);

    /* Both of these were configASSERTs, which are compiled out here — so an undersized
     * buffer made embeddedCliNew return NULL and the next line dereference it, i.e. a data
     * abort at boot with nothing logged. Report and give up instead: no console is
     * recoverable, a data abort is not. */
    if (embeddedCliRequiredSize(cfg) > sizeof(s_cli_buf))
    {
        LOG_ERROR("CON: cli buffer too small (%u < %u) — console disabled\r\n",
                  (unsigned)sizeof(s_cli_buf), (unsigned)embeddedCliRequiredSize(cfg));
        return;
    }

    s_cli = embeddedCliNew(cfg);
    if (s_cli == NULL)
    {
        LOG_ERROR("CON: embeddedCliNew failed — console disabled\r\n");
        return;
    }
    s_cli->writeChar = console_write_char;

    register_commands();

    s_rx_notify = xSemaphoreCreateBinaryStatic(&s_rx_notify_buf);
    configASSERT(s_rx_notify != NULL);

    /* Arm continuous RX: the ring fills from the FLEXCOM2 ISR; a persistent
     * 1-byte threshold notification wakes console_task on each inbound byte. */
    FLEXCOM2_USART_ReadCallbackRegister(rx_event_handler, 0u);
    FLEXCOM2_USART_ReadThresholdSet(CON_RX_THRESHOLD);
    (void)FLEXCOM2_USART_ReadNotificationEnable(true, true);

    (void)xTaskCreateStatic(console_task,
                            "Console",
                            CON_TASK_STACK_WORDS,
                            NULL,
                            CON_TASK_PRIORITY,
                            s_task_stack,
                            &s_task_tcb);
}
