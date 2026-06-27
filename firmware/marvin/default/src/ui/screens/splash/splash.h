#ifndef UI_SCREEN_SPLASH_H
#define UI_SCREEN_SPLASH_H

#include <stdbool.h>
#include <stdint.h>

/* Boot splash — a full-screen raw RGBA8888 image, NOT a Legato screen.
 *
 * It owns its canvas surface and pixel buffer and knows how to load itself: the
 * image is stored on the SD card as raw RGBA8888 pixels in the layer's native
 * byte order, so displaying it is a file read straight into the canvas buffer
 * (the buffer the LCDC scans out) — no JPEG decode, no widgets, no Legato. A
 * software JPEG decode of the full-screen image cost ~1.5 s; the raw form trades
 * ~4 MB of file size for skipping it entirely.
 *
 * This module does the load only. ui_manager (the compositor) decides which
 * hardware layer the splash canvas is shown on and lights the backlight. */

/* Assign the splash canvas pixel buffer. Call once pre-scheduler, before the
 * canvas state machine runs. */
void Splash_InitSurface(void);

/* Read the splash image off the SD card into the splash canvas buffer. Returns
 * true if it loaded; on any failure (no card, missing file, wrong size, short
 * read) the buffer is filled with an opaque fallback colour and false is
 * returned — either way the canvas is displayable. Blocking SD I/O; call from a
 * task once the scheduler is running. */
bool Splash_Load(void);

/* The splash canvas id, so the compositor can bind it to a hardware layer. */
uint32_t Splash_CanvasId(void);

#endif /* UI_SCREEN_SPLASH_H */
