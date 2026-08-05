#ifndef UI_GFX_UI_SURFACE_H
#define UI_GFX_UI_SURFACE_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/canvas/gfx_canvas_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Thin wrapper over gfxcSetPixelBuffer that also remembers each canvas's surface,
 * because the canvas API exposes no getter for it. Screens call UiSurface_Set in
 * place of gfxcSetPixelBuffer so the record cannot drift from what the compositor
 * actually renders into — the point of wrapping rather than keeping a parallel
 * registry each screen must remember to update.
 *
 * The reader is the perf-log canvas dump (PERF_CMD_CANVAS_DUMP), which needs the
 * base address, geometry and colour mode to convert a rect to the wire format. */

#define UI_SURFACE_MAX_CANVAS  8u

/* Assign a canvas's pixel buffer and record it. Returns the underlying
 * gfxcSetPixelBuffer result; the surface is recorded regardless so a dump still
 * works if the canvas framework rejects the call. */
GFXC_RESULT UiSurface_Set(unsigned int canvas, uint32_t w, uint32_t h,
                          GFXC_COLOR_FORMAT mode, void *buffer);

/* Look up a recorded surface. false if the canvas id is out of range or was never
 * assigned. Any out pointer may be NULL. */
bool UiSurface_Get(unsigned int canvas, const void **buffer,
                   uint16_t *w, uint16_t *h, GFXC_COLOR_FORMAT *mode);

#ifdef __cplusplus
}
#endif

#endif /* UI_GFX_UI_SURFACE_H */
