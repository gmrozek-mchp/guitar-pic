#ifndef UI_SCREEN_LOG_H
#define UI_SCREEN_LOG_H

#include <stdbool.h>

#include "ui/gfx/text_lut.h"   /* text_path_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Activity-log screen — layer 9 of the Marvin master screen (Marvin_PANEL_LOG),
 * rendered into its own canvas (CANVAS_LOG) and bound to a hardware layer by
 * ui_manager. A peer full-screen base view (like the dashboard, wiimotes, bus and
 * system screens); built + painted at boot but not shown until swapped onto BASE.
 *
 * Shows the lines log.c captured into log_ring, newest first, in four columns: uptime,
 * level, source and message. The tag -> readable-source mapping lives here, so `log dump`
 * over serial keeps showing the raw tags.
 *
 * Owned by ui/screens/log:
 *   ScreenLog_InitSurface — assign the canvas buffer; call before the canvas state
 *                     machine is RUNNING / before the first render.
 *   ScreenLog_Setup       — canvas window (full-screen) + gate the layer out of picking
 *                     while it isn't shown; call once after screenShow_Marvin.
 */
void ScreenLog_InitSurface(void);
void ScreenLog_Setup(void);

/* Enable/disable picking on the whole log subtree (LE_WIDGET_ENABLED on the background
 * panel; no repaint). ui_manager calls this when it shows/hides the log base view and
 * when the nav drawer opens over it. */
void ScreenLog_SetInput(bool on);

/* Marks the log view shown/hidden — starts/stops the poll that picks up new lines, so it
 * costs nothing on other views. */
void ScreenLog_SetShown(bool shown);

/* Time a repaint of the list and of the whole panel, and report one line each through
 * `out` — the HealthMonitor_Report sink shape. The list is the expensive thing on this
 * screen: it repaints in full whenever a line arrives or the operator drags, so its cost
 * is what decides whether the scroll feels right. */
typedef void (*log_probe_fn)(void *ctx, const char *line);

void ScreenLog_Probe(unsigned iters, log_probe_fn out, void *ctx);

/* Swap the list's glyph-rendering path and repaint, under the render lock. For A/B against
 * ScreenLog_Probe — see ui/gfx/text_lut.h for the three paths. */
void ScreenLog_SetTextPath(text_path_t path);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_LOG_H */
