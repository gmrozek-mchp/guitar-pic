#ifndef UI_SCREEN_BUS_H
#define UI_SCREEN_BUS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 10BASE-T1S bus-statistics screen — layer 6 of the Marvin master screen
 * (Marvin_PANEL_BUS), rendered into its own canvas (CANVAS_BUS) and bound to a
 * hardware layer by ui_manager. A peer full-screen base view (like the dashboard
 * and wiimotes screens); built + painted at boot but not shown until swapped onto
 * BASE. Placeholder for now — the per-node table + header tiles (backed by
 * T1SLink_GetNodeStats / GetBusStats) are built here in a follow-up.
 *
 * Owned by ui/screens/bus:
 *   ScreenBus_InitSurface — assign the canvas buffer; call before the canvas state
 *                     machine is RUNNING / before the first render.
 *   ScreenBus_Setup       — canvas window (full-screen) + gate the layer out of
 *                     picking while it isn't shown; call once after screenShow_Marvin.
 */
void ScreenBus_InitSurface(void);
void ScreenBus_Setup(void);

/* Enable/disable picking on the whole bus subtree (LE_WIDGET_ENABLED on the
 * background panel; no repaint). ui_manager calls this when it shows/hides the bus
 * base view and when the nav drawer opens over it. */
void ScreenBus_SetInput(bool on);

/* Marks the bus view shown/hidden — starts/stops the ~1 Hz statistics refresh so it
 * costs nothing on other views. No-op placeholder until the refresh lands. */
void ScreenBus_SetShown(bool shown);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_BUS_H */
