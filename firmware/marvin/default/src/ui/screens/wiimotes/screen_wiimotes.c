#include "ui/screens/wiimotes/screen_wiimotes.h"

#include <stdint.h>

#include "ui/ui_manager.h"   /* CANVAS_WIIMOTES, BASE_W, BASE_H */

#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* wiimotes widgets */

/* Wiimotes surface, non-cached so the 2D engine and LCDC DMA read CPU-rendered
 * pixels coherently; 32-byte aligned. RGB565 to match the layer's color mode. */
#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

static uint16_t FB_NOCACHE s_fb[BASE_W * BASE_H];

void ScreenWiimotes_InitSurface(void)
{
    gfxcSetPixelBuffer(CANVAS_WIIMOTES, BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565, s_fb);
}

void ScreenWiimotes_Setup(void)
{
    /* Full-screen at the origin. The root is on Legato layer 4 (built by MGS); the
     * canvas window positions that layer's pixels on the display. */
    gfxcSetWindowPosition(CANVAS_WIIMOTES, 0, 0);
    gfxcSetWindowSize(CANVAS_WIIMOTES, BASE_W, BASE_H);

    /* Gate the screen out of picking while it isn't shown. Legato picks across every
     * attached layer top-to-bottom regardless of canvas visibility, so this topmost
     * layer's background panel would otherwise swallow touches meant for the
     * dashboard beneath it. Clearing LE_WIDGET_ENABLED on the background panel gates
     * its whole subtree from picking without repainting (leUtils_PickFromWidget
     * descends only into ENABLED children; the renderer never reads the flag) — the
     * same mechanism ui_manager uses for the closed song-select overlays. */
    Marvin_panel_Marvin->flags &= ~LE_WIDGET_ENABLED;
}
