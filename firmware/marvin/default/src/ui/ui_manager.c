#include "ui/ui_manager.h"
#include "ui/nav.h"

#include <stdint.h>

#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/generated/le_gen_assets.h"
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"

/* Canvas id == Legato layer index (baseCanvasID is 0). XLCDC layer indices in
 * drvLayer/layerOrder order: BASE, HEO, OVR1, OVR2 — HEO is the live camera and
 * is off-limits. The dashboard is the full-screen base view. */
#define CANVAS_BASE   0u
#define HW_BASE       0u

#define BASE_W   1280u
#define BASE_H   800u

#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

static uint16_t FB_NOCACHE s_fb_base[BASE_W * BASE_H];

void UiManager_Initialize(void)
{
    /* Own the BASE canvas surface: RGB565 to match the panel, non-cached so the
     * 2D engine and LCDC DMA read CPU-rendered pixels coherently (matches
     * FB_CACHE_NC). Replaces the generated NULL-buffer template. Each overlay
     * module owns its own surface; assign them before the canvas runs. */
    gfxcSetPixelBuffer(CANVAS_BASE, BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565, s_fb_base);
    Nav_InitSurface();

    /* Drive the canvas state machine to RUNNING now, before the scheduler, so
     * Legato's first blit into a canvas is not dropped by the RUNNING gate in
     * GFXC_BlitBuffer. With effects disabled the canvas task otherwise flips
     * this only on its first tick, which races Legato's first render. */
    GFX_CANVAS_Task();

    /* The MGS screen state machine is disabled (no le_gen_init.c), so we own
     * screen startup: install the string table, build the Marvin screen, and
     * show it. screenShow_Marvin attaches the layer roots and raises
     * Marvin_OnShow. Runs after Legato_Initialize and before the first leUpdate. */
    leSetStringTable(&stringTable);
    initializeStrings();
    screenInit_Marvin();
    screenShow_Marvin();
}

/* Marvin screen composition root (declared in le_gen_screen_Marvin.h). Binds the
 * dashboard to the BASE layer and hands its overlay to the nav module. */
void Marvin_OnShow(void)
{
    gfxcSetWindowPosition(CANVAS_BASE, 0, 0);
    gfxcSetWindowSize(CANVAS_BASE, BASE_W, BASE_H);
    gfxcSetLayer(CANVAS_BASE, HW_BASE);
    gfxcShowCanvas(CANVAS_BASE);
    gfxcCanvasUpdate(CANVAS_BASE);

    Nav_OnShow();
}
