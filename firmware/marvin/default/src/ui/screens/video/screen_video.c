#include "ui/screens/video/screen_video.h"

#include "ui/ui_manager.h"   /* BASE_W, BASE_H, UiManager_VideoShow */

#include <stdbool.h>

#include "gfx/legato/widget/legato_widget.h"                     /* leWidget vtable + touch event */
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* dashboard panels */

/* Tap the live video to toggle fullscreen. The video is on HEO, which Legato
 * cannot pick, so the tap is caught on Marvin_PANEL_DASHBOARD — the full-screen
 * layer-0 panel every dashboard tap bubbles up to unless an interactive widget
 * accepts it first. Windowed: a tap inside the video rect enters fullscreen.
 * Fullscreen: BASE_TOP/BASE_BOTTOM picking is gated off (below) so every tap
 * resolves to the dashboard panel and exits. */

static bool s_fullscreen;

static leWidgetVTable s_dash_vt;
static void (*s_dash_touch)(leWidget *, leWidgetEvent_TouchDown *);
static leBool s_dash_vt_ready = LE_FALSE;

/* Gate the dashboard's interactive widgets from picking without repainting them.
 * All interactive widgets live under BASE_TOP (header) and BASE_BOTTOM (control
 * columns + video + guitar); leUtils_PickFromWidget descends only into ENABLED
 * children, so clearing the flag on those two makes every tap resolve to the
 * dashboard panel. Toggle the flag directly (not setEnabled) so the surface isn't
 * invalidated — same pick-only gate as ui_manager's panel_set_pickable. */
static void set_dashboard_input(bool on)
{
    if (on)
    {
        Marvin_PANEL_BASE_TOP->flags    |= LE_WIDGET_ENABLED;
        Marvin_PANEL_BASE_BOTTOM->flags |= LE_WIDGET_ENABLED;
    }
    else
    {
        Marvin_PANEL_BASE_TOP->flags    &= ~LE_WIDGET_ENABLED;
        Marvin_PANEL_BASE_BOTTOM->flags &= ~LE_WIDGET_ENABLED;
    }
}

static void enter_fullscreen(void)
{
    UiManager_VideoShow(0u, 0u, BASE_W, BASE_H);
    set_dashboard_input(false);
    s_fullscreen = true;
}

void ScreenVideo_ShowWindowed(void)
{
    set_dashboard_input(true);
    UiManager_VideoShow(SCREEN_VIDEO_WIN_X, SCREEN_VIDEO_WIN_Y,
                        SCREEN_VIDEO_WIN_W, SCREEN_VIDEO_WIN_H);
    s_fullscreen = false;
}

static bool in_video_win(int32_t x, int32_t y)
{
    return x >= (int32_t)SCREEN_VIDEO_WIN_X &&
           x <  (int32_t)(SCREEN_VIDEO_WIN_X + SCREEN_VIDEO_WIN_W) &&
           y >= (int32_t)SCREEN_VIDEO_WIN_Y &&
           y <  (int32_t)(SCREEN_VIDEO_WIN_Y + SCREEN_VIDEO_WIN_H);
}

static void dashboard_touchDown(leWidget *wgt, leWidgetEvent_TouchDown *evt)
{
    if (s_dash_touch != NULL) { s_dash_touch(wgt, evt); }

    if (s_fullscreen)
    {
        ScreenVideo_ShowWindowed();
        leWidgetEvent_Accept(&evt->event, wgt);
    }
    else if (in_video_win(evt->x, evt->y))
    {
        enter_fullscreen();
        leWidgetEvent_Accept(&evt->event, wgt);
    }
}

void ScreenVideo_Setup(void)
{
    if (!s_dash_vt_ready)
    {
        s_dash_vt = *Marvin_PANEL_DASHBOARD->fn;
        s_dash_touch = Marvin_PANEL_DASHBOARD->fn->touchDownEvent;
        s_dash_vt.touchDownEvent = dashboard_touchDown;
        s_dash_vt_ready = LE_TRUE;
    }
    Marvin_PANEL_DASHBOARD->fn = &s_dash_vt;
}
