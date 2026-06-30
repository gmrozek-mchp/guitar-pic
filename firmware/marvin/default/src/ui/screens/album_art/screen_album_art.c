#include "ui/screens/album_art/screen_album_art.h"

#include "ui/ui_manager.h"   /* CANVAS_ALBUM_ART */
#include "gfx/canvas/gfx_canvas_api.h"

/* Layer 3 is RGBA8888 — the only full-color mode the 2D engine supports
 * (gfx2dFormats[]: RGB_565 and RGBA_8888 only; RGB_888 is unsupported, so a 24bpp
 * canvas can't be GFX2D-blitted and renders garbage). At 508x208 the 32bpp surface
 * is ~0.42 MB, so the bandwidth cost is trivial; the dialog itself (OVR1) stays
 * RGB565. The window sits over the dialog's art rect. */
#define ART_W   508u
#define ART_H   208u
#define ART_X   433    /* absolute screen position of the art rect (over the OVR1 dialog) */
#define ART_Y   160

#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

/* Non-cached so the LCDC scans CPU-/2D-written pixels coherently (like the other
 * canvas surfaces). RGBA8888: 4 B/px. */
static uint32_t FB_NOCACHE s_fb_album_art[ART_W * ART_H];

void ScreenAlbumArt_InitSurface(void)
{
    /* Clear to opaque black. The buffer is NOLOAD (uninitialized DDR = noise), and
     * the album-art panel is BACKGROUND_NONE, so any area Legato doesn't paint would
     * scan as garbage; a known fill makes "unpainted" read as black, not noise.
     * RGBA8888 word is 0xRRGGBBAA, so opaque black = 0x000000FF. */
    for (uint32_t i = 0u; i < (ART_W * ART_H); i++) { s_fb_album_art[i] = 0x000000FFu; }

    gfxcSetPixelBuffer(CANVAS_ALBUM_ART, ART_W, ART_H, GFX_COLOR_MODE_RGBA_8888, s_fb_album_art);
}

void ScreenAlbumArt_Setup(void)
{
    gfxcSetWindowSize(CANVAS_ALBUM_ART, ART_W, ART_H);
    gfxcSetWindowPosition(CANVAS_ALBUM_ART, ART_X, ART_Y);
}
