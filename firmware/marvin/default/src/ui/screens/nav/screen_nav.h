#ifndef UI_SCREEN_NAV_H
#define UI_SCREEN_NAV_H

#ifdef __cplusplus
extern "C" {
#endif

/* Navigation drawer — layer 1 of the Marvin master screen, rendered into its own
 * canvas (CANVAS_NAV) and bound to a hardware layer by ui_manager. Owned by
 * ui/screens/nav:
 *   Nav_InitSurface — assign the nav canvas buffer; call before the canvas state
 *                     machine is RUNNING / before the first render.
 *   Nav_Setup       — canvas window (closed/off-screen) + move-FX callback +
 *                     button wiring; call once after screenInit_Marvin.
 * Open/close (slide FX) and entry events live in screen_nav.c. */
void Nav_InitSurface(void);
void Nav_Setup(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_NAV_H */
