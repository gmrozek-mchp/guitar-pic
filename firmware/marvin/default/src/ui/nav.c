#include "ui/nav.h"

#include <stdbool.h>
#include <stdint.h>

#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"

/* Nav drawer canvas id (== Legato layer index) and its LCDC overlay layer.
 * BASE, HEO, OVR1, OVR2 in drvLayer/layerOrder order; HEO is the live camera. */
#define CANVAS_NAV   1u
#define HW_OVR1      2u

#define NAV_W   320u
#define NAV_H   800u

#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

static uint16_t FB_NOCACHE s_fb_nav[NAV_W * NAV_H];

static bool s_nav_open = false;

/* Navigation entries, in panel order. The selected one is the active screen. */
#define NAV_COUNT  6u

static leButtonWidget *nav_button(unsigned int i)
{
    switch (i)
    {
        case 0:  return Marvin_BUTTON_NAV_DASHBOARD;
        case 1:  return Marvin_BUTTON_NAV_LOGS;
        case 2:  return Marvin_BUTTON_NAV_PERFORMANCE;
        case 3:  return Marvin_BUTTON_NAV_SYSTEM_INFO;
        case 4:  return Marvin_BUTTON_NAV_DIAGNOSTICS;
        default: return Marvin_BUTTON_NAV_SETTINGS;
    }
}

/* Single released-event sink for every nav entry: highlight the tapped one
 * (single-active) by swapping schemes. The active screen-switch will hook here
 * once the per-screen canvas model lands; for now it only updates the highlight. */
static void nav_on_release(leButtonWidget *btn)
{
    unsigned int i;

    for (i = 0u; i < NAV_COUNT; i++)
    {
        leButtonWidget *b = nav_button(i);
        b->fn->setScheme(b, (b == btn) ? &SCHEME_NAV_BUTTON_SELECTED
                                       : &SCHEME_NAV_BUTTON_UNSELECTED);
    }
}

static void nav_buttons_init(void)
{
    unsigned int i;

    /* Route every nav-entry release through nav_on_release (runtime-registered
     * here rather than six MGS event stubs). */
    for (i = 0u; i < NAV_COUNT; i++)
    {
        nav_button(i)->fn->setReleasedEventCallback(nav_button(i), nav_on_release);
    }

    /* Dashboard is the active entry at startup. */
    nav_on_release(Marvin_BUTTON_NAV_DASHBOARD);
}

static void nav_open(void)
{
    /* Force a full repaint of the panel into the canvas before revealing it. The
     * initial paint queued at startup (while the layer is parked off-screen and
     * hidden) does not fully land in the buffer, so without this the drawer opens
     * partially drawn until per-widget touch damage fills it in. Invalidating here
     * — on the live, post-scheduler render path — paints the whole panel; if the
     * buffer was already complete this is a harmless repaint of the same pixels. */
    Marvin_PANEL_NAVIGATION->fn->invalidate(Marvin_PANEL_NAVIGATION);

    gfxcSetWindowPosition(CANVAS_NAV, 0, 0);
    gfxcShowCanvas(CANVAS_NAV);
    gfxcCanvasUpdate(CANVAS_NAV);
    s_nav_open = true;
}

static void nav_close(void)
{
    /* Hide the layer and park it off-screen so it stops intercepting touches. */
    gfxcHideCanvas(CANVAS_NAV);
    gfxcSetWindowPosition(CANVAS_NAV, -(int)NAV_W, 0);
    gfxcCanvasUpdate(CANVAS_NAV);
    s_nav_open = false;
}

void Nav_InitSurface(void)
{
    gfxcSetPixelBuffer(CANVAS_NAV, NAV_W, NAV_H, GFX_COLOR_MODE_RGB_565, s_fb_nav);
}

void Nav_OnShow(void)
{
    /* Keep the panel visible+enabled so Legato renders it into canvas 1
     * continuously — reveal then shows an already-painted buffer with no
     * first-frame flash. Bind to OVR1 and start closed: parked off-screen to the
     * left and hidden. Open/close move the layer window; Legato's pick rect
     * follows the layer position (LE_DRIVER_LAYER_MODE), so a closed (off-screen)
     * nav can't intercept dashboard touches. The off-screen X also seeds the
     * slide-in animation. */
    Marvin_PANEL_NAVIGATION->fn->setEnabled(Marvin_PANEL_NAVIGATION, LE_TRUE);
    Marvin_PANEL_NAVIGATION->fn->setVisible(Marvin_PANEL_NAVIGATION, LE_TRUE);

    gfxcSetWindowSize(CANVAS_NAV, NAV_W, NAV_H);
    gfxcSetWindowPosition(CANVAS_NAV, -(int)NAV_W, 0);
    gfxcSetLayer(CANVAS_NAV, HW_OVR1);
    gfxcCanvasUpdate(CANVAS_NAV);

    nav_buttons_init();
}

/* Marvin screen button events (declared in le_gen_screen_Marvin.h). */

/* Hamburger on the dashboard (BASE) toggles the nav drawer. */
void event_Marvin_BUTTON_SYSYEM_NAVIGATION_OnPressed(leButtonWidget* btn)
{
    (void)btn;
    if (s_nav_open) { nav_close(); } else { nav_open(); }
}

/* Dashboard entry (in the nav, OVR1). MGS wires only this entry's release; the
 * other five are runtime-registered in nav_buttons_init. Route it through the
 * same shared sink so all entries behave identically (select + highlight). */
void event_Marvin_BUTTON_NAV_DASHBOARD_OnReleased(leButtonWidget* btn)
{
    nav_on_release(btn);
}
