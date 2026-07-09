#include "ui/dashboard_feed.h"

#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "ui/ui_manager.h"                      /* UiManager_RenderLock/Unlock */
#include "ui/screens/dashboard/screen_dashboard.h"

#define DF_QUEUE_DEPTH        16u
#define DF_TASK_STACK_WORDS   1024u
#define DF_TASK_PRIORITY      2u   /* UI band — below every actuation/detector task */

static QueueHandle_t s_q;
static StaticQueue_t s_q_buf;
static uint8_t       s_q_storage[DF_QUEUE_DEPTH * sizeof(dashboard_evt_t)];

static StackType_t   s_task_stack[DF_TASK_STACK_WORDS];
static StaticTask_t  s_task_tcb;

static void post(const dashboard_evt_t *evt)
{
    if (s_q == NULL) { return; }
    (void)xQueueSend(s_q, evt, 0);   /* non-blocking; drop-on-full (best-effort) */
}

void DashboardFeed_PostFret(uint8_t mask)
{
    dashboard_evt_t e = { .type = DASH_EVT_FRET, .u.fret_mask = mask };
    post(&e);
}

void DashboardFeed_PostSelection(void)
{
    dashboard_evt_t e = { .type = DASH_EVT_SELECTION };
    post(&e);
}

void DashboardFeed_PostStatus(const char *text)
{
    dashboard_evt_t e = { .type = DASH_EVT_STATUS };
    if (text != NULL)
    {
        (void)strncpy(e.u.text, text, DASH_EVT_TEXT_CAP - 1u);
        e.u.text[DASH_EVT_TEXT_CAP - 1u] = '\0';
    }
    post(&e);
}

void DashboardFeed_PostPlaytime(uint32_t elapsed_ms)
{
    dashboard_evt_t e = { .type = DASH_EVT_PLAYTIME, .u.play_ms = elapsed_ms };
    post(&e);
}

void DashboardFeed_PostScore(uint32_t score)
{
    dashboard_evt_t e = { .type = DASH_EVT_SCORE, .u.score = score };
    post(&e);
}

void DashboardFeed_PostMultiplier(uint16_t mult)
{
    dashboard_evt_t e = { .type = DASH_EVT_MULTIPLIER, .u.mult = mult };
    post(&e);
}

void DashboardFeed_PostStreak(uint16_t streak)
{
    dashboard_evt_t e = { .type = DASH_EVT_STREAK, .u.streak = streak };
    post(&e);
}

/* Block for new info, then drain everything queued and keep only the latest event of
 * each type — a burst collapses to one apply pass, and a dropped intermediate fret
 * event never loses the final state. Widget mutation is done under the render lock
 * (Legato is single-threaded), so the apply can't race the renderer's damage list;
 * this task is the sole app-side writer of dashboard widgets. */
static void dashboard_task(void *param)
{
    (void)param;

    for (;;)
    {
        dashboard_evt_t evt;
        bool            have[DASH_EVT_COUNT]   = { false };
        dashboard_evt_t latest[DASH_EVT_COUNT] = { { 0 } };

        if (xQueueReceive(s_q, &evt, portMAX_DELAY) != pdTRUE) { continue; }
        do
        {
            if (evt.type < DASH_EVT_COUNT)
            {
                latest[evt.type] = evt;
                have[evt.type]   = true;
            }
        } while (xQueueReceive(s_q, &evt, 0) == pdTRUE);

        /* Selection first (rebuilds the SONG card), then live activity on top. */
        UiManager_RenderLock();
        if (have[DASH_EVT_SELECTION]) { ScreenDashboard_ApplySelection(); }
        if (have[DASH_EVT_STATUS])    { ScreenDashboard_ApplyStatus(latest[DASH_EVT_STATUS].u.text); }
        if (have[DASH_EVT_PLAYTIME])  { ScreenDashboard_ApplyPlaytime(latest[DASH_EVT_PLAYTIME].u.play_ms); }
        if (have[DASH_EVT_SCORE])     { ScreenDashboard_ApplyScore(latest[DASH_EVT_SCORE].u.score); }
        if (have[DASH_EVT_MULTIPLIER]) { ScreenDashboard_ApplyMultiplier(latest[DASH_EVT_MULTIPLIER].u.mult); }
        if (have[DASH_EVT_FRET])      { ScreenDashboard_ApplyFret(latest[DASH_EVT_FRET].u.fret_mask); }
        UiManager_RenderUnlock();
    }
}

void DashboardFeed_Init(void)
{
    s_q = xQueueCreateStatic(DF_QUEUE_DEPTH, sizeof(dashboard_evt_t),
                             s_q_storage, &s_q_buf);
}

void DashboardFeed_Start(void)
{
    (void)xTaskCreateStatic(dashboard_task, "DashFeed", DF_TASK_STACK_WORDS,
                            NULL, DF_TASK_PRIORITY, s_task_stack, &s_task_tcb);
}
