#include "ui/screens/dashboard/screen_dashboard.h"

#include "ui/ui_manager.h"   /* CANVAS_DASH, BASE_W, BASE_H, UiManager_OpenSongSelect */
#include "ui/widgets/panel_aa/widget_panel_aa.h"

#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/widget/legato_widget.h"                     /* leWidget vtable + touch event */
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* dashboard widgets */

/* Dashboard surface, non-cached so the 2D engine and LCDC DMA read CPU-rendered
 * pixels coherently; 32-byte aligned. RGB565 — steady-state UI needs no more. */
#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

static uint16_t FB_NOCACHE s_fb[BASE_W * BASE_H];

void ScreenDashboard_InitSurface(void)
{
    gfxcSetPixelBuffer(CANVAS_DASH, BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565, s_fb);
}

/* Corner radius for the dashboard's rounded cards. */
#define DASH_CARD_RADIUS  4u

/* Tap the top header bar to open the song-select dialog. The header panels are
 * plain leWidgets with no touch behaviour of their own, so we re-point their
 * (shared) vtable at a copy whose touchDownEvent opens the dialog. A tap is
 * delivered to the topmost picked widget without bubbling, so the hamburger button
 * and title labels — picked first within the header — keep their own behaviour;
 * only taps on the bare header panels open the dialog. */
static leWidgetVTable s_header_vt;
static void (*s_header_touch)(leWidget *, leWidgetEvent_TouchDown *);
static leBool s_header_vt_ready = LE_FALSE;

static void header_touchDown(leWidget *wgt, leWidgetEvent_TouchDown *evt)
{
    if (s_header_touch != NULL) { s_header_touch(wgt, evt); }
    UiManager_OpenSongSelect();
}

static void header_tap_opens(leWidget *w)
{
    if (!s_header_vt_ready)
    {
        s_header_vt = *w->fn;
        s_header_touch = w->fn->touchDownEvent;
        s_header_vt.touchDownEvent = header_touchDown;
        s_header_vt_ready = LE_TRUE;
    }
    w->fn = &s_header_vt;
}

void ScreenDashboard_Setup(void)
{
    /* Full-screen at the origin. The root is on Legato layer 0 (built by MGS); the
     * canvas window positions that layer's pixels on the display. */
    gfxcSetWindowPosition(CANVAS_DASH, 0, 0);
    gfxcSetWindowSize(CANVAS_DASH, BASE_W, BASE_H);

    /* Rounded, anti-aliased corners on the robot-controls card — a child of the
     * opaque BASE_LEFT panel, so the corner backdrop is solid. */
    Marvin_PANEL_ROBOT_CONTROLS->fn->setCornerRadius(Marvin_PANEL_ROBOT_CONTROLS, DASH_CARD_RADIUS);
    PanelAA_Enable(Marvin_PANEL_ROBOT_CONTROLS);

    /* Header-bar tap opens song-select (across the bare header panels). */
    header_tap_opens(Marvin_PANEL_BASE_TOP);
    header_tap_opens(Marvin_PANEL_SYSTEM_LEFT);
    header_tap_opens(Marvin_PANEL_SYSTEM_TITLE);
    header_tap_opens(Marvin_PANEL_SYSTEM_RIGHT);
}
