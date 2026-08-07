#ifndef UI_SCREEN_SYSTEM_H
#define UI_SCREEN_SYSTEM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* System-info screen — layer 7 of the Marvin master screen (Marvin_PANEL_SYSTEM),
 * rendered into its own canvas (CANVAS_SYSTEM) and bound to a hardware layer by
 * ui_manager. A peer full-screen base view (like the dashboard, wiimotes and bus
 * screens); built + painted at boot but not shown until swapped onto BASE.
 *
 * A product showcase of the seven boards that make up the robot: an overview grid of
 * node cards, each tapping through to that node's detail view. The static NODE table in
 * screen_system.c is the content — description, parts list and all.
 *
 * Owned by ui/screens/system:
 *   ScreenSystem_InitSurface — assign the canvas buffer; call before the canvas state
 *                     machine is RUNNING / before the first render.
 *   ScreenSystem_Setup       — canvas window (full-screen), both views' widget trees,
 *                     and gate the layer out of picking while it isn't shown; call
 *                     once after screenShow_Marvin.
 */
void ScreenSystem_InitSurface(void);
void ScreenSystem_Setup(void);

/* Enable/disable picking on the whole system subtree (LE_WIDGET_ENABLED on the
 * background panel; no repaint). ui_manager calls this when it shows/hides the view
 * and when the nav drawer opens over it. */
void ScreenSystem_SetInput(bool on);

/* Marks the view shown/hidden. Entering resets to the overview, so leaving from a
 * node detail and coming back lands on the grid rather than a stale detail. Costs a
 * repaint only when the view actually changes. */
void ScreenSystem_SetShown(bool shown);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_SYSTEM_H */
