#ifndef UI_SCREEN_DASHBOARD_H
#define UI_SCREEN_DASHBOARD_H

#include <stdint.h>

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

/* Apply live dashboard state. Called only from the dashboard feed's consumer task
 * (ui/dashboard_feed.c) — the sole writer of dashboard widgets — never directly by
 * producers. ApplySelection rebuilds the SONG card from the committed Selection_Get();
 * ApplyFret reflects the 7-bit guitar mask (GUITAR_BTN_*) on the ROBOT fret buttons. */
void ScreenDashboard_ApplySelection(void);
void ScreenDashboard_ApplyFret(uint8_t mask);
void ScreenDashboard_ApplyStatus(const char *text);
void ScreenDashboard_ApplyPlaytime(uint32_t elapsed_ms);
void ScreenDashboard_ApplyScore(uint32_t score);
void ScreenDashboard_ApplyMultiplier(uint8_t mult);
void ScreenDashboard_ApplyStreak(uint16_t streak);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_DASHBOARD_H */
