#ifndef UI_SCREEN_DASHBOARD_H
#define UI_SCREEN_DASHBOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/legato/legato.h"   /* leWidget */

#ifdef __cplusplus
extern "C" {
#endif

/* Dashboard — layer 0 of the Marvin master screen (the full-screen base view),
 * rendered into CANVAS_DASH. Owned by ui/screens/dashboard:
 *   ScreenDashboard_InitSurface — assign the dashboard canvas buffer; call before the canvas
 *                      state machine is RUNNING / before the first render.
 *   ScreenDashboard_Setup       — set the canvas window (full-screen at origin); call once
 *                      after screenShow_Marvin.
 * ui_manager binds CANVAS_DASH to a hardware layer (BASE) at display time. */
void ScreenDashboard_InitSurface(void);
void ScreenDashboard_Setup(void);

/* The dashboard's shared-titlebar bar widget (NULL before Setup). screen_video
 * gates it out of picking while the video is fullscreen, so a tap anywhere exits
 * fullscreen instead of hitting the hamburger. */
leWidget *ScreenDashboard_Titlebar(void);

/* The panel holding the three content columns (NULL before Setup). Every interactive
 * dashboard widget below the titlebar is a descendant, so screen_video gates the whole
 * lot out of picking with one flag while the video is fullscreen. */
leWidget *ScreenDashboard_Content(void);

/* Apply live dashboard state. Called only from the dashboard feed's consumer task
 * (ui/dashboard_feed.c) — the sole writer of dashboard widgets — never directly by
 * producers. ApplySelection rebuilds the SONG card from the committed GameSelection_Get();
 * ApplyFret reflects the 7-bit guitar mask (GUITAR_BTN_*) on the ROBOT fret buttons. */
void ScreenDashboard_ApplySelection(void);
void ScreenDashboard_ApplyFret(uint8_t mask);

/* Re-read the ACTUATORS rows (node presence + the enable each node reports). Polled
 * from the feed task's idle tick rather than pushed, because both arrive on heartbeats
 * with no event behind them; repaints only what changed. */
void ScreenDashboard_RefreshActuators(void);

/* Show or hide the SMPTE test pattern under the video card: it is only meant to be seen
 * when HEO is not covering it, and the rebind gap after a full-screen view was long
 * enough to flash the bars. Feed-task ctx, render lock held. */
void ScreenDashboard_ApplyVideoState(bool displayed);
void ScreenDashboard_ApplyStatus(const char *text);
void ScreenDashboard_ApplyPlaytime(uint32_t elapsed_ms);
void ScreenDashboard_ApplyScore(uint32_t score);
void ScreenDashboard_ApplyHumanScore(uint32_t score);
void ScreenDashboard_ApplyMultiplier(uint8_t mult);
void ScreenDashboard_ApplyStreak(uint16_t streak);

/* Tell the dashboard whether it is on screen, so telemetry stops repainting a surface
 * nobody scans out (fullscreen video covering it, or another base view replacing it).
 * Peer of ScreenBus_SetShown / ScreenWiimotes_SetShown. Deferred, not dropped: the feed
 * keeps coalescing while hidden and flushes on show. */
void ScreenDashboard_SetShown(bool shown);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_DASHBOARD_H */
