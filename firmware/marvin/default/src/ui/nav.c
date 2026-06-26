#include "ui/nav.h"

#include <stdbool.h>
#include <stdint.h>

#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/screen/le_gen_screen_Dashboard.h"   /* hamburger event decl */
#include "gfx/legato/generated/screen/le_gen_screen_Navigation.h"  /* nav widgets + OnShow */

/* The nav drawer is authored as its own MGS Screen (Navigation) and hosted as a
 * resident overlay on Legato layer 1 → canvas 1 → OVR1. Canvas id == Legato
 * layer index (baseCanvasID 0); HW layer indices are BASE 0, HEO 1, OVR1 2,
 * OVR2 3 (HEO is the live camera). */
#define NAV_LAYER    1u   /* Legato layer / canvas id we host the drawer on */
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
        case 0:  return Navigation_BUTTON_NAV_DASHBOARD;
        case 1:  return Navigation_BUTTON_NAV_LOGS;
        case 2:  return Navigation_BUTTON_NAV_PERFORMANCE;
        case 3:  return Navigation_BUTTON_NAV_SYSTEM_INFO;
        case 4:  return Navigation_BUTTON_NAV_DIAGNOSTICS;
        default: return Navigation_BUTTON_NAV_SETTINGS;
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
     * here — the Navigation screen wires no button events itself). */
    for (i = 0u; i < NAV_COUNT; i++)
    {
        nav_button(i)->fn->setReleasedEventCallback(nav_button(i), nav_on_release);
    }

    /* Dashboard is the active entry at startup. */
    nav_on_release(Navigation_BUTTON_NAV_DASHBOARD);
}

static void nav_open(void)
{
    /* Force a full repaint of the panel into the canvas before revealing it. The
     * initial paint queued at startup (while the layer is parked off-screen and
     * hidden) does not fully land in the buffer, so without this the drawer opens
     * partially drawn until per-widget touch damage fills it in. Invalidating here
     * — on the live, post-scheduler render path — paints the whole panel; if the
     * buffer was already complete this is a harmless repaint of the same pixels. */
    Navigation_PANEL_NAVIGATION->fn->invalidate(Navigation_PANEL_NAVIGATION);

    gfxcSetWindowPosition(NAV_LAYER, 0, 0);
    gfxcShowCanvas(NAV_LAYER);
    gfxcCanvasUpdate(NAV_LAYER);
    s_nav_open = true;
}

static void nav_close(void)
{
    /* Hide the layer and park it off-screen so it stops intercepting touches. */
    gfxcHideCanvas(NAV_LAYER);
    gfxcSetWindowPosition(NAV_LAYER, -(int)NAV_W, 0);
    gfxcCanvasUpdate(NAV_LAYER);
    s_nav_open = false;
}

void Nav_InitSurface(void)
{
    gfxcSetPixelBuffer(NAV_LAYER, NAV_W, NAV_H, GFX_COLOR_MODE_RGB_565, s_fb_nav);
}

/* Navigation screen composition root (declared in le_gen_screen_Navigation.h),
 * raised by screenShow_Navigation. The screen authors its content on its own
 * layer 0; re-host that root onto NAV_LAYER so the drawer composites on OVR1
 * while the dashboard keeps layer 0. Then keep the panel visible+enabled so
 * Legato renders it into canvas NAV_LAYER continuously, bind to OVR1, and start
 * closed (parked off-screen + hidden); Legato's pick rect follows the layer
 * position (LE_DRIVER_LAYER_MODE), so a closed (off-screen) nav can't intercept
 * dashboard touches. The off-screen X also seeds the slide-in animation. */
void Navigation_OnShow(void)
{
    leWidget *root = screenGetRoot_Navigation(0);
    leRemoveRootWidget(root, 0);
    leAddRootWidget(root, NAV_LAYER);

    Navigation_PANEL_NAVIGATION->fn->setEnabled(Navigation_PANEL_NAVIGATION, LE_TRUE);
    Navigation_PANEL_NAVIGATION->fn->setVisible(Navigation_PANEL_NAVIGATION, LE_TRUE);

    gfxcSetWindowSize(NAV_LAYER, NAV_W, NAV_H);
    gfxcSetWindowPosition(NAV_LAYER, -(int)NAV_W, 0);
    gfxcSetLayer(NAV_LAYER, HW_OVR1);
    gfxcCanvasUpdate(NAV_LAYER);

    nav_buttons_init();
}

/* Hamburger on the Dashboard screen (BASE) toggles the nav drawer. */
void event_Dashboard_BUTTON_SYSYEM_NAVIGATION_OnPressed(leButtonWidget* btn)
{
    (void)btn;
    if (s_nav_open) { nav_close(); } else { nav_open(); }
}
