#ifndef UI_SCREEN_SPLASH_H
#define UI_SCREEN_SPLASH_H

#include <stdbool.h>
#include <stdint.h>

/* Boot splash — a full-screen raw RGBA8888 image, NOT a Legato screen.
 *
 * It owns its canvas surface and pixel buffer and knows how to load itself: the
 * image lives in QSPI NOR (flash/qspi_layout.h QSPI_SPLASH_OFFSET, provisioned
 * with openocd/program-qspi.sh) as raw RGBA8888 pixels in the layer's native byte
 * order, so displaying it is a flash read straight into the canvas buffer (the
 * buffer the LCDC scans out) — no JPEG decode, no widgets, no Legato, no SD. Raw
 * over JPEG: a software full-screen JPEG decode cost ~1.5 s; raw trades ~4 MB of
 * flash for skipping it. Reading from QSPI instead of the SD card also drops the
 * SD bring-up out of the splash path — splash up in ~260 ms, no card needed.
 *
 * This module does the load only. ui_manager (the compositor) decides which
 * hardware layer the splash canvas is shown on and lights the backlight. */

/* Assign the splash canvas pixel buffer. Call once pre-scheduler, before the
 * canvas state machine runs. */
void Splash_InitSurface(void);

/* Read the splash image from QSPI NOR into the splash canvas buffer. Returns true
 * if it loaded; on a flash error the buffer is filled with an opaque fallback
 * colour and false is returned — either way the canvas is displayable. Uses the
 * SST26 driver; call from a task once the scheduler is running. */
bool Splash_Load(void);

/* The splash canvas id, so the compositor can bind it to a hardware layer. */
uint32_t Splash_CanvasId(void);

#endif /* UI_SCREEN_SPLASH_H */
