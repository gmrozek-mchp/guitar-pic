#ifndef UI_UI_MANAGER_H
#define UI_UI_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Top-level UI orchestrator. Owns the canvas surface pool and LCDC layer
 * mapping, screen startup (string table + screenInit/Show — the MGS screen
 * state machine is disabled; see docs/ui_compositor.md), and the BASE/dashboard
 * layer. Per-panel modules (ui/nav, future dialogs) own their own widgets and
 * interactions and are hosted here. Call once from APP_Initialize, before the
 * scheduler and after Legato_Initialize. Builds + hosts every screen on its own
 * layer/canvas — splash on OVR2 (full screen, on top), dashboard on BASE, nav on
 * OVR1 — so the Legato render task paints them all once the scheduler is up. */
void UiManager_Initialize(void);

/* Drop the splash overlay (OVR2) to reveal the dashboard on BASE — the
 * compositor's "replace" verb. Called by the loader once rendering is idle (the
 * dashboard, having painted behind the splash, is already complete). */
void UiManager_RevealDashboard(void);

/* Turn on the LCD backlight. The boot loader calls this once the splash is
 * painted, so the panel never shows a pre-splash frame. The UI owns the
 * display/composition, so backlight control lives here. */
void UiManager_EnableBacklight(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_UI_MANAGER_H */
