#ifndef UI_SCREEN_SPLASH_H
#define UI_SCREEN_SPLASH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Boot splash image, layered onto the MGS `Splash` screen.
 *
 * The Splash screen itself (root + full-screen Splash_Panel_0 with a solid
 * scheme fill) is authored in MGS like every base view (le_gen_screen_Splash.c)
 * — kept for the "every base view has an MGS component" convention even though
 * the splash's only real content is loaded, not authored. The photo can't be an
 * MGS asset (it's read off the SD card at runtime), so this module adds one
 * leImageWidget onto the MGS panel in code and points it at the in-memory JPEG.
 * With no image set (missing/oversized splash.jpg, no card) the panel's solid
 * fill is the fallback splash.
 *
 * Order: ui_manager calls screenInit_Splash() (builds the persistent MGS tree),
 * moves it onto the splash canvas (bound to OVR2), then Splash_AttachImage()
 * (adds the image widget); the loader later calls Splash_SetImageJpeg() once the
 * file is read. */

#define SPLASH_W   1280u
#define SPLASH_H    800u

/* Add the image widget onto the (already-built) MGS Splash panel. Call once,
 * after screenInit_Splash(), before the first leUpdate. */
void Splash_AttachImage(void);

/* Point the splash image at a JPEG held in memory (the loader's staging
 * buffer). `data` must stay valid while the splash is shown — the decoder reads
 * it on each paint. Assumed SPLASH_W x SPLASH_H. Returns false on bad args or
 * if the image widget isn't attached (caller shows the solid-fill fallback). */
bool Splash_SetImageJpeg(void *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_SPLASH_H */
