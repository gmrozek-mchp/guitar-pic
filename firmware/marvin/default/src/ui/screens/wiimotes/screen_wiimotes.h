#ifndef UI_SCREEN_WIIMOTES_H
#define UI_SCREEN_WIIMOTES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wiimotes / manual-override screen — layer 4 of the Marvin master screen, rendered
 * into its own canvas (CANVAS_WIIMOTES) and bound to a hardware layer by ui_manager.
 * Defined but not shown at boot (the canvas pool holds more layer-screens than the
 * LCDC composites at once — see docs/ui_compositor.md §0). Owned by
 * ui/screens/wiimotes:
 *   ScreenWiimotes_InitSurface — assign the canvas buffer; call before the canvas
 *                          state machine is RUNNING / before the first render.
 *   ScreenWiimotes_Setup       — canvas window (full-screen) + gate the layer out of
 *                          picking while it isn't shown; call once after screenShow_Marvin.
 */
void ScreenWiimotes_InitSurface(void);
void ScreenWiimotes_Setup(void);

/* Enable/disable picking on the whole wiimotes subtree (LE_WIDGET_ENABLED on the
 * background panel; no repaint). ui_manager calls this when it shows/hides the
 * wiimotes base view and when the nav drawer opens over it. */
void ScreenWiimotes_SetInput(bool on);

/* Where the live video sits on this screen and the ARGB_4444 frame surface to
 * composite over it — ui_manager binds HEO and the overlay layer from these when it
 * shows this base view. The rect is a compile-time constant of the layout. */
void ScreenWiimotes_VideoRect(uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h);
const void *ScreenWiimotes_VideoFrameSurface(void);

/* Take (shown) / relinquish (hidden) the fauxmote link as this manual-override view
 * becomes/stops being the active base view. While shown, the screen's buttons drive
 * fauxmote directly and take priority over the gameplay mirror. Distinct from
 * SetInput, which the drawer modal also toggles. ui_manager calls this on the real
 * base-view show/hide only. */
void ScreenWiimotes_SetShown(bool shown);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_WIIMOTES_H */
