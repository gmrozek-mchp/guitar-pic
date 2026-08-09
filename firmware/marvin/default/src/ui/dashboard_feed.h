#ifndef UI_DASHBOARD_FEED_H
#define UI_DASHBOARD_FEED_H

#include <stdbool.h>
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
 * Drop-on-full is correct for telemetry, which is *sampled*: a lost playtime or score
 * is superseded by the next one. It is wrong for DASH_EVT_STATUS, which is an *edge* —
 * a terminal status is never resent, so dropping one leaves the dashboard acting on a
 * run that has already ended. Status therefore goes through its own depth-1 overwrite
 * mailbox as well, which cannot be crowded out by telemetry and is still wait-free.
 * Any new event type should be classified the same way before it is added.
 *
 * This header is POD-only (no Legato types) so producers in actuator/ and game/ can
 * include it without pulling in the GFX stack.
 *
 * Not everything on the dashboard has a producer: T1S node presence and the output
 * enable each actuator node confirms arrive on heartbeats, with no event to post. The
 * consumer therefore waits with a timeout rather than parking, and polls those rows on
 * the idle tick (repainting only what changed).
 *
 * Lifecycle: DashboardFeed_Init() creates the queue pre-scheduler (so producers may
 * post immediately — buffered events are coalesced); DashboardFeed_Start() creates the
 * consumer task once the dashboard widgets are built and painted (post-reveal). */

typedef enum
{
    DASH_EVT_FRET = 0,   /* u.fret_mask — 7-bit guitar mask (GUITAR_BTN_*)   */
    DASH_EVT_SELECTION,  /* no payload — consumer reads GameSelection_Get()       */
    DASH_EVT_STATUS,     /* u.text — game-controller status line              */
    DASH_EVT_PLAYTIME,   /* u.play_ms — elapsed play time, scaled to bar fill */
    DASH_EVT_SCORE,      /* u.score      (future)                             */
    DASH_EVT_MULTIPLIER, /* u.mult       (future)                             */
    DASH_EVT_STREAK,     /* u.streak     (future)                             */
    DASH_EVT_VIDEO,      /* u.on — HEO is (not) covering the video card       */
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
        bool     on;
        char     text[DASH_EVT_TEXT_CAP];
    } u;
} dashboard_evt_t;

/* Create the event queue. Call once, pre-scheduler (before any producer can post). */
void DashboardFeed_Init(void);

/* Create the consumer task. Call once the dashboard widgets exist and are painted
 * (end of boot / post-reveal). Buffered events apply on the task's first run. */
void DashboardFeed_Start(void);

/* Gate the applies on whether the dashboard is actually on screen. While hidden the
 * consumer keeps coalescing events but writes no widgets, so nothing repaints into a
 * surface the LCDC is not scanning; the show flushes whatever changed meanwhile, so
 * updates are deferred rather than lost. Call through ScreenDashboard_SetShown(), which
 * is the name the compositor uses for every other screen. */
void DashboardFeed_SetShown(bool shown);

/* Producer posts — all non-blocking (drop-on-full). No-ops if the queue isn't up. */
void DashboardFeed_PostFret(uint8_t mask);
void DashboardFeed_PostSelection(void);
void DashboardFeed_PostStatus(const char *text);
void DashboardFeed_PostPlaytime(uint32_t elapsed_ms);
void DashboardFeed_PostScore(uint32_t score);
void DashboardFeed_PostMultiplier(uint16_t mult);
void DashboardFeed_PostStreak(uint16_t streak);

/* Whether the live video is actually on the panel over the video card. Posted by the
 * compositor when HEO's bind state settles, so the dashboard can drop the SMPTE test
 * pattern that sits *under* HEO — otherwise the bars show through for the gap between
 * the dashboard becoming visible and HEO being rebound. Video-task ctx. */
void DashboardFeed_PostVideo(bool displayed);

#ifdef __cplusplus
}
#endif

#endif /* UI_DASHBOARD_FEED_H */
