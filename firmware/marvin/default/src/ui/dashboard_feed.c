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

/* Status gets a depth-1 overwrite mailbox of its own instead of riding the shared
 * queue, because it is the one event type that is an *edge* rather than a sample.
 *
 * The shared queue is deliberately drop-on-full, which is right for telemetry: a lost
 * playtime or score is replaced by the next one 300 ms later. A status is not resent —
 * "READY" at the end of a run is the last one there will be — so dropping it left the
 * dashboard believing a run was still in flight, latching START/STOP in STOP until the
 * next run. During gameplay the controller posts up to four telemetry events per poll
 * against a 16-deep queue drained by a priority-2 task, so a full queue is ordinary,
 * not exceptional.
 *
 * xQueueOverwrite never fails and never blocks, so this keeps the wait-free contract
 * the header promises, and the queue's own copy avoids tearing the string. */
static QueueHandle_t s_status_q;
static StaticQueue_t s_status_q_buf;
static uint8_t       s_status_q_storage[sizeof(dashboard_evt_t)];

static StackType_t   s_task_stack[DF_TASK_STACK_WORDS];
static StaticTask_t  s_task_tcb;

/* An out-of-range type: the drain loop ignores it (it only records types below
 * DASH_EVT_COUNT), so it carries no state and exists purely to unblock the consumer —
 * which otherwise waits forever on the queue and would not notice a show. */
#define DF_EVT_WAKE   ((uint8_t)DASH_EVT_COUNT)

/* Latest not-yet-applied event of each type, and whether there is one. Persistent
 * across loop iterations rather than local to one drain, which is what lets the applies
 * be deferred while the dashboard is hidden: events keep coalescing in here, and a show
 * flushes exactly the set that changed while it was away. */
static bool            s_have[DASH_EVT_COUNT];
static dashboard_evt_t s_latest[DASH_EVT_COUNT];

/* False while the dashboard is not on screen — fullscreen video covers it, or another
 * base view has replaced it. Applying then would repaint a surface nobody scans out,
 * costing CPU and DDR write bandwidth for pixels that cannot be seen. */
static volatile bool   s_shown = true;

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

void DashboardFeed_PostVideo(bool displayed)
{
    dashboard_evt_t e = { .type = DASH_EVT_VIDEO, .u.on = displayed };
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
    if (s_status_q != NULL) { (void)xQueueOverwrite(s_status_q, &e); }

    /* Still poke the shared queue, purely to wake the consumer. Losing this to a full
     * queue is harmless: full means the consumer has work pending and has not drained
     * yet, so its next drain necessarily happens after the mailbox was written, and it
     * reads the mailbox on that same pass. */
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

        if (xQueueReceive(s_q, &evt, portMAX_DELAY) != pdTRUE) { continue; }
        do
        {
            if (evt.type < DASH_EVT_COUNT)
            {
                s_latest[evt.type] = evt;
                s_have[evt.type]   = true;
            }
        } while (xQueueReceive(s_q, &evt, 0) == pdTRUE);

        /* Take the status from its mailbox, which cannot have been dropped, rather
         * than from whatever survived the shared queue. Done after the drain so a
         * status posted during it is still seen this pass. */
        {
            dashboard_evt_t st;
            if (xQueueReceive(s_status_q, &st, 0) == pdTRUE)
            {
                s_latest[DASH_EVT_STATUS] = st;
                s_have[DASH_EVT_STATUS]   = true;
            }
        }

        /* Hidden: keep coalescing, apply nothing. The pending set is flushed by the
         * show, so no update is lost — only deferred. */
        if (!s_shown) { continue; }

        /* Selection first (rebuilds the SONG card), then live activity on top. Each
         * apply clears its pending flag, so the next pass only touches what changed. */
        UiManager_RenderLock();
        if (s_have[DASH_EVT_SELECTION]) { ScreenDashboard_ApplySelection(); }
        if (s_have[DASH_EVT_STATUS])    { ScreenDashboard_ApplyStatus(s_latest[DASH_EVT_STATUS].u.text); }
        if (s_have[DASH_EVT_PLAYTIME])  { ScreenDashboard_ApplyPlaytime(s_latest[DASH_EVT_PLAYTIME].u.play_ms); }
        if (s_have[DASH_EVT_SCORE])     { ScreenDashboard_ApplyScore(s_latest[DASH_EVT_SCORE].u.score); }
        if (s_have[DASH_EVT_MULTIPLIER]) { ScreenDashboard_ApplyMultiplier(s_latest[DASH_EVT_MULTIPLIER].u.mult); }
        if (s_have[DASH_EVT_STREAK])    { ScreenDashboard_ApplyStreak(s_latest[DASH_EVT_STREAK].u.streak); }
        if (s_have[DASH_EVT_FRET])      { ScreenDashboard_ApplyFret(s_latest[DASH_EVT_FRET].u.fret_mask); }
        if (s_have[DASH_EVT_VIDEO])     { ScreenDashboard_ApplyVideoState(s_latest[DASH_EVT_VIDEO].u.on); }
        UiManager_RenderUnlock();

        (void)memset(s_have, 0, sizeof s_have);
    }
}

void DashboardFeed_SetShown(bool shown)
{
    bool was = s_shown;

    s_shown = shown;

    /* A show has to wake the consumer: it is parked on the queue with no timeout, so
     * without this the deferred set would sit unapplied until the next producer post —
     * which, outside a run, may be a long time. Only on a real transition, so the
     * several paths that legitimately re-assert "shown" cost nothing. */
    if (shown && !was)
    {
        dashboard_evt_t wake = { .type = DF_EVT_WAKE, .u = { 0 } };
        post(&wake);
    }
}

void DashboardFeed_Init(void)
{
    s_q = xQueueCreateStatic(DF_QUEUE_DEPTH, sizeof(dashboard_evt_t),
                             s_q_storage, &s_q_buf);
    s_status_q = xQueueCreateStatic(1u, sizeof(dashboard_evt_t),
                                    s_status_q_storage, &s_status_q_buf);
}

void DashboardFeed_Start(void)
{
    (void)xTaskCreateStatic(dashboard_task, "DashFeed", DF_TASK_STACK_WORDS,
                            NULL, DF_TASK_PRIORITY, s_task_stack, &s_task_tcb);
}
