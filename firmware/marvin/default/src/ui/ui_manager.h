#ifndef UI_UI_MANAGER_H
#define UI_UI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Canvas ids — the per-layer-screen render surfaces in the GFX_CANVAS pool.
 *
 * The three Marvin layer-screens render into canvas[i] for Legato layer i (the
 * renderer maps Legato layer i → canvas[baseCanvasID + i], base 0), so these
 * canvas ids are pinned equal to their layer index — NOT free. A canvas is bound
 * to a *hardware* layer at display time by the compositor — that binding is
 * independent and runtime (see HW_* + bind_canvas). The boot splash is NOT a
 * canvas: it drives its hardware layer directly (see splash.h). */
#define CANVAS_DASH        0u   /* Marvin layer 0 — dashboard (base view)    */
#define CANVAS_NAVIGATION  1u   /* Marvin layer 1 — navigation drawer        */
#define CANVAS_SONGSEL     2u   /* Marvin layer 2 — song/mode-select dialog  */
#define CANVAS_ALBUM_ART   3u   /* Marvin layer 3 — song-select cover (RGB888) */

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

/* Video (HEO layer) display control. The compositor owns the HEO hardware layer;
 * video.c is the capture producer. Show binds the live capture to HEO at the given
 * panel rect (bilinear-scaled if the rect differs from the source), taking effect
 * once the source locks; Hide disables HEO output (capture keeps running). Layout
 * (the rect) is compositor policy. */
void UiManager_VideoShow(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void UiManager_VideoHide(void);

/* Toggle the HEO video levels-expansion (limited→full range via the gamma CLUT).
 * On by default. Display-only; takes effect on the next HEO (re)bind (a rebind is
 * requested so a shown video updates within a frame). For A/B eyeballing. */
void UiManager_SetVideoLevels(bool on);
bool UiManager_GetVideoLevels(void);

/* Video frame overlay (OVR1, above HEO). Show binds a caller-owned ARGB_4444
 * framebuffer to OVR1 at the given panel rect (drawn on top of the video for the
 * rounded anti-aliased frame); Hide disables OVR1. The buffer must stay resident
 * while shown. Compositor owns the OVR1 register writes. */
void UiManager_VideoOverlayShow(const void *buf, uint32_t x, uint32_t y,
                                uint32_t w, uint32_t h);
void UiManager_VideoOverlayHide(void);

/* Bind / unbind the navigation drawer's canvas to its hardware layer (OVR2, above
 * the video frame so the drawer covers it when open). Show sets OVR2 to the
 * drawer's RGB565 mode; the drawer module drives the slide. */
void UiManager_ShowNavLayer(void);
void UiManager_HideNavLayer(void);

/* Show / hide the song-select dialog as a modal pair: the RGB565 dialog on OVR1 and
 * its full-color cover strip on OVR2 are bound + shown (open) or hidden together
 * (close). Closing frees OVR1 for the nav drawer. Both are no-ops if already in the
 * requested state. The dialog starts closed at boot. */
void UiManager_OpenSongSelect(void);
void UiManager_CloseSongSelect(void);

/* Gate the dashboard's touch pickability without repainting it. Called by the
 * modal screens (nav drawer, song-select) to make the dashboard a true modal
 * backdrop while one is open: false = dashboard ignores touches, true = live.
 * Toggles pickability only — the dashboard surface is never re-drawn. */
void UiManager_SetDashboardPickable(bool on);

/* Set the LCD backlight brightness, 0–100% (clamped). PWM-dimmed on PC18; valid
 * once the PWM channel is up (after the splash is shown at boot). */
void UiManager_SetBacklight(uint32_t pct);

/* Current backlight brightness, 0–100%. */
uint32_t UiManager_GetBacklight(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_UI_MANAGER_H */
