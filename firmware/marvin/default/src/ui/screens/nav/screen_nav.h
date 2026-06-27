#ifndef UI_SCREEN_NAV_H
#define UI_SCREEN_NAV_H

#ifdef __cplusplus
extern "C" {
#endif

/* Navigation drawer — authored as its own MGS Screen (Navigation), rendered into
 * its own canvas and bound to the OVR1 hardware layer as a resident overlay.
 * Owned by ui/screens/nav; ui_manager only has to assign the surface and show
 * the screen:
 *   Nav_InitSurface — assign the nav canvas buffer; call before the canvas state
 *                     machine is RUNNING / before the first render.
 * Everything else (moving the root onto the nav canvas, binding it to OVR1,
 * panel setup, button wiring, open/close) happens in the Navigation screen's
 * OnShow hook and entry events. */
void Nav_InitSurface(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_NAV_H */
