#ifndef UI_SCREEN_DASHBOARD_H
#define UI_SCREEN_DASHBOARD_H

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

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_DASHBOARD_H */
