#ifndef UI_UI_MANAGER_H
#define UI_UI_MANAGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Canvas ids — the per-screen render surfaces in the GFX_CANVAS pool. ui_manager
 * owns this numbering (the single source of truth); each screen renders into its
 * own canvas. A canvas is NOT tied to a hardware layer — see below. */
#define CANVAS_DASH    0u
#define CANVAS_NAV     1u
#define CANVAS_SPLASH  2u

/* LCDC hardware-layer indices (drvLayer / layerOrder): BASE 0, HEO 1, OVR1 2,
 * OVR2 3. HEO is the live camera (off-limits). A canvas is bound to a hardware
 * layer at *display time* by the compositor (ui_manager) via gfxcSetLayer — a
 * screen never owns a layer. The HW-layer budget limits how many canvases can be
 * *visible at once*, not how many screens can be built. */
#define HW_BASE   0u
#define HW_OVR1   2u
#define HW_OVR2   3u

#define BASE_W   1280u
#define BASE_H    800u

/* Hardware layer the boot splash is shown on. OVR1 (above BASE so it covers the
 * dashboard); OVR2 is left free for an overlay drawn over the splash, e.g. a
 * loading bar. */
#define SPLASH_HW_LAYER   HW_OVR1

/* Bring up the UI. Call once from APP_Initialize, before the scheduler and after
 * Legato_Initialize. Assigns the per-screen canvas surfaces and creates the boot
 * task that runs the bring-up sequence (splash → screens → reveal) once the
 * scheduler is up. Pure setup; no screen is built or shown here. */
void UiManager_Initialize(void);

/* Register a callback fired the instant the splash is on screen, from the boot
 * task. The app uses it to start everything else (services + camera) in parallel
 * with the behind-the-splash screen painting, so the system is warm at reveal.
 * Set before UiManager_Initialize. */
void UiManager_SetSplashShownCallback(void (*cb)(void));

#ifdef __cplusplus
}
#endif

#endif /* UI_UI_MANAGER_H */
