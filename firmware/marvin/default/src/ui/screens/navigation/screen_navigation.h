#ifndef UI_SCREEN_NAVIGATION_H
#define UI_SCREEN_NAVIGATION_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Navigation drawer — layer 1 of the Marvin master screen, rendered into its own
 * canvas (CANVAS_NAVIGATION) and bound to a hardware layer by ui_manager. Owned by
 * ui/screens/navigation:
 *   ScreenNavigation_InitSurface — assign the canvas buffer; call before the canvas state
 *                            machine is RUNNING / before the first render.
 *   ScreenNavigation_Setup       — canvas window (closed/off-screen) + move-FX callback +
 *                            button wiring; call once after screenShow_Marvin.
 * Open/close (slide FX) and entry events live in screen_navigation.c. */
void ScreenNavigation_InitSurface(void);
void ScreenNavigation_Setup(void);

/* Open/close the navigation drawer. Public so any base-view titlebar hamburger
 * (the shared ui/titlebar component) can toggle it. */
void ScreenNavigation_ToggleDrawer(void);


/* Drawer slide animation on/off. The canvas Move FX is unique to this screen, so
 * disabling it isolates the FX from everything else the drawer does. Console:
 * `nav slide on|off`. */
void ScreenNavigation_SetSlide(bool on);
bool ScreenNavigation_GetSlide(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_NAVIGATION_H */
