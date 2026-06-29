#ifndef UI_SCREEN_SPLASH_H
#define UI_SCREEN_SPLASH_H

#include <stdbool.h>

#include "definitions.h"   /* XLCDC_LAYER */

/* Boot splash — a full-screen raw RGBA8888 image, NOT a Legato screen and NOT a
 * GFX canvas.
 *
 * It owns a static framebuffer and drives an XLCDC hardware layer directly (the
 * same way video.c drives the camera on HEO), so it can be on screen before
 * Legato / the Marvin screen is built — the image lives in QSPI NOR
 * (flash/qspi_layout.h QSPI_SPLASH_OFFSET, provisioned with openocd/program-qspi.sh)
 * as raw RGBA8888 pixels in the layer's native byte order, so displaying it is a
 * flash read straight into the framebuffer the LCDC scans out: no JPEG decode, no
 * widgets, no Legato, no SD. Reading from QSPI keeps the SD bring-up out of the
 * splash path — splash up in ~260 ms, no card needed.
 *
 * ui_manager owns hardware-layer policy and passes the layer to show/hide on. */

/* Read the splash image from QSPI NOR into the framebuffer. Returns true if it
 * loaded; on a flash error the buffer is filled with an opaque fallback colour and
 * false is returned — either way the framebuffer is displayable. Uses the SST26
 * driver; call from a task once the scheduler is running. */
bool Splash_Load(void);

/* Program the given XLCDC layer to scan the splash framebuffer (full-screen,
 * opaque, RGBA8888) and enable it. */
void Splash_Show(XLCDC_LAYER layer);

/* Disable the given XLCDC layer. */
void Splash_Hide(XLCDC_LAYER layer);

#endif /* UI_SCREEN_SPLASH_H */
