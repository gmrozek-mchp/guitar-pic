#include "ui/screens/dashboard/screen_dashboard.h"

#include "ui/ui_manager.h"   /* CANVAS_DASH, BASE_W, BASE_H */

#include "gfx/canvas/gfx_canvas_api.h"

/* Dashboard surface, non-cached so the 2D engine and LCDC DMA read CPU-rendered
 * pixels coherently; 32-byte aligned. RGB565 — steady-state UI needs no more. */
#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

static uint16_t FB_NOCACHE s_fb[BASE_W * BASE_H];

void ScreenDashboard_InitSurface(void)
{
    gfxcSetPixelBuffer(CANVAS_DASH, BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565, s_fb);
}

void ScreenDashboard_Setup(void)
{
    /* Full-screen at the origin. The root is on Legato layer 0 (built by MGS); the
     * canvas window positions that layer's pixels on the display. */
    gfxcSetWindowPosition(CANVAS_DASH, 0, 0);
    gfxcSetWindowSize(CANVAS_DASH, BASE_W, BASE_H);
}
