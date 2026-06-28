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

#include "actuator/timing_pipeline.h"
#include "actuator/manual_control.h"
#include "actuator/fretboard_link.h"
#include "detector/detector.h"
#include "video/video.h"
#include "game/fret.h"
#include "net/t1s/t1s_link.h"
#include "storage/storage.h"
#include "results/results.h"
#include "game/catalog.h"
#include "ui/ui_manager.h"
#include "ui/widgets/song_list/widget_song_list_demo.h"
#include "flash/qspi_smoke.h"
#include "flash/settings.h"

#define CON_TASK_STACK_WORDS  1024u
#define CON_TASK_PRIORITY     2u      /* low / UI band — human-interactive */

#define CON_RX_THRESHOLD      1u      /* wake on any inbound byte */
#define CON_RX_WAIT_MS        100u    /* bounded so a missed notify can't wedge */

/* embedded-cli internal buffer (static-allocation mode → no malloc). Sized
 * generously for the config below; the actual requirement is asserted at init,
 * so bump this if the assert ever fires after a config change. */
static CLI_UINT s_cli_buf[BYTES_TO_CLI_UINTS(1024)];

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
    if (strcmp(s, "adc") == 0) { return DETECTOR_ADC_FRETBOARD; }
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
    console_printf("active:     %s", (act == DETECTOR_CV_MARVIN_V1) ? "cv" : "adc");
    console_printf("detect cv:  %s", Detector_IsEnabled(DETECTOR_CV_MARVIN_V1)  ? "on" : "off");
    console_printf("detect adc: %s", Detector_IsEnabled(DETECTOR_ADC_FRETBOARD) ? "on" : "off");
    console_printf("manual:     %s", ManualControl_IsEnabled() ? "on" : "off");
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
}

static void cmd_nodes(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)args; (void)ctx;

    uint8_t n = T1SLink_NodeTableCount();
    console_printf("id  type      present  last-hb");
    for (uint8_t i = 0u; i < n; i++)
    {
        T1SLink_NodeInfo ni;
        if (T1SLink_GetNodeInfo(i, &ni))
        {
            console_printf("%-3u %-9s %-7s  %lums",
                           (unsigned)ni.node_id, ni.type,
                           ni.present ? "yes" : "no",
                           (unsigned long)ni.age_ms);
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
        bool ok = Catalog_Reload();
        console_printf("%s; %d song(s) cached", ok ? "reloaded" : "no catalog",
                       Catalog_Count());
        return;
    }
    if (strcmp(sub, "ls") == 0)
    {
        int n = Catalog_Count();
        if (n == 0) { console_printf("catalog empty (try: catalog reload)"); return; }
        for (int i = 0; i < n; i++)
        {
            const catalog_entry_t *e = Catalog_At(i);
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

    catalog_entry_t e;
    if (!Catalog_Lookup((uint8_t)sl, (uint8_t)parse_u32(idx, 0u), &e))
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

static void cmd_songlist(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    /* Bring-up: attach the SongList widget (catalog-backed) to the live screen. */
    const char *sub = embeddedCliGetToken(args, 1);
    if (sub != NULL && strcmp(sub, "stats") == 0)
    {
        uint32_t dc = 0; int sel = -1;
        if (SongList_DemoStats(&dc, &sel))
        {
            console_printf("songlist: drawCount=%lu selected=%d", (unsigned long)dc, sel);
        }
        else
        {
            console_printf("songlist: not attached (run `songlist` first)");
        }
    }
    else if (sub != NULL && strcmp(sub, "off") == 0)
    {
        SongList_DemoDetach();
        console_printf("songlist: removed, dashboard restored");
    }
    else if (sub != NULL && strcmp(sub, "hide") == 0)
    {
        SongList_DemoSetDashboard(false);
        console_printf("songlist: dashboard hidden");
    }
    else if (sub != NULL && strcmp(sub, "show") == 0)
    {
        SongList_DemoSetDashboard(true);
        console_printf("songlist: dashboard shown");
    }
    else if (sub != NULL && strcmp(sub, "solid") == 0)
    {
        bool on = SongList_DemoToggleFill();
        console_printf("songlist: solid-fill %s", on ? "ON (magenta)" : "off");
    }
    else if (sub != NULL && strcmp(sub, "tree") == 0)
    {
        int cnt = 0; int idx = SongList_DemoZOrder(&cnt);
        if (idx < 0) { console_printf("songlist: not attached"); }
        else { console_printf("songlist: z-index %d of %d (topmost=%s)",
                              idx, cnt, (idx == cnt - 1) ? "yes" : "NO"); }
    }
    else
    {
        SongList_DemoAttach();
        console_printf("songlist: attach requested");
    }
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

static void cmd_detect(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    int id  = parse_detector(embeddedCliGetToken(args, 1));
    int val = parse_onoff(embeddedCliGetToken(args, 2));
    if (id < 0 || val < 0)
    {
        console_printf("usage: detect <cv|adc> <on|off>");
        return;
    }
    if (val) { Detector_Enable((detector_id_t)id); }
    else     { Detector_Disable((detector_id_t)id); }
    console_printf("detect %s = %s", (id == DETECTOR_CV_MARVIN_V1) ? "cv" : "adc",
                   val ? "on" : "off");
}

static void cmd_active(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    int id = parse_detector(embeddedCliGetToken(args, 1));
    if (id < 0)
    {
        console_printf("usage: active <cv|adc>");
        return;
    }
    Detector_SetActive((detector_id_t)id);
    console_printf("active = %s", (id == DETECTOR_CV_MARVIN_V1) ? "cv" : "adc");
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
    TimingPipeline_SetEnabled(val != 0);
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

static void register_commands(void)
{
    static const CliCommandBinding bindings[] = {
        { "status", "Print link / detector / mode / video state", false, NULL, cmd_status },
        { "t1s",    "Print T1S link / sync / PLCA / traffic counters",  false, NULL, cmd_t1s },
        { "nodes",  "List T1S nodes + heartbeat presence / last-seen",  false, NULL, cmd_nodes },
        { "sd",     "sd <info|ls|bench|mount|unmount> [arg]: SD-card bring-up", true, NULL, cmd_sd },
        { "time",   "time [set YYYY-MM-DD HH:MM:SS]: read/set the RTC (UTC)",   true, NULL, cmd_time },
        { "player", "player [name]: show/set the current player",              true, NULL, cmd_player },
        { "scores", "scores <main|bonus> <index> [difficulty]: top scores",    true, NULL, cmd_scores },
        { "results","results add <set> <idx> <diff> <part> <score>: test row", true, NULL, cmd_results },
        { "catalog","catalog <reload|ls|<main|bonus> <index>>: song labels",    true, NULL, cmd_catalog },
        { "songlist","songlist [off|hide|show|solid|tree|stats]: song-list widget bring-up", true, NULL, cmd_songlist },
        { "detect", "detect <cv|adc> <on|off>: enable/disable a detector", true, NULL, cmd_detect },
        { "active", "active <cv|adc>: select the actuated detector",       true, NULL, cmd_active },
        { "timing", "timing <on|off>: marvin chord/strum scheduler",       true, NULL, cmd_timing },
        { "manual", "manual <on|off>: manual-control actuation mode",      true, NULL, cmd_manual },
        { "fret",   "fret <g|r|y|b|o> <0|1>: press/release a fret",        true, NULL, cmd_fret },
        { "strum",  "strum <down|up>: one strum pulse",                    true, NULL, cmd_strum },
        { "backlight","backlight <0-100>: set LCD backlight brightness %",  true, NULL, cmd_backlight },
        { "qspi",   "qspi [bench [MB]|verify [KB] [passes]]: SST26 smoke / bench / integrity stress", true, NULL, cmd_qspi },
        { "settings","settings [dump|save|wipe|stress [n]]: persistent settings (QSPI)", true, NULL, cmd_settings },
    };
    for (size_t i = 0u; i < (sizeof(bindings) / sizeof(bindings[0])); i++)
    {
        (void)embeddedCliAddBinding(s_cli, bindings[i]);
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
    cfg->maxBindingCount   = 24u;
    cfg->enableAutoComplete = true;
    cfg->cliBuffer         = s_cli_buf;
    cfg->cliBufferSize     = sizeof(s_cli_buf);

    configASSERT(embeddedCliRequiredSize(cfg) <= sizeof(s_cli_buf));

    s_cli = embeddedCliNew(cfg);
    configASSERT(s_cli != NULL);
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
