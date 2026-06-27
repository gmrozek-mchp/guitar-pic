#include "ui/ui_manager.h"
#include "ui/screens/nav/screen_nav.h"

#include <stdint.h>

#include "definitions.h"   /* XLCDC_SetLayerRGBColorMode, AC69T88A_BACKLIGHT_EN_Set */
#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/generated/le_gen_assets.h"
#include "gfx/legato/generated/screen/le_gen_screen_Dashboard.h"
#include "gfx/legato/generated/screen/le_gen_screen_Navigation.h"

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

/* Splash fallback fill: opaque black. The splash canvas is RGBA_8888, which packs
 * 0xRRGGBBAA, so a little-endian pixel word of 0x000000FF is R=G=B=0, A=0xFF. */
#define SPLASH_FILL   0x000000FFu

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

    /* Splash: a raw full-screen RGBA8888 bitmap, NOT a Legato-rendered screen.
     * The loader reads splash.raw off the SD straight into this canvas's buffer
     * (s_fb_splash), which OVR2 scans out directly — so there's no widget, no
     * render, and no blit for the splash (a full-screen software paint cost
     * ~1.5 s). We only set the canvas up and bind it to OVR2 here; the MGS Splash
     * screen is unused. Pre-fill opaque black as the fallback if the SD load fails. */
    for (uint32_t i = 0u; i < (BASE_W * BASE_H); i++) { s_fb_splash[i] = SPLASH_FILL; }
    gfxcSetWindowPosition(CANVAS_SPLASH, 0, 0);
    gfxcSetWindowSize(CANVAS_SPLASH, BASE_W, BASE_H);
    gfxcSetLayer(CANVAS_SPLASH, HW_OVR2);
    gfxcShowCanvas(CANVAS_SPLASH);
    gfxcCanvasUpdate(CANVAS_SPLASH);

    /* Commit the OVR2 hardware color mode to 32bpp. The GFX-XLCDC driver's canvas
     * commit path (drv_gfx_xlcdc.c SET_LAYER_UNLOCK) never writes RGBMODE — it
     * assumes every layer is the project framebuffer format (RGB565) — so OVR2
     * would read the 32bpp splash buffer as RGB565 → garbled. Set it directly; the
     * driver never rewrites RGBMODE, so it sticks (same poke video.c uses for HEO). */
    XLCDC_SetLayerRGBColorMode(XLCDC_LAYER_OVR2, XLCDC_RGB_COLOR_MODE_RGBA_8888, true);

    /* Build + host the dashboard + nav now, pre-scheduler (no render-task
     * concurrency); they're persistent, built once. Dashboard keeps its authored
     * canvas 0 (RGB565, bound to BASE); Navigation_OnShow re-hosts the nav onto
     * canvas 1 (bound to OVR1). They paint behind the OVR2 splash. */
    screenInit_Dashboard();
    screenShow_Dashboard();    /* Dashboard_OnShow binds canvas 0 to BASE */
    screenInit_Navigation();
    screenShow_Navigation();   /* Navigation_OnShow re-hosts nav → OVR1 */

    /* Detach the dashboard and nav roots so that when the scheduler starts the
     * render task paints only the splash — the panel comes up as fast as the
     * card + JPEG decode allow, not gated behind the 200+ widget dashboard. Both
     * trees are fully built and their canvases bound (BASE / OVR1); the loader
     * re-attaches them with UiManager_AttachMainScreens once the splash is lit,
     * so they paint behind the opaque OVR2 splash while it's held. Detaching here
     * is pre-scheduler, so there's no race with the render task. */
    leRemoveRootWidget(screenGetRoot_Dashboard(0),  CANVAS_DASH);
    leRemoveRootWidget(screenGetRoot_Navigation(0), CANVAS_NAV);
}

/* Bring the dashboard + nav into the render path, behind the splash. Called by
 * the loader after the splash is painted and the backlight is on: the heavy
 * dashboard paint then happens while the splash is held, off the
 * splash-to-screen critical path. invalidate() forces a full first paint (the
 * trees were detached before the scheduler, so they've never been drawn). The
 * add-root mirrors UiManager_RevealDashboard's post-scheduler remove-root. */
void UiManager_AttachMainScreens(void)
{
    leWidget *dash = screenGetRoot_Dashboard(0);
    leAddRootWidget(dash, CANVAS_DASH);
    dash->fn->invalidate(dash);

    leWidget *nav = screenGetRoot_Navigation(0);
    leAddRootWidget(nav, CANVAS_NAV);
    nav->fn->invalidate(nav);
}

/* The splash canvas's pixel buffer (OVR2 scanout). The loader reads splash.raw
 * straight into this — it's RGBA8888 in the layer's native byte order, so the
 * read IS the load; no decode or blit. *bytes (if non-NULL) = its size. */
void *UiManager_SplashFramebuffer(uint32_t *bytes)
{
    if (bytes != NULL) { *bytes = (uint32_t)sizeof(s_fb_splash); }
    return s_fb_splash;
}

/* Re-latch the splash canvas after the loader writes new pixels into it, so OVR2
 * scans the updated buffer. */
void UiManager_CommitSplash(void)
{
    gfxcCanvasUpdate(CANVAS_SPLASH);
}

/* Reveal the (already painted, behind-the-splash) dashboard by dropping the
 * splash overlay. The dashboard canvas is already bound to BASE and full; hiding
 * OVR2 uncovers it instantly. Called by the loader once the dashboard is painted.
 * The splash has no Legato root (it's a raw framebuffer), so there's nothing in
 * the input pick path to detach — just hide the canvas, freeing OVR2 for the
 * future modal dialog. */
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
