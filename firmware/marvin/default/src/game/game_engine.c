#include "game/game_engine.h"
#include "game/gameplay_classify.h"
#include "game/gameplay_present.h"
#include "game/gameplay_select.h"
#include "game/gameplay_amp2p.h"
#include "game/gameplay_score.h"
#include "game/gameplay_endprobe.h"
#include "game/gameplay_endlayout.h"
#include "game/gameplay_metadata.h"
#include "game/game_timing.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "log.h"
#include "video/video.h"

#define GAME_TASK_STACK_WORDS   1024u
#define GAME_TASK_PRIORITY      4u    /* vision tier (peer of cv_marvin_v1) */
#define GAME_FRAME_QUEUE_DEPTH  1u    /* newest-frame-only */
#define GAME_BUS_DEPTH          8u
#define GAME_BYTES_PER_PIXEL    3u
#define GAME_US_PER_TICK        (1000000u / configTICK_RATE_HZ)

/* Observation is a synchronous request/response: the task idles (draining frames,
 * ~0 CPU) until GameEngine_Observe() posts a request, then classifies exactly
 * one fresh frame and hands the result back through s_resp_queue. There is no
 * free-running scan and no retained "latest" to poll — a read blocks for a frame
 * captured after the request, so it can never return stale state. The song_select
 * match is heavy soft-float (no FPU, ~tens of ms) and must never run unasked — a
 * continuous scan once pegged prio-4 and froze the UI. */

static QueueHandle_t s_bus_queue;
static StaticQueue_t s_bus_queue_buf;
static uint8_t       s_bus_queue_storage[GAME_BUS_DEPTH * sizeof(game_state_t)];

/* Response back to the single requester (the game controller). Depth 1. */
static QueueHandle_t s_resp_queue;
static StaticQueue_t s_resp_queue_buf;
static uint8_t       s_resp_queue_storage[1 * sizeof(game_state_t)];

static StaticQueue_t s_frame_queue_buf;
static uint8_t       s_frame_queue_storage[GAME_FRAME_QUEUE_DEPTH * sizeof(Video_FrameInfo)];

static StackType_t   s_task_stack[GAME_TASK_STACK_WORDS];
static StaticTask_t  s_task_tcb;

static volatile bool s_req_pending;

/* End-of-song watch. `s_end_armed` is set by the controller around the actuation
 * window; the tracker and the fired flag are owned by game_task. The semaphore is
 * how the controller's play loop learns about it without polling. */
static volatile bool s_end_armed;
static volatile bool s_end_fired;
static gp_end_tracker_t s_end_track;
static SemaphoreHandle_t s_end_sig;
static StaticSemaphore_t s_end_sig_buf;

/* Streak tracker state: advanced by the observer across in_song reads, reset when
 * gameplay is left (owned solely by game_task; not shared). */
static gp_streak_state_t s_streak;

static const char *screen_name(uint8_t idx)
{
    return (idx < GP_N_SCREENS) ? gp_screen_ids[idx] : "unknown";
}

/* Read the selection for the classified screen: static-list cell index, the
 * song-template index on song_select, or -1 where there's no selection concept
 * (gameplay / loading / unknown). `name`/`name2` receive log-friendly labels. */
static int16_t read_selection(const uint8_t *frame, int w, int h, uint8_t screen,
                              const char **name, const char **name2)
{
    *name = NULL;
    *name2 = NULL;
    const gp_menu_layout_t *menu = gp_menu_for_screen(screen);
    if (menu != NULL)
    {
        int cell = gp_read_selection(frame, w, h, menu);
        if (cell >= 0 && cell < menu->count) { *name = menu->items[cell]; }
        return (int16_t)cell;
    }
    if (screen == GP_SCREEN_song_select)
    {
        gp_song_t song;
        if (gp_read_song(frame, w, h, &song) == 0 && song.tmpl >= 0)
        {
            *name = (song.setlist == GP_SETLIST_BONUS) ? "bonus" : "main";
            *name2 = gp_song_templates[song.tmpl].song_id;
            return song.tmpl;
        }
    }
    return -1;
}

static void game_task(void *param)
{
    (void)param;

    QueueHandle_t frames = xQueueCreateStatic(GAME_FRAME_QUEUE_DEPTH,
                                              sizeof(Video_FrameInfo),
                                              s_frame_queue_storage,
                                              &s_frame_queue_buf);
    configASSERT(frames != NULL);
    bool subscribed = Video_SubscribeFrames(frames);
    configASSERT(subscribed);

    LOG_INFO("GAME: game_engine started\r\n");

    uint8_t   last_screen = GP_SCREEN_UNKNOWN;
    int16_t   last_sel    = -2;  /* != any real selection or -1, so first read logs */
    bool      armed       = false;  /* pre-request frame discarded; next one is fresh */

    for (;;)
    {
        Video_FrameInfo frame;
        if (xQueueReceive(frames, &frame, portMAX_DELAY) != pdTRUE) { continue; }

        /* End-of-song watch, on every frame while the actuation window is open.
         * This is the one thing here that is not request-driven, and it can be
         * because it costs 225 integer luma samples — the heavy readers below are
         * what the request/response protocol exists to rate-limit. Cutting the
         * pipeline from inside the frame loop is deliberate: the controller's poll
         * would re-introduce the delay this removes. */
        if (s_end_armed && frame.buffer != NULL
            && frame.bytes_per_pixel == GAME_BYTES_PER_PIXEL)
        {
            int hits = gp_end_probe((const uint8_t *)frame.buffer,
                                   (int)frame.width, (int)frame.height, NULL, NULL);
            bool latched = gp_end_tracker_update(&s_end_track, hits);
            if (latched && !s_end_fired)
            {
                s_end_fired = true;
                GameTiming_SetEnabled(false);   /* release the wire now, not in 300 ms */
                (void)xSemaphoreGive(s_end_sig);
                LOG_INFO("GAME: end-of-song probe fired (%d/%d hits) — actuation cut\r\n",
                         hits, GP_END_K_HITS);
            }
            else if (!latched && s_end_fired)
            {
                /* Released. Clearing the flag re-arms the edge, but the pipeline is
                 * NOT switched back on here: the controller owns the actuation
                 * window, so the observer may only ever veto it, never grant it.
                 * The controller restores it once its own classifier read confirms
                 * the song is still running. */
                s_end_fired = false;
                LOG_INFO("GAME: end-of-song probe released (%d hits)\r\n", hits);
            }
        }

        /* Drain every frame so the queue never backs up. Classify only while a
         * request is pending — no free-running scan. */
        if (!s_req_pending) { armed = false; continue; }

        /* The frame in hand when the request arrived predates it; skip it so the
         * classified frame is guaranteed to have been captured after the request. */
        if (!armed) { armed = true; continue; }

        /* A bad frame leaves the request pending so the next valid one serves it. */
        if (frame.buffer == NULL)                          { continue; }
        if (frame.bytes_per_pixel != GAME_BYTES_PER_PIXEL) { continue; }

        TickType_t now = xTaskGetTickCount();
        const uint8_t *buf = (const uint8_t *)frame.buffer;
        int w = (int)frame.width, h = (int)frame.height;

        /* Probe-first: the gameplay screens (in_song / in_song_2p) are identified by
         * static scoreboard-chrome presence (gp_present) — robust to the dynamic
         * highway/crowd content that makes a whole-frame centroid flaky. Everything
         * else falls through to the centroid classifier, which the static menu
         * screens match tightly. */
        int32_t best_dist = 0, margin = 0;
        uint8_t screen = gp_present(buf, w, h, NULL);
        if (screen == GP_SCREEN_UNKNOWN)
        {
            screen = gp_classify(buf, w, h, &best_dist, &margin);
        }

        const char *sel_name = NULL, *sel_name2 = NULL;
        int16_t sel = read_selection(buf, w, h, screen, &sel_name, &sel_name2);

        /* In-song: read the open-ended score (training font) + multiplier + the note
         * streak. Sentinels (-1 / 0) elsewhere. The streak tracker is stateful: it
         * advances across in_song observations and resets whenever gameplay is left
         * (a fresh run re-seeds from the odometer). */
        int32_t score = -1;
        uint8_t multiplier = 0;
        uint16_t streak = 0;
        int32_t score_p1 = -1, score_p2 = -1;
        if (screen == GP_SCREEN_in_song)
        {
            gp_score_t sc;
            if (gp_read_score(buf, w, h, GP_SCORE_MODE_TRAINING, &sc) == 0) { score = sc.value; }
            multiplier = (uint8_t)gp_read_multiplier(buf, w, h);
            gp_streak_raw_t sr;
            if (gp_read_streak(buf, w, h, &sr) == 0) { streak = gp_streak_track(&s_streak, &sr); }
        }
        else if (screen == GP_SCREEN_in_song_2p)
        {
            /* Two amp scoreboards instead of the 1p block: read each side. The 1p
             * score/multiplier/streak readers do not apply here and stay at their
             * sentinels. gp_present has already confirmed the amps are on screen,
             * which the reader requires (it has no presence notion of its own). */
            gp_amp2p_t a;
            if (gp_read_amp2p(buf, w, h, GP_AMP2P_SIDE_LEFT, &a) == 0)
            {
                score_p1 = a.value;
            }
            if (gp_read_amp2p(buf, w, h, GP_AMP2P_SIDE_RIGHT, &a) == 0)
            {
                score_p2 = a.value;
            }
            gp_streak_reset(&s_streak);
        }
        else
        {
            gp_streak_reset(&s_streak);
        }

        /* guitar_select_2p: whether P1's READY! badge is showing. The controller needs
         * this because that screen only advances once *both* sides confirm, so an
         * unchanged screen alone can't tell a failed GREEN from a human still choosing. */
        int8_t ready_p1 = -1;
        if (screen == GP_SCREEN_guitar_select_2p)
        {
            ready_p1 = (int8_t)gp_ready_p1_present(buf, w, h, NULL);
        }

        /* An unnamed screen is the case the end-layout namer exists for: a new
         * magazine makes gp_classify reject a real end screen, and the navigator
         * then has no branch to take. Read it only here — it is ~1.2k luma reads,
         * but more to the point it is not a screen classifier, so running it on a
         * frame gp_classify already named would invite acting on it out of
         * context. */
        uint8_t end_layout = GP_END_LAY_UNCERTAIN;
        if (screen == GP_SCREEN_UNKNOWN)
        {
            end_layout = (uint8_t)gp_end_layout(buf, w, h, NULL);
        }

        game_state_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.frame_epoch  = frame.frame_count;
        ev.timestamp_us = (uint64_t)now * GAME_US_PER_TICK;
        ev.screen       = screen;
        ev.best_dist    = best_dist;
        ev.margin       = margin;
        ev.selection    = sel;
        ev.score        = score;
        ev.multiplier   = multiplier;
        ev.streak       = streak;
        ev.ready_p1     = ready_p1;
        ev.end_layout   = end_layout;
        ev.score_p1     = score_p1;
        ev.score_p2     = score_p2;

        /* Answer the requester first (clear pending before the send so a follow-up
         * Observe that wakes on the response can't have its new request cleared). */
        armed = false;
        taskENTER_CRITICAL();
        s_req_pending = false;
        taskEXIT_CRITICAL();
        (void)xQueueSend(s_resp_queue, &ev, 0);

        if (screen != last_screen || sel != last_sel)
        {
            (void)xQueueSend(s_bus_queue, &ev, 0);

            if (sel_name2 != NULL)  /* song_select: "<setlist> #<idx> <song>" */
            {
                LOG_INFO("GAME: %s / %s #%d %s\r\n",
                         screen_name(screen), sel_name,
                         (int)gp_song_templates[sel].index, sel_name2);
            }
            else if (sel_name != NULL)  /* static-list: "<screen> / <item>" */
            {
                LOG_INFO("GAME: %s / %s\r\n", screen_name(screen), sel_name);
            }
            else if (screen == GP_SCREEN_in_song)  /* "in_song / score N x<mult> streak<s>" */
            {
                LOG_INFO("GAME: %s / score %ld x%u streak%u\r\n",
                         screen_name(screen), (long)score, (unsigned)multiplier, (unsigned)streak);
            }
            else if (screen == GP_SCREEN_in_song_2p)  /* "in_song_2p / p1 N p2 N" */
            {
                LOG_INFO("GAME: %s / p1 %ld p2 %ld\r\n",
                         screen_name(screen), (long)score_p1, (long)score_p2);
            }
            else
            {
                LOG_INFO("GAME: %s\r\n", screen_name(screen));
            }
            last_screen = screen;
            last_sel    = sel;
        }
    }
}

void GameEngine_Initialize(void)
{
    s_bus_queue = xQueueCreateStatic(GAME_BUS_DEPTH,
                                     sizeof(game_state_t),
                                     s_bus_queue_storage,
                                     &s_bus_queue_buf);
    configASSERT(s_bus_queue != NULL);

    s_resp_queue = xQueueCreateStatic(1, sizeof(game_state_t),
                                      s_resp_queue_storage, &s_resp_queue_buf);
    configASSERT(s_resp_queue != NULL);

    s_end_sig = xSemaphoreCreateBinaryStatic(&s_end_sig_buf);
    configASSERT(s_end_sig != NULL);

    (void)xTaskCreateStatic(game_task, "GameEngine", GAME_TASK_STACK_WORDS,
                            NULL, GAME_TASK_PRIORITY, s_task_stack, &s_task_tcb);
}

QueueHandle_t GameEngine_BusQueue(void)
{
    return s_bus_queue;
}

bool GameEngine_Observe(game_state_t *out, uint32_t timeout_ms)
{
    if (out == NULL) { return false; }

    /* Discard any stale response (e.g. from a prior timed-out request), then post
     * the request and block for the result. */
    game_state_t drain;
    while (xQueueReceive(s_resp_queue, &drain, 0) == pdTRUE) { }

    taskENTER_CRITICAL();
    s_req_pending = true;
    taskEXIT_CRITICAL();

    return (xQueueReceive(s_resp_queue, out, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
}

void GameEngine_ArmEndWatch(bool armed)
{
    if (armed)
    {
        /* Clear state before arming, and drain a stale signal from a prior window,
         * so a song can't inherit the previous one's fired flag. */
        gp_end_tracker_reset(&s_end_track);
        s_end_fired = false;
        if (s_end_sig != NULL) { (void)xSemaphoreTake(s_end_sig, 0); }
        s_end_armed = true;
    }
    else
    {
        s_end_armed = false;
        gp_end_tracker_reset(&s_end_track);
        s_end_fired = false;
    }
}

bool GameEngine_WaitEndOfSong(uint32_t timeout_ms)
{
    if (s_end_sig == NULL) { return false; }
    return (xSemaphoreTake(s_end_sig, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
}

bool GameEngine_EndOfSongSeen(void)
{
    return s_end_fired;
}
