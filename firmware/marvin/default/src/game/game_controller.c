#include "game_controller.h"

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "log.h"
#include "selection.h"
#include "gameplay_engine.h"
#include "gameplay_metadata.h"
#include "actuator/timing_pipeline.h"
#include "actuator/manual_control.h"
#include "actuator/fretboard_link.h"
#include "perf_log/perf_log_records.h"

#if (MARVIN_FRETBOARD_TRANSPORT == FRETBOARD_TRANSPORT_T1S)
#include "net/fauxmote/fauxmote_link.h"   /* pre-flight: ensure the Wii link is up */
#include "net/fauxmote/mf_proto.h"
#endif

#define GC_TASK_STACK_WORDS   768u
#define GC_TASK_PRIORITY      4u

/* Menu-input masks (guitar fret/strum bits, timing_pipeline.h layout). */
#define GC_GREEN        TIMING_BIT_GREEN        /* confirm / enter */
#define GC_RED          TIMING_BIT_RED          /* back up one level */
#define GC_YELLOW       TIMING_BIT_YELLOW       /* song_select: main setlist */
#define GC_BLUE         TIMING_BIT_BLUE         /* song_select: bonus setlist */
#define GC_STRUM_DOWN   TIMING_BIT_STRUM_DOWN   /* move selection down */
#define GC_STRUM_UP     TIMING_BIT_STRUM_UP     /* move selection up */

/* Input pulse: menus auto-repeat a *held* strum, so press briefly then release.
 * Kept deliberately unhurried — GH3 drops inputs that arrive too fast, and each
 * strum must read as a discrete edge (not a held auto-repeat). */
#define GC_PULSE_MS         70u    /* bit asserted (edge for GH3 to register) */
#define GC_GAP_MS           180u   /* released gap between inputs (keeps them discrete) */
/* Settle used only for the RED/recover pauses. */
#define GC_SETTLE_MS        500u
/* Force + wait for a fresh classification (RequestObservation → next frame). */
#define GC_OBS_WAIT_MS      140u
/* Poll bounds. */
#define GC_STEP_POLL_MS     150u
#define GC_STEP_TIMEOUT_MS  4000u  /* wait for a menu transition after a step */
#define GC_LOADING_TIMEOUT_MS  20000u
#define GC_PLAY_POLL_MS     300u
#define GC_CONNECT_TIMEOUT_MS  12000u  /* wait for fauxmote↔Wii reconnect */

/* Budgets (mirror the offline NavController). */
#define GC_MAX_ITERS        60
#define GC_MAX_RECOVER      12
#define GC_MAX_MOVE         64
#define GC_MAX_EXIT_ITERS   16   /* menu hops to reach main_menu (anchor / song-end exit) */

typedef enum { ACT_SELECT_INDEX, ACT_SELECT_SONG, ACT_SATURATE_TOP, ACT_WAIT } gc_act_t;

typedef struct
{
    uint8_t     from;    /* GP_SCREEN_* this step applies to */
    gc_act_t    act;
    int16_t     index;   /* target cell for ACT_SELECT_INDEX */
    uint8_t     to;      /* GP_SCREEN_* expected next */
    const char *desc;
} gc_step_t;

static StackType_t   s_stack[GC_TASK_STACK_WORDS];
static StaticTask_t  s_tcb;
static SemaphoreHandle_t s_start_sig;
static StaticSemaphore_t s_start_sig_buf;

static volatile bool s_busy;
static volatile bool s_stop_req;
static void (*s_status_cb)(const char *);

static gc_step_t s_plan[8];
static int       s_plan_len;

static void status(const char *s)
{
    LOG_INFO("GC: %s\r\n", s);
    if (s_status_cb != NULL) { s_status_cb(s); }
}

static int gc_abs(int v) { return v < 0 ? -v : v; }

/* ── observation ──────────────────────────────────────────────────────────── */

/* Force a fresh classification and return the resulting {screen, selection}.
 * false only before the first classified frame ever. */
static bool observe(uint8_t *screen, int16_t *sel)
{
    GameplayEngine_RequestObservation();
    vTaskDelay(pdMS_TO_TICKS(GC_OBS_WAIT_MS));
    game_state_t gs;
    if (!GameplayEngine_GetLatest(&gs)) { return false; }
    *screen = gs.screen;
    *sel    = gs.selection;
    return true;
}

/* ── actuation ────────────────────────────────────────────────────────────── */

static void send_input(uint8_t mask)
{
    FretboardLink_Send(mask, (uint8_t)PERF_ACTUATOR_PRODUCER_GAMEPLAY);
    vTaskDelay(pdMS_TO_TICKS(GC_PULSE_MS));
    FretboardLink_Send(0u, (uint8_t)PERF_ACTUATOR_PRODUCER_GAMEPLAY);
    vTaskDelay(pdMS_TO_TICKS(GC_GAP_MS));
}

/* Move a static-list cursor to an absolute index from the observed position,
 * then confirm. Signed-delta strum is sticky-default-safe. */
static void select_to(int target, int16_t cur)
{
    if (cur < 0) { return; }   /* selection unreadable — let the loop retry/recover */
    int delta = target - (int)cur;
    uint8_t move = (delta > 0) ? GC_STRUM_DOWN : GC_STRUM_UP;
    int n = gc_abs(delta);
    if (n > GC_MAX_MOVE) { n = GC_MAX_MOVE; }
    for (int i = 0; i < n; i++) { send_input(move); }
    send_input(GC_GREEN);
}

/* song_select is fixed-slot: `sel` is a gp_song_templates[] index. Toggle setlist
 * if needed, then strum the signed ordinal delta to the target song. */
static void select_song(int16_t tmpl)
{
    const selection_t *want = Selection_Get();
    if (tmpl < 0 || tmpl >= GP_N_SONGS) { return; }

    if (gp_song_templates[tmpl].setlist != want->setlist)
    {
        send_input((want->setlist == GP_SETLIST_BONUS) ? GC_BLUE : GC_YELLOW);
        vTaskDelay(pdMS_TO_TICKS(GC_SETTLE_MS));
        uint8_t sc;
        if (!observe(&sc, &tmpl) || tmpl < 0 || tmpl >= GP_N_SONGS) { return; }
    }

    int delta = (int)want->index - (int)gp_song_templates[tmpl].index;
    uint8_t move = (delta > 0) ? GC_STRUM_DOWN : GC_STRUM_UP;
    int n = gc_abs(delta);
    if (n > GC_MAX_MOVE) { n = GC_MAX_MOVE; }
    for (int i = 0; i < n; i++) { send_input(move); }
    send_input(GC_GREEN);
}

/* Strum up until the selection stops moving (top), then confirm — robust to a
 * sticky entry position (FULL SONG / FULL SPEED = index 0). */
static void saturate_top(void)
{
    int16_t prev = -999;
    for (int i = 0; i < GC_MAX_MOVE; i++)
    {
        uint8_t sc; int16_t cur;
        if (!observe(&sc, &cur)) { break; }
        if (cur == 0 || cur == prev) { break; }
        prev = cur;
        send_input(GC_STRUM_UP);
    }
    send_input(GC_GREEN);
}

static bool wait_for(uint8_t target, uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    for (;;)
    {
        uint8_t sc; int16_t sel;
        if (observe(&sc, &sel) && sc == target) { return true; }
        if (s_stop_req) { return false; }
        if ((TickType_t)(xTaskGetTickCount() - start) > pdMS_TO_TICKS(timeout_ms)) { return false; }
        vTaskDelay(pdMS_TO_TICKS(GC_STEP_POLL_MS));
    }
}

/* Block until the observed screen leaves `from` (the transition completed) or a
 * timeout. This is what keeps a step from re-firing on a not-yet-transitioned
 * screen — and prevents a stray confirm/strum from leaking onto the next screen. */
static bool wait_screen_change(uint8_t from, uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    for (;;)
    {
        uint8_t sc; int16_t sel;
        if (observe(&sc, &sel) && sc != from) { return true; }
        if (s_stop_req) { return false; }
        if ((TickType_t)(xTaskGetTickCount() - start) > pdMS_TO_TICKS(timeout_ms)) { return false; }
        vTaskDelay(pdMS_TO_TICKS(GC_STEP_POLL_MS));
    }
}

/* ── plan ─────────────────────────────────────────────────────────────────── */

static void build_plan(const selection_t *sel)
{
    int n = 0;
    s_plan[n++] = (gc_step_t){ GP_SCREEN_main_menu,        ACT_SELECT_INDEX, 4, GP_SCREEN_training_menu,    "TRAINING" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_training_menu,    ACT_SELECT_INDEX, 1, GP_SCREEN_song_select,      "PRACTICE" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_song_select,      ACT_SELECT_SONG,  0, GP_SCREEN_part_select,      "song" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_part_select,      ACT_SELECT_INDEX, 0, GP_SCREEN_difficulty_select,"LEAD" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_difficulty_select,ACT_SELECT_INDEX, (int16_t)sel->difficulty, GP_SCREEN_section_select, "difficulty" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_section_select,   ACT_SATURATE_TOP, 0, GP_SCREEN_speed_select,     "FULL SONG" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_speed_select,     ACT_SATURATE_TOP, 0, GP_SCREEN_loading,          "FULL SPEED" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_loading,          ACT_WAIT,         0, GP_SCREEN_in_song,          "loading" };
    s_plan_len = n;
}

static const gc_step_t *step_for(uint8_t screen)
{
    for (int i = 0; i < s_plan_len; i++)
    {
        if (s_plan[i].from == screen) { return &s_plan[i]; }
    }
    return NULL;
}

static void execute(const gc_step_t *st, int16_t sel)
{
    switch (st->act)
    {
        case ACT_SELECT_INDEX:  select_to(st->index, sel);              break;
        case ACT_SELECT_SONG:   select_song(sel);                       break;
        case ACT_SATURATE_TOP:  saturate_top();                         break;
        case ACT_WAIT:          wait_for(st->to, GC_LOADING_TIMEOUT_MS); break;
        default:                                                        break;
    }
}

/* ── run ──────────────────────────────────────────────────────────────────── */

/* Release the wire, disable CV, report a terminal status. */
static void finish(const char *st)
{
    TimingPipeline_SetEnabled(false);   /* also sends one release */
    status(st);
    s_busy = false;
}

/* Return to main_menu from wherever we are. RED backs up the forward-nav menus,
 * but the terminal overlays (practice-end / quit-confirm / pause) don't reliably
 * accept RED, so QUIT out of those explicitly. Used both to anchor a run and to
 * clear the end-of-song menus when a run finishes. */
static bool nav_to_main_menu(void)
{
    for (int i = 0; i < GC_MAX_EXIT_ITERS; i++)
    {
        uint8_t sc; int16_t sel;
        if (!observe(&sc, &sel)) { vTaskDelay(pdMS_TO_TICKS(GC_SETTLE_MS)); continue; }
        if (sc == GP_SCREEN_main_menu) { return true; }
        if (s_stop_req) { return false; }

        switch (sc)
        {
            case GP_SCREEN_practice_end_menu: select_to(4, sel); break;  /* QUIT → main_menu */
            case GP_SCREEN_quit_confirm:      select_to(1, sel); break;  /* QUIT (confirm) → main_menu */
            case GP_SCREEN_pause_menu:        select_to(6, sel); break;  /* QUIT → quit_confirm */
            default:                          send_input(GC_RED); break; /* back up one level */
        }
        (void)wait_screen_change(sc, GC_STEP_TIMEOUT_MS);
    }
    uint8_t sc; int16_t sel;
    return observe(&sc, &sel) && sc == GP_SCREEN_main_menu;
}

/* CV plays; hold until the song ends (practice_end_menu), we leave gameplay, or
 * Stop is requested. Observation is request-triggered, so poll via observe() — a
 * classify during in_song is cheap (no song-match) and keeps the in_song gate
 * (GameplayEngine_CurrentScreen) fresh for the timing pipeline. */
static void play_until_done(void)
{
    status("PLAYING");
    TimingPipeline_SetEnabled(true);   /* in_song gate arms actuation automatically */

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(GC_PLAY_POLL_MS));
        if (s_stop_req) { break; }
        uint8_t sc; int16_t sel;
        if (observe(&sc, &sel) && sc != GP_SCREEN_in_song
            && sc != GP_SCREEN_loading && sc != GP_SCREEN_UNKNOWN)
        {
            break;   /* song ended (practice_end_menu) or left gameplay */
        }
    }

    /* Leave GH3 on the end screen; just release CV and go idle. The next run's
     * anchor (nav_to_main_menu) QUITs out of the end/pause menus when START is
     * pressed again. */
    finish("READY");
}

/* Make sure fauxmote is connected to the Wii before a run (it's the actuation path
 * to the console). Reconnect if the link is bonded-but-down; sets a descriptive
 * status and returns false if it can't get there. */
#if (MARVIN_FRETBOARD_TRANSPORT == FRETBOARD_TRANSPORT_T1S)
static bool ensure_wii_connected(void)
{
    uint8_t flags;
    if (!Fauxmote_GetStatus(&flags, NULL, NULL, NULL, NULL))
    {
        /* No STATUS from fauxmote (UART link down / not running) — can't manage the
         * BT link here; proceed so a T1S guitar can still actuate. */
        LOG_WARN("GC: no fauxmote status; skipping Wii connect check\r\n");
        return true;
    }
    if (flags & MF_ST_CONNECTED) { return true; }
    if (!(flags & MF_ST_BONDED))
    {
        status("PAIR WII");   /* never synced — needs a manual red-SYNC pairing */
        return false;
    }

    status("CONNECTING");
    Fauxmote_SendCmd(MF_CMD_RECONNECT);
    TickType_t start = xTaskGetTickCount();
    while ((TickType_t)(xTaskGetTickCount() - start) < pdMS_TO_TICKS(GC_CONNECT_TIMEOUT_MS))
    {
        if (s_stop_req) { return false; }
        vTaskDelay(pdMS_TO_TICKS(500));
        if (Fauxmote_GetStatus(&flags, NULL, NULL, NULL, NULL) && (flags & MF_ST_CONNECTED))
        {
            return true;
        }
    }
    status("NO WII");
    return false;
}
#else
static bool ensure_wii_connected(void) { return true; }
#endif

static void run(void)
{
    const selection_t *sel = Selection_Get();
    if (!sel->valid)
    {
        status("NO SONG");
        return;
    }

    s_busy = true;

    /* Pre-flight: the Wii link must be up (fauxmote is how we reach the console). */
    if (!ensure_wii_connected())
    {
        s_busy = false;   /* status already set (CONNECTING→NO WII / PAIR WII) */
        return;
    }

    status("NAVIGATING");

    /* Own the wire: manual off, timing off (set timing off *after* manual, since
     * ManualControl_SetEnabled(false) flips timing on — the arbitration trap). */
    if (ManualControl_IsEnabled()) { ManualControl_SetEnabled(false); }
    TimingPipeline_SetEnabled(false);

    if (!nav_to_main_menu())
    {
        finish(s_stop_req ? "READY" : "FAILED");
        return;
    }

    build_plan(sel);

    int recover = 0;
    for (int iter = 0; iter < GC_MAX_ITERS; iter++)
    {
        if (s_stop_req) { finish("READY"); return; }

        uint8_t sc; int16_t s;
        if (!observe(&sc, &s)) { continue; }

        if (sc == GP_SCREEN_in_song)
        {
            play_until_done();
            return;
        }

        const gc_step_t *st = step_for(sc);
        if (st != NULL)
        {
            LOG_INFO("GC: %s\r\n", st->desc);
            execute(st, s);
            /* Wait for the transition off this screen before re-evaluating, so the
             * same step can't re-fire (and a stray input can't hit the next screen).
             * ACT_WAIT already blocked until its target screen. */
            if (st->act != ACT_WAIT)
            {
                (void)wait_screen_change(st->from, GC_STEP_TIMEOUT_MS);
            }
            recover = 0;
        }
        else
        {
            if (++recover > GC_MAX_RECOVER) { finish("FAILED"); return; }
            LOG_INFO("GC: recover (RED) from screen %u\r\n", (unsigned)sc);
            send_input(GC_RED);
            (void)wait_screen_change(sc, GC_STEP_TIMEOUT_MS);
        }
    }
    finish("FAILED");   /* step budget exhausted */
}

static void gc_task(void *param)
{
    (void)param;
    for (;;)
    {
        if (xSemaphoreTake(s_start_sig, portMAX_DELAY) != pdTRUE) { continue; }
        run();
    }
}

/* ── public API ───────────────────────────────────────────────────────────── */

void GameController_Initialize(void)
{
    s_start_sig = xSemaphoreCreateBinaryStatic(&s_start_sig_buf);
    configASSERT(s_start_sig != NULL);
    (void)xTaskCreateStatic(gc_task, "GameCtl", GC_TASK_STACK_WORDS,
                            NULL, GC_TASK_PRIORITY, s_stack, &s_tcb);
}

void GameController_Start(void)
{
    if (s_busy)
    {
        LOG_WARN("GC: busy — ignoring start\r\n");
        return;
    }
    s_stop_req = false;
    (void)xSemaphoreGive(s_start_sig);
}

void GameController_Stop(void)
{
    s_stop_req = true;
}

bool GameController_IsBusy(void)
{
    return s_busy;
}

void GameController_SetStatusObserver(void (*cb)(const char *))
{
    s_status_cb = cb;
}
