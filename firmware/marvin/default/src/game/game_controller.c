#include "game_controller.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "log.h"
#include "game_selection.h"
#include "game_engine.h"
#include "gameplay_metadata.h"
#include "game/game_timing.h"
#include "game/game_catalog.h"        /* per-song nod trim for lemmy; song title for results */
#include "results/results.h"          /* persist the human's score at song end */
#include "actuator/manual_control.h"
#include "actuator/fretboard_link.h"
#include "actuator/guitar_cmd.h"
#include "actuator/actuator_enable.h" /* performance window: lemmy + lightshow */
#include "net/t1s/t1s_link.h"         /* lemmy nod control channel */
#include "detector/cv_marvin_v1.h"   /* highway geometry + play difficulty */
#include "detector/detector.h"       /* active-detector fallback when NN can't play */
#include "perf_log/perf_log_records.h"
#include "ui/dashboard_feed.h"   /* playtime → dashboard progress bar */
#include "net/fauxmote/fauxmote_link.h"   /* pre-flight: ensure the Wii link is up */
#include "net/fauxmote/mf_proto.h"

/* 4 KB: the run's terminal path calls Results_Append, whose locals are ~530 bytes
 * (line/path/dir/song/ts buffers + a SYS_FS_FSTAT) on top of FatFs beneath it, and
 * it lands on an already-deep call chain. The console task calls the same function
 * with 1024 words. Actual headroom is observable — the perf log reports task stack
 * high-water. */
#define GC_TASK_STACK_WORDS   1024u
#define GC_TASK_PRIORITY      4u

/* Menu-input masks (guitar fret/strum bits, timing_pipeline.h layout). */
#define GC_GREEN        GUITAR_BTN_GREEN        /* confirm / enter */
#define GC_RED          GUITAR_BTN_RED          /* back up one level */
#define GC_YELLOW       GUITAR_BTN_YELLOW       /* song_select: main setlist */
#define GC_BLUE         GUITAR_BTN_BLUE         /* song_select: bonus setlist */
#define GC_STRUM_DOWN   GUITAR_BTN_STRUM_DOWN   /* move selection down */
#define GC_STRUM_UP     GUITAR_BTN_STRUM_UP     /* move selection up */

/* Input pulse: menus auto-repeat a *held* strum, so press briefly then release.
 * Kept deliberately unhurried — GH3 drops inputs that arrive too fast, and each
 * strum must read as a discrete edge (not a held auto-repeat). */
#define GC_PULSE_MS         70u    /* bit asserted (edge for GH3 to register) */
#define GC_GAP_MS           180u   /* released gap between inputs (keeps them discrete) */
/* Settle used only for the RED/recover pauses and setlist toggles. */
#define GC_SETTLE_MS        500u
/* Blocking wait for one synchronous observation (classify of a fresh frame). */
#define GC_OBS_TIMEOUT_MS   800u   /* covers song_select's ~190 ms soft-float match */
/* Poll bounds. */
#define GC_STEP_POLL_MS     150u
#define GC_STEP_TIMEOUT_MS  4000u  /* wait for a menu transition after a step */
/* Budget for the post-step wait that lets a confirmed screen actually change. Longer
 * than GC_STEP_TIMEOUT_MS because some GH3 transitions are genuinely slow — confirming
 * the multiplayer mode takes a few seconds to bring up character select (Greg,
 * measured on hardware) — and the cost of being too short is not a delay but a
 * *repeated input*: the wait lapses, the loop re-observes the same screen, and the
 * step re-fires a strum + GREEN into a menu that has already been committed. Only
 * bounds how long a genuinely stuck screen takes to reach the retry path. */
#define GC_TRANSITION_MS    10000u
#define GC_LOADING_TIMEOUT_MS  20000u
#define GC_PLAY_POLL_MS     300u
#define GC_CONNECT_TIMEOUT_MS  12000u  /* wait for fauxmote↔Wii reconnect */
#define GC_ATTACH_WAIT_MS   120000u    /* attach mode: how long to wait for a gameplay screen */
/* How long to wait on a step whose transition needs a human (2-player setup: the
 * screen advances only once the second player confirms their own side). Human-paced,
 * so it is the same order as GC_ATTACH_WAIT_MS rather than GC_STEP_TIMEOUT_MS.
 * STOP is honoured throughout the wait, and a timeout ends the run with a status of
 * its own — never the RED-recovery path, which would back marvin out of the setup
 * flow while the player was still deciding. */
#define GC_PLAYER_WAIT_MS   120000u

/* Budgets (mirror the offline NavController). */
#define GC_MAX_ITERS        60
#define GC_MAX_RECOVER      12
#define GC_MAX_MOVE         64
#define GC_MAX_EXIT_ITERS   16   /* menu hops to reach main_menu (anchor / song-end exit) */
#define GC_MAX_CONFIRM      4    /* re-observe/re-move tries to land a cursor before GREEN */
#define GC_MAX_CONFIRM_FAIL 4    /* consecutive un-confirmable steps before FAILED */
#define GC_SATURATE_STRUMS  24   /* max strum-ups to drive a readable list to its top */
/* Consecutive unreadable observations tolerated before giving up on a plan step.
 * UNKNOWN is *not* evidence of being off-plan — menus animate, and some transient
 * states have no corpus exemplar at all (the 2-player character/ready screens advance
 * per side, so the frame between them is genuinely unmodelled). Pressing RED on an
 * unreadable frame navigates *backwards*, undoing marvin's own progress: on hardware
 * that turned the character-select hop into a GREEN/RED loop that never escaped. So
 * UNKNOWN waits; only a decisive unexpected screen recovers. */
#define GC_MAX_UNKNOWN      24
/* Consecutive decisive off-gameplay reads before believing a song ended. Missing a
 * note shakes the GH3 screen, which shifts the whole frame — and the scoreboard
 * presence probes have no positional headroom at all (measured: all three exceed
 * GP_PRESENT_TAU at a 1 px shift). So a shaken gameplay frame misses the probe, falls
 * through to the centroid classifier, and that can confidently return a menu class;
 * a single such sample used to end the run mid-song. At GC_PLAY_POLL_MS this still
 * exits well under a second after a real song end. */
#define GC_END_CONFIRM      3
/* Whether the frame-rate end-of-song probe arms for the actuation window. Disabled:
 * on hardware it fires repeatedly at certain moments of real gameplay, and the cost
 * is not the probe's own reads — a firing probe makes GameEngine_WaitEndOfSong
 * return immediately, so the play loop below loses its GC_PLAY_POLL_MS rate limit
 * and runs gp_classify flat out, which is the utilization hit that affected play.
 * The code and its exported tables stay; what is missing is evidence, not logic (6
 * static gameplay frames cannot show whatever is bright behind the band mid-song).
 * See tools/gameplay/docs/journal.md open question 3 for the capture that closes it.
 * With this at 0 the end of a song is detected by the poll below, as it was before
 * the probe existed. */
#define GC_END_PROBE_ARMED  0
/* How long to wait for P1's READY! badge after confirming on guitar_select_2p. This is
 * marvin's own on-screen acknowledgement, so it is a short UI-animation wait, not a
 * human one — if it does not appear the GREEN did not register and the step retries. */
#define GC_READY_TIMEOUT_MS 2500u

/* Bound on the per-song nod trim taken from the catalog. lemmy adds the trim to its
 * detected nod half-period and clamps the sum to 4..18 frames, so anything past ±14
 * only saturates — and clamping here is what keeps a tempo mistakenly typed into that
 * column (140) from wrapping through int8 to -116 and pegging the nod at its fastest. */
#define GC_NOD_TRIM_MAX     14

typedef enum { ACT_SELECT_INDEX, ACT_SELECT_SONG, ACT_SATURATE_TOP, ACT_WAIT,
               ACT_CONFIRM, ACT_CONFIRM_READY } gc_act_t;

typedef struct
{
    uint8_t     from;    /* GP_SCREEN_* this step applies to */
    gc_act_t    act;
    int16_t     index;   /* target cell for ACT_SELECT_INDEX */
    uint8_t     to;      /* GP_SCREEN_* expected next */
    /* The transition off `from` is gated on the *human* confirming their own side,
     * not on marvin's input (the 2-player setup screens — see build_plan). Such a
     * step waits for `to` with the long GC_PLAYER_WAIT_MS budget instead of
     * GC_STEP_TIMEOUT_MS, and must not re-fire while waiting: pressing GREEN a
     * second time on a side that is already READY would un-confirm it. */
    bool        await_player;
    const char *desc;
} gc_step_t;

static StackType_t   s_stack[GC_TASK_STACK_WORDS];
static StaticTask_t  s_tcb;
static SemaphoreHandle_t s_start_sig;
static StaticSemaphore_t s_start_sig_buf;

typedef enum { GC_MODE_NAV = 0, GC_MODE_ATTACH } gc_mode_t;

static volatile bool s_busy;
static volatile bool s_stop_req;
static bool s_performing;   /* the gameplay window is open (see set_performing) */
static volatile uint8_t s_mode = GC_MODE_NAV;   /* set by Start / StartAttach */
static void (*s_status_cb)(const char *);

/* The longest plan today is 2-player at 12 steps (1-player practice uses 8); sized with
 * slack so adding a step to either doesn't silently run off the end. Both builders
 * assert their fit. */
#define GC_PLAN_CAP  16
static gc_step_t s_plan[GC_PLAN_CAP];
static int       s_plan_len;

static void status(const char *s)
{
    LOG_INFO("GC: %s\r\n", s);
    if (s_status_cb != NULL) { s_status_cb(s); }
}

static int gc_abs(int v) { return v < 0 ? -v : v; }

/* ── observation ──────────────────────────────────────────────────────────── */

/* Synchronously observe a fresh {screen, selection}. Blocks until the engine
 * classifies a frame captured after this call, or the timeout elapses. */
static bool observe(uint8_t *screen, int16_t *sel)
{
    game_state_t gs;
    if (!GameEngine_Observe(&gs, GC_OBS_TIMEOUT_MS)) { return false; }
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

/* Move a static-list cursor onto `target` and confirm with GREEN — fully
 * closed-loop. Blind-strums the signed delta from the observed cursor (sticky-
 * default-safe), then re-observes and repeats if it under/overshoots. GREEN is
 * pressed only once the observed cursor == target while still on `expect_screen`.
 * Returns false (never pressing GREEN) if the screen isn't the expected one, the
 * selection is unreadable, or the cursor won't settle — the caller recovers. */
static bool select_and_confirm(uint8_t expect_screen, int target)
{
    for (int attempt = 0; attempt < GC_MAX_CONFIRM; attempt++)
    {
        uint8_t sc; int16_t cur;
        if (!observe(&sc, &cur))     { return false; }
        if (sc != expect_screen)     { return false; }
        if (cur < 0)                 { return false; }
        if ((int)cur == target)      { send_input(GC_GREEN); return true; }

        int delta = target - (int)cur;
        uint8_t move = (delta > 0) ? GC_STRUM_DOWN : GC_STRUM_UP;
        int n = gc_abs(delta);
        if (n > GC_MAX_MOVE) { n = GC_MAX_MOVE; }
        for (int i = 0; i < n; i++) { send_input(move); }
    }
    return false;
}

/* song_select is fixed-slot: the observed selection is a gp_song_templates[]
 * index. Toggle to the wanted setlist if needed, then strum the signed ordinal
 * delta to the target song, re-observing until the target occupies the slot,
 * then GREEN. Same closed-loop contract as select_and_confirm. */
static bool select_song(void)
{
    const game_selection_t *want = GameSelection_Get();

    for (int attempt = 0; attempt < GC_MAX_CONFIRM; attempt++)
    {
        uint8_t sc; int16_t tmpl;
        if (!observe(&sc, &tmpl))                { return false; }
        if (sc != GP_SCREEN_song_select)         { return false; }
        if (tmpl < 0 || tmpl >= GP_N_SONGS)      { return false; }

        if (gp_song_templates[tmpl].setlist != want->setlist)
        {
            send_input((want->setlist == GP_SETLIST_BONUS) ? GC_BLUE : GC_YELLOW);
            vTaskDelay(pdMS_TO_TICKS(GC_SETTLE_MS));   /* let the list swap before re-reading */
            continue;
        }

        if ((int)gp_song_templates[tmpl].index == (int)want->index)
        {
            send_input(GC_GREEN);
            return true;
        }

        int delta = (int)want->index - (int)gp_song_templates[tmpl].index;
        uint8_t move = (delta > 0) ? GC_STRUM_DOWN : GC_STRUM_UP;
        int n = gc_abs(delta);
        if (n > GC_MAX_MOVE) { n = GC_MAX_MOVE; }
        for (int i = 0; i < n; i++) { send_input(move); }
    }
    return false;
}

/* Move to the top item and GREEN — the always-want-the-top steps (FULL SONG /
 * FULL SPEED = index 0). Where the row is readable (speed_select) strum up until
 * the selection reports the top (strumming up can't overshoot — no wrap). Where
 * it isn't (section_select has no row reader) GREEN through, assuming the top is
 * already selected — we only reach it with FULL SONG selected. See
 * firmware/marvin/docs/journal.md for the deferred section_select FULL SONG reader.
 * Returns false only if the screen isn't the expected one or an observation fails. */
static bool saturate_top(uint8_t expect_screen)
{
    uint8_t sc; int16_t cur;
    if (!observe(&sc, &cur)) { return false; }
    if (sc != expect_screen) { return false; }

    for (int i = 0; i < GC_SATURATE_STRUMS && cur > 0; i++)
    {
        send_input(GC_STRUM_UP);
        if (!observe(&sc, &cur)) { return false; }
        if (sc != expect_screen) { return false; }
    }
    send_input(GC_GREEN);
    return true;
}

/* Point the CV detector at the highway the observed gameplay screen implies, and say
 * so. The log line matters because it is the *only* announcement that the scoreboard
 * presence probe fired and which side marvin is about to read: the engine's own
 * `GAME:` line is edge-triggered, so attaching to a song that is already in progress
 * (`play attach`) produces no screen change and therefore no output at all. */
static void enter_gameplay(uint8_t screen)
{
    bool two = (screen == GP_SCREEN_in_song_2p);
    const cv_marvin_v1_config_t *cfg = two ? &CV_MARVIN_CFG_2P_LEFT : &CV_MARVIN_CFG_1P;

    CvMarvinV1_SetConfig(cfg);
    LOG_INFO("GC: %s gameplay detected - CV highway %s\r\n",
             two ? "2-player" : "1-player", cfg->name);
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
/* Confirm on guitar_select_2p and verify marvin's own side actually took it: press
 * GREEN, then poll until P1's READY! badge shows. Returns false if it never does, so
 * the caller retries the step rather than sitting in the P2 wait on a GREEN that never
 * registered — which is what made a stalled setup indistinguishable from a slow human. */
static bool confirm_and_verify_ready(uint8_t expect_screen)
{
    send_input(GC_GREEN);

    TickType_t t0 = xTaskGetTickCount();
    for (;;)
    {
        if (s_stop_req) { return false; }

        game_state_t gs;
        if (GameEngine_Observe(&gs, GC_OBS_TIMEOUT_MS))
        {
            /* Already advanced (both sides confirmed fast) — nothing left to verify. */
            if (gs.screen != expect_screen) { return true; }
            if (gs.ready_p1 == 1)
            {
                LOG_INFO("GC: P1 READY confirmed\r\n");
                return true;
            }
        }
        if ((TickType_t)(xTaskGetTickCount() - t0) > pdMS_TO_TICKS(GC_READY_TIMEOUT_MS))
        {
            LOG_WARN("GC: P1 READY badge never appeared — GREEN did not register\r\n");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(GC_STEP_POLL_MS));
    }
}

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

/* The 2-player (pro face-off) path, per gh3_navigation.md. Policy is fixed, not
 * chosen at run time: always PRO FACE-OFF, always PLAY SHOW, confirm through the
 * venue, and marvin drives P1 (left) only while a human drives P2.
 *
 * Two steps are `await_player`: on select-guitar and player-ready, marvin confirms
 * its own side and the screen advances only once the human confirms theirs.
 *
 * The tail from song_select on is deliberately over-specified. Whether pro face-off
 * shows part_select, and whether section/speed select appear at all, is an open
 * question (the capture that mapped this path stopped at venue_select). Because the
 * plan is dispatched by *observed* screen, a step whose screen never appears is
 * simply never run — so listing them costs nothing and turns a plausible surprise
 * screen into a handled one instead of a RED-recovery back out of the setup flow.
 *
 * Unverified hop: character_select_2p -> player_ready_2p. Since each half advances
 * independently, the frame right after marvin confirms is "P1 on its panel, P2 still
 * on the strip" — a mixed state the corpus has no example of (it has the mirror,
 * P1-strip/P2-panel), so which class it lands in is unknown. If it still reads as
 * character_select_2p the step re-fires one extra GREEN, which lands on the panel's
 * already-highlighted PLAY SHOW and therefore confirms the intended destination
 * early rather than wandering off. Watch the GAME log on the first hardware run.
 */
static void build_plan_2p(const game_selection_t *sel)
{
    int n = 0;
    s_plan[n++] = (gc_step_t){ GP_SCREEN_main_menu,          ACT_SELECT_INDEX, 3, GP_SCREEN_guitar_select_2p,    false, "MULTIPLAYER" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_guitar_select_2p,   ACT_CONFIRM_READY, 0, GP_SCREEN_multiplayer_menu,   true,  "guitar (await P2)" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_multiplayer_menu,   ACT_SELECT_INDEX, 1, GP_SCREEN_character_select_2p, false, "PRO FACE-OFF" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_character_select_2p,ACT_CONFIRM,      0, GP_SCREEN_player_ready_2p,     true,  "character (await P2)" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_player_ready_2p,    ACT_SELECT_INDEX, 0, GP_SCREEN_venue_select,        true,  "PLAY SHOW (await P2)" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_venue_select,       ACT_CONFIRM,      0, GP_SCREEN_song_select,         false, "venue" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_song_select,        ACT_SELECT_SONG,  0, GP_SCREEN_difficulty_select,   false, "song" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_part_select,        ACT_SELECT_INDEX, 0, GP_SCREEN_difficulty_select,   false, "LEAD" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_difficulty_select,  ACT_SELECT_INDEX, (int16_t)sel->difficulty, GP_SCREEN_loading, false, "difficulty" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_section_select,     ACT_SATURATE_TOP, 0, GP_SCREEN_speed_select,        false, "FULL SONG" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_speed_select,       ACT_SATURATE_TOP, 0, GP_SCREEN_loading,             false, "FULL SPEED" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_loading,            ACT_WAIT,         0, GP_SCREEN_in_song_2p,          false, "loading" };
    configASSERT(n <= GC_PLAN_CAP);
    s_plan_len = n;
}

static void build_plan(const game_selection_t *sel)
{
    if (sel->mode == (uint8_t)GAME_MODE_2P) { build_plan_2p(sel); return; }

    int n = 0;
    s_plan[n++] = (gc_step_t){ GP_SCREEN_main_menu,        ACT_SELECT_INDEX, 4, GP_SCREEN_training_menu,    false, "TRAINING" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_training_menu,    ACT_SELECT_INDEX, 1, GP_SCREEN_song_select,      false, "PRACTICE" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_song_select,      ACT_SELECT_SONG,  0, GP_SCREEN_part_select,      false, "song" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_part_select,      ACT_SELECT_INDEX, 0, GP_SCREEN_difficulty_select,false, "LEAD" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_difficulty_select,ACT_SELECT_INDEX, (int16_t)sel->difficulty, GP_SCREEN_section_select, false, "difficulty" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_section_select,   ACT_SATURATE_TOP, 0, GP_SCREEN_speed_select,     false, "FULL SONG" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_speed_select,     ACT_SATURATE_TOP, 0, GP_SCREEN_loading,          false, "FULL SPEED" };
    s_plan[n++] = (gc_step_t){ GP_SCREEN_loading,          ACT_WAIT,         0, GP_SCREEN_in_song,          false, "loading" };
    configASSERT(n <= GC_PLAN_CAP);
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

/* Run a step closed-loop. Returns true only if the step's activation was
 * confirmed (cursor landed then GREEN, or the WAIT reached its target); false
 * means "did not activate" — the caller re-observes and retries / recovers. */
static bool execute(const gc_step_t *st)
{
    switch (st->act)
    {
        case ACT_SELECT_INDEX:  return select_and_confirm(st->from, st->index);
        case ACT_SELECT_SONG:   return select_song();
        case ACT_SATURATE_TOP:  return saturate_top(st->from);
        case ACT_WAIT:          return wait_for(st->to, GC_LOADING_TIMEOUT_MS);
        /* Accept whatever this screen already offers. For screens with no cursor to
         * read (no gp_menus layout) and nothing to choose: select-guitar, the
         * character-select strip, and the venue carousel (any venue is fine). The
         * dispatcher only calls this after observing st->from, so the screen is
         * already confirmed and GREEN is not blind. */
        case ACT_CONFIRM:       send_input(GC_GREEN); return true;
        /* As ACT_CONFIRM, but verifies marvin's own side registered before the caller
         * drops into the (unbounded) wait for the human. */
        case ACT_CONFIRM_READY: return confirm_and_verify_ready(st->from);
        default:                return false;
    }
}

/* ── run ──────────────────────────────────────────────────────────────────── */

/* Open/close the performance window — the band plays along with the song, not with
 * the menus. Two things ride on it:
 *
 *   - lemmy's servos and lightshow's LEDs, gated at each node via actuator_enable
 *     (ANDed with the operator's master toggle, which is unchanged by this).
 *   - lemmy's nod, per song, from the catalog's nod_trim column: 0 means "he doesn't
 *     nod well to this one" (and is what an unknown song reads as), so the nod stays
 *     off for the whole window; anything else is pushed as his trim and the nod is
 *     enabled. Trim first, so the first nod frame already runs at the song's setting.
 *
 * Sending on the window edges makes the console's `lemmy nod`/`trim` a bench override
 * that the next run replaces. Idempotent, because every terminal path routes through
 * finish() including those that never reached gameplay. */
static void set_performing(bool on)
{
    if (s_performing == on) { return; }
    s_performing = on;

    ActuatorEnable_SetPlaying(on);

    int16_t trim = 0;
    if (on)
    {
        const game_selection_t *sel = GameSelection_Get();
        int16_t raw = sel->valid ? GameCatalog_NodTrim(sel->setlist, sel->index) : 0;

        trim = raw;
        if (trim >  GC_NOD_TRIM_MAX) { trim =  GC_NOD_TRIM_MAX; }
        if (trim < -GC_NOD_TRIM_MAX) { trim = -GC_NOD_TRIM_MAX; }
        if (trim != raw)
        {
            LOG_WARN("GC: nod trim %d out of range, using %d - that column is lemmy's "
                     "trim, not a tempo\r\n", (int)raw, (int)trim);
        }

        LOG_INFO("GC: song nod trim %d - lemmy nod %s\r\n", (int)trim, trim ? "on" : "off");
        if (trim != 0)
        {
            (void)T1SLink_SendLemmyCtrl(T1S_ANIM_CTRL_NOD_TRIM, (uint8_t)(int8_t)trim);
        }
    }
    (void)T1SLink_SendLemmyCtrl(T1S_ANIM_CTRL_NOD_EN, (trim != 0) ? 1u : 0u);
}

/* Release the wire, disable CV, report a terminal status.
 *
 * Clearing s_busy *before* publishing is load-bearing, not tidiness. Observers run
 * on their own task and read GameController_IsBusy() when they handle the status, so
 * publishing first is a race: an observer that samples between the two lines sees a
 * run still in flight, and since this is the last status of the run, nothing ever
 * corrects it. That latched the dashboard's START/STOP button in STOP.
 *
 * Every terminal path routes through here so the ordering can't be forgotten. */
static void finish(const char *st)
{
    GameEngine_ArmEndWatch(false);  /* close the window before releasing the wire */
    GameTiming_SetEnabled(false);   /* also sends one release */
    set_performing(false);          /* lemmy and lightshow go still with the song */
    s_busy = false;
    status(st);
}

/* Return to main_menu from wherever we are. RED backs up the forward-nav menus,
 * but the terminal overlays (practice-end / quit-confirm / pause) don't reliably
 * accept RED, so QUIT out of those explicitly. Used both to anchor a run and to
 * clear the end-of-song menus when a run finishes. */
static bool nav_to_main_menu(void)
{
    for (int i = 0; i < GC_MAX_EXIT_ITERS; i++)
    {
        game_state_t gs;
        if (!GameEngine_Observe(&gs, GC_OBS_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(GC_SETTLE_MS));
            continue;
        }
        uint8_t sc = gs.screen;
        if (sc == GP_SCREEN_main_menu) { return true; }
        if (s_stop_req) { return false; }

        /* An unnamed screen here is usually a real end screen the fingerprint could
         * not name, because the magazine and collage are per-song: a 2P run on an
         * unseen magazine landed 5192 from song_select. Without a substitute the
         * switch below falls to RED, which the face-off results screen ignores, and
         * the exit budget drains. gp_end_layout reads page layout instead, and this
         * is the window its precondition requires (a run we started just ended). */
        if (sc == GP_SCREEN_UNKNOWN)
        {
            if (gs.end_layout == GP_END_LAY_PRACTICE)     { sc = GP_SCREEN_practice_end_menu; }
            else if (gs.end_layout == GP_END_LAY_FACEOFF) { sc = GP_SCREEN_faceoff_end_menu; }
            if (sc != GP_SCREEN_UNKNOWN)
            {
                LOG_INFO("GC: unnamed screen read as %s by layout\r\n",
                         (sc == GP_SCREEN_practice_end_menu) ? "practice_end_menu"
                                                             : "faceoff_end_menu");
            }
        }

        switch (sc)
        {
            case GP_SCREEN_practice_end_menu: (void)select_and_confirm(sc, 4); break;  /* QUIT → main_menu */
            /* The face-off results screen has no BACK affordance at all — its legend
             * offers only SELECT and UP/DOWN, so RED does nothing and the default arm
             * below would spin until the exit budget ran out. CONTINUE (item 0) is the
             * only way off it, and it lands on song_select, which RED does back out of. */
            case GP_SCREEN_faceoff_end_menu:  (void)select_and_confirm(sc, 0); break;  /* CONTINUE → song_select */
            case GP_SCREEN_quit_confirm:      (void)select_and_confirm(sc, 1); break;  /* QUIT (confirm) → main_menu */
            case GP_SCREEN_pause_menu:        (void)select_and_confirm(sc, 6); break;  /* QUIT → quit_confirm */
            default:                          send_input(GC_RED);              break;  /* back up one level */
        }
        /* Wait on what the classifier actually reported, not the substitute: if we
         * came in on an unnamed frame, the thing that changes is it ceasing to be
         * unnamed. */
        (void)wait_screen_change(gs.screen, GC_STEP_TIMEOUT_MS);
    }
    uint8_t sc; int16_t sel;
    return observe(&sc, &sel) && sc == GP_SCREEN_main_menu;
}

/* CV plays; hold until the song ends (practice_end_menu), we leave gameplay, or
 * Stop is requested. Poll via observe() to detect the end — a classify during
 * in_song is cheap (no song-match). The timing pipeline actuates for the whole
 * enabled window; the controller owns that window (enable here, disable on exit). */
/* Persist the human player's score for a finished 2-player song (spec §4.8.6).
 *
 * Only a 2-player run that played to the end produces a row: 1P ROBOT is marvin
 * playing alone (no human performance to record) and a stopped run has no song
 * total. `seen` is required as well as a value, so an opponent amp that never
 * produced a confident read writes nothing rather than a 0. Every skipped case
 * says which one it was — a missing high score should be diagnosable from the log.
 */
static void record_human_result(bool ended_naturally, bool seen, uint32_t score)
{
    const game_selection_t *sel = GameSelection_Get();

    if (!ended_naturally)
    {
        LOG_INFO("GC: no result row — run did not reach the end of the song\r\n");
        return;
    }
    if (!sel->valid || sel->mode != (uint8_t)GAME_MODE_2P)
    {
        LOG_INFO("GC: no result row — not a 2-player run (results record the human)\r\n");
        return;
    }
    if (!seen)
    {
        LOG_WARN("GC: no result row — never got a confident read of the human's amp\r\n");
        return;
    }

    const char *title = GameCatalog_Title(sel->setlist, sel->index);

    results_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.setlist    = (sel->setlist == GP_SETLIST_BONUS) ? "bonus" : "main";
    rec.index      = sel->index;
    rec.song       = (title != NULL) ? title : "";
    rec.difficulty = GameSelection_DifficultyName(sel->difficulty);
    rec.score      = score;

    if (Results_Append(&rec))
    {
        LOG_INFO("GC: result saved — %s %s #%u '%s' %s %lu\r\n",
                 Results_GetPlayer(), rec.setlist, (unsigned)rec.index,
                 rec.song, rec.difficulty, (unsigned long)rec.score);

        /* The run just changed the high-score table this row belongs to, so the dashboard's
         * board is stale. Posted rather than read here: re-reading the file is the feed's
         * job, which does it off the render lock. */
        DashboardFeed_PostResults();
    }
    else
    {
        LOG_WARN("GC: result save FAILED (card missing or write error)\r\n");
    }
}

static void play_until_done(void)
{
    status("PLAYING");
    GameTiming_SetEnabled(true);   /* controller owns the actuation window */
    GameEngine_ArmEndWatch(GC_END_PROBE_ARMED);  /* frame-rate veto — off, see the define */
    set_performing(true);          /* lemmy + lightshow live for the song, nod per song */
    TickType_t play_start = xTaskGetTickCount();
    DashboardFeed_PostPlaytime(0u);    /* reset the dashboard playtime bar (run reset the rest) */

    uint32_t final_score = 0u;   /* last CV-read score this run (the song total) */
    uint16_t peak_streak = 0u;   /* longest note streak this run — persists across misses */
    /* The human's amp total (2p only) — the one value that gets persisted. `seen`
     * distinguishes "never read the opponent's amp" from "the human scored 0". */
    uint32_t human_score = 0u;
    bool     human_seen  = false;
    bool     ended_naturally = false;   /* song ran out, vs. a stop request */
    int      off_gameplay = 0;              /* consecutive reads of off_screen (GC_END_CONFIRM) */
    uint8_t  off_screen   = GP_SCREEN_UNKNOWN;   /* which off-gameplay screen is accumulating */

    for (;;)
    {
        /* Wait on the end-of-song probe instead of sleeping through it. It fires
         * within ~33 ms of the results screen appearing and has already cut the
         * pipeline by the time this returns; a timeout is the ordinary poll tick.
         * The classifier below still decides the run ended — the probe only buys
         * back the ~1 s of actuation that the poll-and-confirm path used to leave
         * running on the results menu. */
        (void)GameEngine_WaitEndOfSong(GC_PLAY_POLL_MS);
        if (s_stop_req) { break; }

        DashboardFeed_PostPlaytime((uint32_t)(xTaskGetTickCount() - play_start)
                                   * portTICK_PERIOD_MS);

        /* Observe the full state so the CV-read score can ride the dashboard feed;
         * the same read detects the song ending (screen leaves gameplay). */
        game_state_t gs;
        if (GameEngine_Observe(&gs, GC_OBS_TIMEOUT_MS))
        {
            /* With the frame-rate probe disarmed, GameEngine_EndOfSongSeen() never
             * fires, and an end screen the fingerprint cannot name would sit at
             * UNKNOWN — which is *held* below, not counted — so the run would stay
             * open until STOP. That is the hang the probe was covering, and
             * gp_end_layout covers it instead: it reads page layout, so a new
             * magazine does not defeat it.
             *
             * Fed through the ordinary off-gameplay path rather than treated as
             * decisive, because it leaks on gameplay frames too (in_song_2p reaches
             * +32 against a +10 gate). Requiring GC_END_CONFIRM identical reads is
             * exactly the shake defence that already exists for that. */
            uint8_t sc = gs.screen;
            if (sc == GP_SCREEN_UNKNOWN)
            {
                if (gs.end_layout == GP_END_LAY_PRACTICE)     { sc = GP_SCREEN_practice_end_menu; }
                else if (gs.end_layout == GP_END_LAY_FACEOFF) { sc = GP_SCREEN_faceoff_end_menu; }
            }

            /* Any positive gameplay read clears the end-of-song evidence. `loading`
             * and UNKNOWN neither confirm nor deny (UNKNOWN is the shake case), so
             * they hold the count rather than resetting or advancing it — unless the
             * end-of-song probe has fired, which resolves them (see below). */
            if (gs.screen == GP_SCREEN_in_song || gs.screen == GP_SCREEN_in_song_2p)
            {
                off_gameplay = 0;
                off_screen   = GP_SCREEN_UNKNOWN;

                /* The probe vetoed actuation but the classifier still sees the
                 * highway: a false positive. Restoring the window here (rather
                 * than in the observer) keeps the rule that only the controller
                 * grants actuation, and is why an aggressive probe is safe — the
                 * cost of a false fire is the notes missed in this interval, not
                 * the run. Re-arming resets the confirm state. */
                if (!GameTiming_IsEnabled())
                {
                    LOG_WARN("GC: end-of-song probe was a false alarm (screen %u) — resuming\r\n",
                             (unsigned)gs.screen);
                    GameTiming_SetEnabled(true);
                    GameEngine_ArmEndWatch(GC_END_PROBE_ARMED);
                }
            }

            if (gs.screen == GP_SCREEN_in_song)
            {
                if (gs.score >= 0) { final_score = (uint32_t)gs.score; DashboardFeed_PostScore(final_score); }
                if (gs.multiplier >= 1u) { DashboardFeed_PostMultiplier(gs.multiplier); }
                if (gs.streak > peak_streak) { peak_streak = gs.streak; }
                DashboardFeed_PostStreak(gs.streak);
            }
            else if (gs.screen == GP_SCREEN_in_song_2p)
            {
                /* 2p: the two amp scoreboards replace the 1p block, so the score comes
                 * from those instead. Marvin is P1 (it drives the left highway only),
                 * so its amp feeds the ROBOT card and `final_score`, and the human's
                 * feeds the HUMAN card. Multiplier and streak are visible in the amp
                 * block but have no 2p reader yet, so those rows hold their reset
                 * values rather than showing a 1p read that doesn't apply here. */
                if (gs.score_p1 >= 0)
                {
                    final_score = (uint32_t)gs.score_p1;
                    DashboardFeed_PostScore(final_score);
                }
                if (gs.score_p2 >= 0)
                {
                    human_score = (uint32_t)gs.score_p2;
                    human_seen  = true;
                    DashboardFeed_PostHumanScore(human_score);
                }
            }
            else if (GameEngine_EndOfSongSeen())
            {
                /* The probe saw the results screen and this read is not the highway:
                 * the song is over. This case has to come before the UNKNOWN hold
                 * below, because the classifier does not necessarily recognize the
                 * end screen at all — its practice_end_menu exemplars are all one
                 * song, and the left magazine page carries per-song cover art, so a
                 * different song's results page can sit at UNKNOWN indefinitely.
                 * Waiting for a decisive *name* would then hang the run until STOP.
                 *
                 * The probe is the decisive evidence here, and it is not the shake
                 * case GC_END_CONFIRM defends against (a jolt cannot light the
                 * results collage). Only a positive gameplay read above overrules
                 * it, which is exactly the false-alarm path. */
                LOG_INFO("GC: song ended — end-of-song probe, screen %u\r\n",
                         (unsigned)gs.screen);
                ended_naturally = true;
                break;
            }
            else if (sc != GP_SCREEN_loading && sc != GP_SCREEN_UNKNOWN)
            {
                /* Decisive off-gameplay read. Require the *same* screen to persist,
                 * not merely N off-gameplay reads: a real ending lands on the end/pause
                 * menu and stays there, whereas a shake makes the centroid classifier
                 * match whatever it happens to match, typically differing frame to
                 * frame. Keying on stability rides through a shake without slowing a
                 * genuine song end. Logged while holding, so a run that does end early
                 * says which screen the classifier thought it saw. */
                if (sc == off_screen)
                {
                    off_gameplay++;
                }
                else
                {
                    off_screen   = sc;
                    off_gameplay = 1;
                }
                /* Reached only when the probe has *not* fired (that case returns
                 * above), so this is still the full shake defence it was built as. */
                if (off_gameplay >= GC_END_CONFIRM) { ended_naturally = true; break; }
                LOG_INFO("GC: off-gameplay read %u%s (%d/%d) — holding\r\n",
                         (unsigned)sc, (sc != gs.screen) ? " by layout" : "",
                         off_gameplay, GC_END_CONFIRM);
            }
        }
    }

    uint32_t play_ms = (uint32_t)(xTaskGetTickCount() - play_start) * portTICK_PERIOD_MS;
    LOG_INFO("GC: playtime %lu.%03lu s\r\n",
             (unsigned long)(play_ms / 1000u), (unsigned long)(play_ms % 1000u));
    LOG_INFO("GC: result score %lu, peak streak %u\r\n",
             (unsigned long)final_score, (unsigned)peak_streak);
    record_human_result(ended_naturally, human_seen, human_score);

    /* Leave GH3 on the end screen; just release CV and go idle. The next run's
     * anchor (nav_to_main_menu) QUITs out of the end/pause menus when START is
     * pressed again. */
    finish("READY");
}

/* Make sure fauxmote is connected to the Wii before a run (it's the actuation path
 * to the console). Reconnect if the link is bonded-but-down.
 *
 * On failure it reports the reason through `why` rather than publishing it: this runs
 * with the run still marked busy, so the caller has to hand the message to finish()
 * for the busy flag to be settled before observers see it. Progress statuses
 * ("CONNECTING") are published directly — those are not terminal, so a later status
 * always corrects whatever an observer sampled. */
static bool ensure_wii_connected(const char **why)
{
    uint8_t flags;
    if (!Fauxmote_GetStatus(&flags, NULL, NULL, NULL, NULL))
    {
        /* No STATUS from fauxmote (link down / not running) — can't manage the
         * BT link here; proceed so the guitar node can still actuate. */
        LOG_WARN("GC: no fauxmote status; skipping Wii connect check\r\n");
        return true;
    }
    if (flags & MF_ST_CONNECTED) { return true; }
    if (!(flags & MF_ST_BONDED))
    {
        *why = "PAIR WII";   /* never synced — needs a manual red-SYNC pairing */
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
    *why = "NO WII";
    return false;
}

/* Attach mode: no navigation. Wait for the operator's manually-started game to
 * reach a gameplay screen, point the CV detector at the matching highway, then
 * actuate the song to its end. Times out to idle if no game appears. */
static void play_attached(void)
{
    status("WAIT GAME");
    TickType_t t0 = xTaskGetTickCount();
    for (;;)
    {
        if (s_stop_req) { finish("READY"); return; }

        uint8_t sc; int16_t sel;
        if (observe(&sc, &sel) &&
            (sc == GP_SCREEN_in_song || sc == GP_SCREEN_in_song_2p))
        {
            enter_gameplay(sc);
            play_until_done();   /* actuates until the song ends / Stop / leaves gameplay */
            return;
        }
        if ((TickType_t)(xTaskGetTickCount() - t0) > pdMS_TO_TICKS(GC_ATTACH_WAIT_MS))
        {
            finish("NO GAME");   /* no gameplay screen within the wait budget */
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(GC_STEP_POLL_MS));
    }
}

static void run(void)
{
    const game_selection_t *sel = GameSelection_Get();
    if (s_mode == GC_MODE_NAV && !sel->valid)
    {
        status("NO SONG");
        return;
    }

    s_busy = true;

    /* Push the committed difficulty to both detectors: the CV detector picks its
     * observation lead from it (the highway scrolls faster on harder tiers), and
     * the fretboard node picks its inference model. Applied once here because the
     * selection is an input to the run and SELECT SONG is gated while one is in
     * flight. Guarded on ->valid so attach mode with nothing committed keeps
     * whatever the console last set. */
    if (sel->valid)
    {
        CvMarvinV1_SetDifficulty(sel->difficulty);
        FretboardLink_SetDifficulty(sel->difficulty);
    }

    /* Fall back to CV when the NN can't play what was committed (untrained
     * difficulty, or 2-player — see FretboardLink_CanPlay). The dashboard greys
     * the NEURAL NETWORK row for exactly these selections, so this is the
     * backstop for the paths that bypass it: the console `active fretboard`, and
     * a selection committed while NN was already active. Silently playing the
     * hard model at another tier's scroll speed is the failure this prevents. */
    if (Detector_GetActive() == DETECTOR_FRETBOARD
        && !FretboardLink_CanPlay(sel->valid, sel->difficulty,
                                  sel->mode == (uint8_t)GAME_MODE_2P))
    {
        LOG_WARN("GC: NN unavailable for this selection — using CV\r\n");
        Detector_SetActive(DETECTOR_CV_MARVIN_V1);
        FretboardLink_UpdateArm();
    }

    /* Clear the telemetry the instant a run is requested — score/multiplier/streak
     * zero out on Play, without waiting to navigate into gameplay. The HUMAN score
     * clears with them so a 2-player run never opens showing the previous game's
     * opponent total (only a 2-player song ever writes it). */
    DashboardFeed_PostScore(0u);
    DashboardFeed_PostHumanScore(0u);
    DashboardFeed_PostMultiplier(1u);
    DashboardFeed_PostStreak(0u);

    /* Pre-flight: the Wii link must be up (fauxmote is how we reach the console).
     * Exits through finish() like every other terminal path, so the busy flag is
     * settled before the status reaches an observer. A stop during the connect wait
     * reports no reason of its own. */
    const char *why = NULL;
    if (!ensure_wii_connected(&why))
    {
        finish((why != NULL) ? why : (s_stop_req ? "READY" : "FAILED"));
        return;
    }

    status("NAVIGATING");

    /* Own the wire: manual off, timing off (set timing off *after* manual, since
     * ManualControl_SetEnabled(false) flips timing on — the arbitration trap). */
    if (ManualControl_IsEnabled()) { ManualControl_SetEnabled(false); }
    GameTiming_SetEnabled(false);

    /* Attach mode skips menu navigation entirely — the operator set the game up. */
    if (s_mode == GC_MODE_ATTACH) { play_attached(); return; }

    if (!nav_to_main_menu())
    {
        finish(s_stop_req ? "READY" : "FAILED");
        return;
    }

    build_plan(sel);

    int recover = 0;
    int confirm_fail = 0;
    int unknown = 0;
    for (int iter = 0; iter < GC_MAX_ITERS; iter++)
    {
        if (s_stop_req) { finish("READY"); return; }

        uint8_t sc; int16_t s;
        if (!observe(&sc, &s)) { continue; }

        if (sc == GP_SCREEN_in_song || sc == GP_SCREEN_in_song_2p)
        {
            /* 2p reads the left (robot) highway, 1p the centered one. (The console
             * `cvcfg` override can force either.) */
            enter_gameplay(sc);
            play_until_done();
            return;
        }

        const gc_step_t *st = step_for(sc);
        if (st != NULL)
        {
            LOG_INFO("GC: %s\r\n", st->desc);
            if (execute(st))
            {
                if (st->await_player)
                {
                    /* Marvin has confirmed its side; the screen now moves only when the
                     * human confirms theirs. Wait for the *expected* screen (not merely
                     * "changed") on the long budget, and treat running out as its own
                     * terminal outcome: falling through would re-fire the step and press
                     * GREEN again on an already-READY side, and recovering would RED out
                     * of the setup flow while the player was still deciding. */
                    status("WAITING FOR P2");
                    if (!wait_for(st->to, GC_PLAYER_WAIT_MS))
                    {
                        finish(s_stop_req ? "READY" : "NO PLAYER 2");
                        return;
                    }
                }
                /* Wait for the transition off this screen before re-evaluating, so
                 * the same step can't re-fire (and a stray input can't hit the next
                 * screen). ACT_WAIT already blocked until its target screen. */
                else if (st->act != ACT_WAIT)
                {
                    (void)wait_screen_change(st->from, GC_TRANSITION_MS);
                }
                recover = 0;
                confirm_fail = 0;
                unknown = 0;
            }
            else
            {
                /* Couldn't confirm the cursor/activation on a known screen — never
                 * press GREEN blind. Re-observe and retry; give up if it persists. */
                if (++confirm_fail > GC_MAX_CONFIRM_FAIL) { finish("FAILED"); return; }
                LOG_INFO("GC: unconfirmed on screen %u — retrying\r\n", (unsigned)sc);
            }
        }
        else if (sc == GP_SCREEN_UNKNOWN)
        {
            /* Unreadable, not off-plan — hold position and look again (see
             * GC_MAX_UNKNOWN). Bounded so a permanently unreadable screen still
             * reaches a terminal outcome instead of spinning forever. */
            if (++unknown > GC_MAX_UNKNOWN) { finish("NO SCREEN"); return; }
            vTaskDelay(pdMS_TO_TICKS(GC_STEP_POLL_MS));
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
    s_mode = GC_MODE_NAV;
    s_stop_req = false;
    (void)xSemaphoreGive(s_start_sig);
}

void GameController_StartAttach(void)
{
    if (s_busy)
    {
        LOG_WARN("GC: busy — ignoring attach\r\n");
        return;
    }
    s_mode = GC_MODE_ATTACH;
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
