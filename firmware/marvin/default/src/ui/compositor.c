#include "ui/compositor.h"

#include <stdint.h>

#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"

/* Two layers on the Marvin screen (le_gen_screen_Marvin.c):
 *   layer 0 (root0) — full-screen dashboard
 *   layer 1 (root1) — 320x800 navigation panel (slide-out)
 * Canvas id == Legato layer index (baseCanvasID is 0). */
#define CANVAS_BASE   0u
#define CANVAS_NAV    1u

#define BASE_W   1280u
#define BASE_H   800u
#define NAV_W    320u
#define NAV_H    800u

/* XLCDC layer indices in drvLayer/layerOrder order: BASE, HEO, OVR1, OVR2.
 * HEO (1) is the live camera and is off-limits to the compositor. */
#define HW_BASE   0u
#define HW_OVR1   2u

#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

static uint16_t FB_NOCACHE s_fb_base[BASE_W * BASE_H];
static uint16_t FB_NOCACHE s_fb_nav[NAV_W * NAV_H];

void Compositor_Initialize(void)
{
    /* Own the canvas surface memory: RGB565 to match the panel, non-cached so
     * the 2D engine and LCDC DMA read CPU-rendered pixels coherently. Replaces
     * the generated NULL-buffer template in gfx_canvas_config.c. */
    gfxcSetPixelBuffer(CANVAS_BASE, BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565, s_fb_base);
    gfxcSetPixelBuffer(CANVAS_NAV,  NAV_W,  NAV_H,  GFX_COLOR_MODE_RGB_565, s_fb_nav);

    /* Drive the canvas state machine to RUNNING now, before the scheduler, so
     * Legato's first blit into a canvas is not dropped by the RUNNING gate in
     * GFXC_BlitBuffer. With effects disabled the canvas task otherwise flips
     * this only on its first tick, which races Legato's first render. */
    GFX_CANVAS_Task();
}

void Marvin_OnShow(void)
{
    /* Dashboard: full-screen BASE layer, shown. */
    gfxcSetWindowPosition(CANVAS_BASE, 0, 0);
    gfxcSetWindowSize(CANVAS_BASE, BASE_W, BASE_H);
    gfxcSetLayer(CANVAS_BASE, HW_BASE);
    gfxcShowCanvas(CANVAS_BASE);
    gfxcCanvasUpdate(CANVAS_BASE);

    /* Navigation: keep the panel visible+enabled so Legato renders it into
     * canvas 1 continuously — reveal then shows an already-painted buffer with
     * no first-frame flash. Bind to OVR1 and start closed: parked off-screen to
     * the left and hidden. Open/close move the layer window; Legato's pick rect
     * follows the layer position (LE_DRIVER_LAYER_MODE), so a closed (off-screen)
     * nav can't intercept dashboard touches. The off-screen X also seeds the
     * slide-in animation. */
    Marvin_PANEL_NAVIGATION->fn->setEnabled(Marvin_PANEL_NAVIGATION, LE_TRUE);
    Marvin_PANEL_NAVIGATION->fn->setVisible(Marvin_PANEL_NAVIGATION, LE_TRUE);

    gfxcSetWindowSize(CANVAS_NAV, NAV_W, NAV_H);
    gfxcSetWindowPosition(CANVAS_NAV, -(int)NAV_W, 0);
    gfxcSetLayer(CANVAS_NAV, HW_OVR1);
    gfxcCanvasUpdate(CANVAS_NAV);
}

void Compositor_NavOpen(void)
{
    /* Buffer is already painted (panel stays visible), so just slot the layer
     * on-screen and enable it — no repaint, no flash. */
    gfxcSetWindowPosition(CANVAS_NAV, 0, 0);
    gfxcShowCanvas(CANVAS_NAV);
    gfxcCanvasUpdate(CANVAS_NAV);
}

void Compositor_NavClose(void)
{
    /* Hide the layer and park it off-screen so it stops intercepting touches. */
    gfxcHideCanvas(CANVAS_NAV);
    gfxcSetWindowPosition(CANVAS_NAV, -(int)NAV_W, 0);
    gfxcCanvasUpdate(CANVAS_NAV);
}

/* Marvin screen button events (declared in le_gen_screen_Marvin.h). The
 * navigation button lives on the dashboard (BASE); the dashboard button lives
 * in the nav menu (OVR1). */
void event_Marvin_BUTTON_SYSYEM_NAVIGATION_OnPressed(leButtonWidget* btn)
{
    (void)btn;
    Compositor_NavOpen();
}

void event_Marvin_BUTTON_NAV_DASHBOARD_OnReleased(leButtonWidget* btn)
{
    (void)btn;
    Compositor_NavClose();
}
