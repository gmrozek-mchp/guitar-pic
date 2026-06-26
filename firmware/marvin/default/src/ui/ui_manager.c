#include "ui/ui_manager.h"
#include "ui/screens/nav/screen_nav.h"
#include "ui/screens/splash/screen_splash.h"

#include <stdint.h>

#include "definitions.h"   /* XLCDC_SetLayerRGBColorMode, AC69T88A_BACKLIGHT_EN_Set */
#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/generated/le_gen_assets.h"
#include "gfx/legato/generated/screen/le_gen_screen_Dashboard.h"
#include "gfx/legato/generated/screen/le_gen_screen_Navigation.h"
#include "gfx/legato/generated/screen/le_gen_screen_Splash.h"

/* A canvas is a RAM surface Legato renders into; each screen has its own
 * (the CANVAS_* ids below — gfxcSetPixelBuffer assigns each one's buffer).
 * A hardware layer is what the LCDC scans out. These are independent: at
 * runtime we bind a canvas onto whatever hardware layer suits its current need
 * with gfxcSetLayer(canvasId, hwLayer), so a canvas id is unrelated to a hw
 * layer index. Screens are persistent (built once, never torn down), so a canvas
 * keeps its last-painted pixels; switching screens just re-binds canvases to
 * hardware layers.
 *
 *   canvas          bound to hw layer        color mode
 *   CANVAS_DASH      BASE                     RGB565
 *   CANVAS_NAV       OVR1                     RGB565   (owned by ui/nav)
 *   CANVAS_SPLASH    OVR2 (topmost)           RGBA8888
 *
 * The splash binds to the topmost overlay (OVR2) so it covers the dashboard
 * (on BASE) while the dashboard paints underneath, then is unbound to reveal it. */
#define CANVAS_DASH    0u
#define CANVAS_NAV     1u
#define CANVAS_SPLASH  2u

/* LCDC hardware-layer indices (drvLayer/layerOrder): BASE 0, HEO 1, OVR1 2,
 * OVR2 3. HEO is the live camera (off-limits). */
#define HW_BASE   0u
#define HW_OVR2   3u

#define BASE_W   1280u
#define BASE_H    800u

#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

/* Per-screen surfaces, non-cached so the 2D engine and LCDC DMA read CPU-
 * rendered pixels coherently (matches FB_CACHE_NC). The dashboard is RGB565
 * (steady-state UI needs no more); the splash is 32bpp so the photo keeps full
 * color depth. The splash buffer is also the natural decode scratch for the
 * (future) album-art pre-load, once the splash leaves the screen. */
static uint16_t FB_NOCACHE s_fb_base[BASE_W * BASE_H];
static uint32_t FB_NOCACHE s_fb_splash[BASE_W * BASE_H];

void UiManager_Initialize(void)
{
    /* Assign the per-screen canvas surfaces before the canvas runs. The nav owns
     * its own (canvas 1, via Nav_InitSurface). */
    gfxcSetPixelBuffer(CANVAS_DASH,   BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565,   s_fb_base);
    gfxcSetPixelBuffer(CANVAS_SPLASH, BASE_W, BASE_H, GFX_COLOR_MODE_RGBA_8888, s_fb_splash);
    Nav_InitSurface();

    /* Drive the canvas state machine to RUNNING now, before the scheduler, so a
     * blit into a canvas is not dropped by the RUNNING gate in GFXC_BlitBuffer. */
    GFX_CANVAS_Task();

    leSetStringTable(&stringTable);
    initializeStrings();

    /* Build + host every screen now, each on its own canvas. All run before the
     * scheduler, so there's no concurrency with the Legato render task — it
     * paints them (over several frames) once the scheduler is up; the loader just
     * waits for that to finish before lighting the panel. Screens are persistent,
     * so they're built once and keep their pixels.
     *
     * Splash: its MGS screen authors the root on canvas 0, so move it to
     * CANVAS_SPLASH, add the image widget, and bind that canvas to OVR2. OVR2 is
     * the topmost overlay, full-screen, so it covers the dashboard until the
     * loader unbinds it. */
    screenInit_Splash();
    leWidget *sp = screenGetRoot_Splash(0);
    leRemoveRootWidget(sp, 0);
    leAddRootWidget(sp, CANVAS_SPLASH);
    leSetLayerColorMode(CANVAS_SPLASH, LE_COLOR_MODE_RGBA_8888);
    Splash_AttachImage();

    gfxcSetWindowPosition(CANVAS_SPLASH, 0, 0);
    gfxcSetWindowSize(CANVAS_SPLASH, BASE_W, BASE_H);
    gfxcSetLayer(CANVAS_SPLASH, HW_OVR2);
    gfxcShowCanvas(CANVAS_SPLASH);
    gfxcCanvasUpdate(CANVAS_SPLASH);

    /* Commit the OVR2 hardware color mode to 32bpp. The GFX-XLCDC driver's
     * canvas commit path (drv_gfx_xlcdc.c SET_LAYER_UNLOCK) never writes
     * RGBMODE — it assumes every layer is the project framebuffer format
     * (FB_COL_MODE = RGB565) and only stages our RGBA8888 request in
     * drvLayer.pixelformat, then drops it. So OVR2 would read the 32bpp splash
     * buffer as RGB565 → garbled. Set the mode directly; the driver never
     * rewrites RGBMODE, so it sticks (same runtime XLCDC poke video.c uses for
     * HEO). BASE/OVR1 stay RGB565 = the default, so they need no poke. */
    XLCDC_SetLayerRGBColorMode(XLCDC_LAYER_OVR2, XLCDC_RGB_COLOR_MODE_RGBA_8888, true);

    /* Dashboard keeps its authored canvas 0 (bound to BASE); Navigation_OnShow
     * moves the nav onto canvas 1 (bound to OVR1). Built after the splash so
     * canvas 0's render color mode ends up RGB565 (the splash's screenInit set
     * canvas 0 to 8888 before we moved the splash off it). They paint behind the
     * OVR2 splash. */
    screenInit_Dashboard();
    screenShow_Dashboard();    /* Dashboard_OnShow binds canvas 0 to BASE */
    screenInit_Navigation();
    screenShow_Navigation();   /* Navigation_OnShow re-hosts nav → OVR1 */
}

/* Reveal the (already painted, behind-the-splash) dashboard by dropping the
 * splash overlay. The dashboard canvas is already bound to BASE and full; hiding
 * OVR2 uncovers it instantly. Called by the loader once rendering is idle. */
void UiManager_RevealDashboard(void)
{
    gfxcHideCanvas(CANVAS_SPLASH);
    gfxcCanvasUpdate(CANVAS_SPLASH);
}

/* Turn on the LCD backlight. The backlight is a plain GPIO enable
 * (AC69T88A_BACKLIGHT_EN / PC18), NOT the LCDC PWM that XLCDC_EnableBacklight()
 * drives — that PWM output isn't wired to this board's backlight, so enabling it
 * does nothing visible. Drive the enable pin directly instead. The pin starts
 * low at boot (MCC PIO config), so the panel stays dark until the loader calls
 * this once the splash is painted — no pre-splash frame. Active-high assumed
 * (the _Set name == enable); flip to _Clear if the board is active-low. */
void UiManager_EnableBacklight(void)
{
    AC69T88A_BACKLIGHT_EN_Set();
}

/* Dashboard screen composition root (declared in le_gen_screen_Dashboard.h).
 * Binds the dashboard canvas to the BASE hardware layer. The nav drawer is its own screen
 * (Navigation), hosted by ui/nav from its own OnShow hook. */
void Dashboard_OnShow(void)
{
    gfxcSetWindowPosition(CANVAS_DASH, 0, 0);
    gfxcSetWindowSize(CANVAS_DASH, BASE_W, BASE_H);
    gfxcSetLayer(CANVAS_DASH, HW_BASE);
    gfxcShowCanvas(CANVAS_DASH);
    gfxcCanvasUpdate(CANVAS_DASH);
}
