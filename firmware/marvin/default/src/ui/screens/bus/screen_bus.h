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

/* Marks the bus view shown/hidden — starts/stops the periodic statistics refresh so it
 * costs nothing on other views. */
void ScreenBus_SetShown(bool shown);

/* Swap the screen between the live T1S telemetry and a simulated feed. The
 * simulator exists because a healthy bus is near-idle and error-free, so the
 * thresholds, colour ramps, gauge sweep and chart scaling never exercise on real
 * data (and followers report zeros until each is reflashed with the v2 heartbeat).
 * While simulated, the UPTIME tile's sub-line reads SIMULATED so the screen never
 * implies the numbers are live. Takes effect on the next refresh tick; the row set
 * is built at Setup, and both feeds present the same node count. */
void ScreenBus_SetSimulated(bool on);

/* Time a frame for each custom-painted widget on this screen (gauge, sparkline, a TX bar) and
 * report one line each through `out` — the HealthMonitor_Report sink shape. Everything here
 * repaints on every refresh, so paint cost is continuous rather than only-while-touched. */
typedef void (*bus_probe_fn)(void *ctx, const char *line);

void ScreenBus_Probe(unsigned iters, bus_probe_fn out, void *ctx);

/* Force the old whole-panel repaint on every refresh instead of letting each changed
 * widget invalidate itself. For A/B measurement only; targeted is the default. */
void ScreenBus_SetFullRepaint(bool on);
bool ScreenBus_FullRepaint(void);
bool ScreenBus_Simulated(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_BUS_H */
