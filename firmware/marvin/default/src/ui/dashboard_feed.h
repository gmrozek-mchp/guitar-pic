#ifndef UI_DASHBOARD_FEED_H
#define UI_DASHBOARD_FEED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Dashboard live-telemetry feed — the one flow by which producers push new state to
 * the dashboard. Producers (fret actuation, gameplay stats, selection) post a small
 * POD event with a non-blocking queue send; a single consumer task owned here wakes
 * on new info, coalesces, and applies it to the dashboard widgets.
 *
 * The post functions are wait-free (xQueueSend timeout 0, drop-on-full) and take no
 * lock and never touch Legato, so they are safe to call from the deterministic
 * actuation path — dashboard tracking must never delay gameplay timing. The consumer
 * runs in the UI priority band, below every actuation/detector task.
 *
 * This header is POD-only (no Legato types) so producers in actuator/ and game/ can
 * include it without pulling in the GFX stack.
 *
 * Lifecycle: DashboardFeed_Init() creates the queue pre-scheduler (so producers may
 * post immediately — buffered events are coalesced); DashboardFeed_Start() creates the
 * consumer task once the dashboard widgets are built and painted (post-reveal). */

typedef enum
{
    DASH_EVT_FRET = 0,   /* u.fret_mask — 7-bit guitar mask (GUITAR_BTN_*)   */
    DASH_EVT_SELECTION,  /* no payload — consumer reads Selection_Get()       */
    DASH_EVT_STATUS,     /* u.text — game-controller status line              */
    DASH_EVT_PLAYTIME,   /* u.play_ms — elapsed play time, scaled to bar fill */
    DASH_EVT_SCORE,      /* u.score      (future)                             */
    DASH_EVT_MULTIPLIER, /* u.mult       (future)                             */
    DASH_EVT_STREAK,     /* u.streak     (future)                             */
    DASH_EVT_COUNT
} dashboard_evt_type_t;

#define DASH_EVT_TEXT_CAP  24u

typedef struct
{
    uint8_t type;   /* dashboard_evt_type_t */
    union
    {
        uint8_t  fret_mask;
        uint32_t score;
        uint16_t mult;
        uint16_t streak;
        uint32_t play_ms;
        char     text[DASH_EVT_TEXT_CAP];
    } u;
} dashboard_evt_t;

/* Create the event queue. Call once, pre-scheduler (before any producer can post). */
void DashboardFeed_Init(void);

/* Create the consumer task. Call once the dashboard widgets exist and are painted
 * (end of boot / post-reveal). Buffered events apply on the task's first run. */
void DashboardFeed_Start(void);

/* Producer posts — all non-blocking (drop-on-full). No-ops if the queue isn't up. */
void DashboardFeed_PostFret(uint8_t mask);
void DashboardFeed_PostSelection(void);
void DashboardFeed_PostStatus(const char *text);
void DashboardFeed_PostPlaytime(uint32_t elapsed_ms);
void DashboardFeed_PostScore(uint32_t score);
void DashboardFeed_PostMultiplier(uint16_t mult);
void DashboardFeed_PostStreak(uint16_t streak);

#ifdef __cplusplus
}
#endif

#endif /* UI_DASHBOARD_FEED_H */
