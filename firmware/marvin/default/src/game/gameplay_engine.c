#include "game/gameplay_engine.h"
#include "game/gameplay_classify.h"
#include "game/gameplay_select.h"
#include "game/gameplay_metadata.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "log.h"
#include "video/video.h"

#define GAME_TASK_STACK_WORDS   1024u
#define GAME_TASK_PRIORITY      4u    /* vision tier (peer of cv_marvin_v1) */
#define GAME_FRAME_QUEUE_DEPTH  1u    /* newest-frame-only */
#define GAME_BUS_DEPTH          8u
#define GAME_BYTES_PER_PIXEL    3u
#define GAME_US_PER_TICK        (1000000u / configTICK_RATE_HZ)

/* Observation is purely request-triggered: the task sleeps (draining frames) and
 * classifies exactly one frame per GameplayEngine_RequestObservation() call —
 * there is no free-running background scan. The only consumer is the game
 * controller (menu nav + song-end polling), which requests an observation
 * whenever it needs one. This keeps an idle marvin at ~0 CPU: the song_select
 * match is a heavy soft-float compute (no FPU on this core, ~tens of ms) and
 * must never run unasked — a continuous scan pegged prio-4 and froze the UI. */

static QueueHandle_t s_bus_queue;
static StaticQueue_t s_bus_queue_buf;
static uint8_t       s_bus_queue_storage[GAME_BUS_DEPTH * sizeof(game_state_t)];

static StaticQueue_t s_frame_queue_buf;
static uint8_t       s_frame_queue_storage[GAME_FRAME_QUEUE_DEPTH * sizeof(Video_FrameInfo)];

static StackType_t   s_task_stack[GAME_TASK_STACK_WORDS];
static StaticTask_t  s_task_tcb;

static volatile bool    s_observe_enabled;
static volatile bool    s_force_observe;
static volatile uint8_t s_current_screen = GP_SCREEN_UNKNOWN;

/* Latest classified state, retained every classify (not just on change) so the
 * game-state controller can poll a fresh {screen, selection} synchronously. */
static volatile bool s_have_latest;
static game_state_t  s_latest;

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

    LOG_INFO("GAME: gameplay_engine started\r\n");

    uint8_t   last_screen = GP_SCREEN_UNKNOWN;
    int16_t   last_sel    = -2;  /* != any real selection or -1, so first read logs */

    for (;;)
    {
        Video_FrameInfo frame;
        if (xQueueReceive(frames, &frame, portMAX_DELAY) != pdTRUE) { continue; }

        /* Drain every frame so the queue never backs up, but classify only when an
         * observation has been requested — no free-running scan. A bad frame leaves
         * the request pending so it's served by the next valid one. */
        if (!s_observe_enabled)                            { continue; }
        if (!s_force_observe)                              { continue; }
        if (frame.buffer == NULL)                          { continue; }
        if (frame.bytes_per_pixel != GAME_BYTES_PER_PIXEL) { continue; }
        s_force_observe = false;

        TickType_t now = xTaskGetTickCount();
        const uint8_t *buf = (const uint8_t *)frame.buffer;
        int w = (int)frame.width, h = (int)frame.height;

        int32_t best_dist = 0, margin = 0;
        uint8_t screen = gp_classify(buf, w, h, &best_dist, &margin);
        s_current_screen = screen;

        const char *sel_name = NULL, *sel_name2 = NULL;
        int16_t sel = read_selection(buf, w, h, screen, &sel_name, &sel_name2);

        taskENTER_CRITICAL();
        s_latest.frame_epoch  = frame.frame_count;
        s_latest.timestamp_us = (uint64_t)now * GAME_US_PER_TICK;
        s_latest.screen       = screen;
        s_latest.best_dist    = best_dist;
        s_latest.margin       = margin;
        s_latest.selection    = sel;
        s_have_latest         = true;
        taskEXIT_CRITICAL();

        if (screen != last_screen || sel != last_sel)
        {
            game_state_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.frame_epoch  = frame.frame_count;
            ev.timestamp_us = (uint64_t)now * GAME_US_PER_TICK;
            ev.screen       = screen;
            ev.best_dist    = best_dist;
            ev.margin       = margin;
            ev.selection    = sel;
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
            else
            {
                LOG_INFO("GAME: %s\r\n", screen_name(screen));
            }
            last_screen = screen;
            last_sel    = sel;
        }
    }
}

void GameplayEngine_Initialize(void)
{
    s_bus_queue = xQueueCreateStatic(GAME_BUS_DEPTH,
                                     sizeof(game_state_t),
                                     s_bus_queue_storage,
                                     &s_bus_queue_buf);
    configASSERT(s_bus_queue != NULL);

    /* Master gate on; the task still does nothing until a RequestObservation. */
    s_observe_enabled = true;

    (void)xTaskCreateStatic(game_task, "GameEngine", GAME_TASK_STACK_WORDS,
                            NULL, GAME_TASK_PRIORITY, s_task_stack, &s_task_tcb);
}

QueueHandle_t GameplayEngine_BusQueue(void)
{
    return s_bus_queue;
}

void GameplayEngine_SetObserveEnabled(bool on)
{
    s_observe_enabled = on;
}

void GameplayEngine_RequestObservation(void)
{
    s_force_observe = true;
}

bool GameplayEngine_ObserveEnabled(void)
{
    return s_observe_enabled;
}

uint8_t GameplayEngine_CurrentScreen(void)
{
    return s_current_screen;
}

bool GameplayEngine_GetLatest(game_state_t *out)
{
    if (out == NULL) { return false; }
    bool have;
    taskENTER_CRITICAL();
    have = s_have_latest;
    if (have) { *out = s_latest; }
    taskEXIT_CRITICAL();
    return have;
}
