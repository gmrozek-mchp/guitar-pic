#include "ui/screens/video/screen_video.h"

#include "ui/ui_manager.h"   /* BASE_W, BASE_H, UiManager_VideoShow, overlay verbs */
#include "ui/screens/dashboard/screen_dashboard.h"   /* ScreenDashboard_Titlebar */

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "gfx/legato/widget/legato_widget.h"                     /* leWidget vtable + touch event */
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* dashboard panels */

/* Tap the live video to toggle fullscreen. The video is on HEO, which Legato
 * cannot pick, so the tap is caught on Marvin_PANEL_DASHBOARD — the full-screen
 * layer-0 panel every dashboard tap bubbles up to unless an interactive widget
 * accepts it first. Windowed: a tap inside the video rect enters fullscreen.
 * Fullscreen: DASHBOARD_TOP/DASHBOARD_BOTTOM picking is gated off (below) so every
 * tap resolves to the dashboard panel and exits. */

static bool s_fullscreen;

/* ── rounded anti-aliased video frame (OVR1, above HEO) ───────────────────────
 * A static ARGB_4444 surface the size of the windowed video, composited over the
 * video by the LCDC. Mostly transparent (video shows through); the four corners
 * are cut to opaque black (= the dashboard backdrop, so the video's square corners
 * read as rounded) and a 1px stroke frames it. Per-pixel alpha (4-bit → 16 levels)
 * gives the corner/edge AA. Filled once at setup; the compositor just binds/unbinds
 * OVR1 to it. */
#define BORDER_RADIUS   6.0f
#define BORDER_STROKE   1.0f              /* stroke thickness, px            */
#define BORDER_C4       4u                /* 0x40 → 4-bit; stroke 0x404040    */

#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))
static uint16_t FB_NOCACHE s_border_fb[SCREEN_VIDEO_WIN_W * SCREEN_VIDEO_WIN_H];

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/* Signed distance to the rounded-rect boundary (negative inside), iq's rounded-box
 * SDF, for pixel-centre (x+0.5, y+0.5) in a WxH rect with corner radius R. */
static float rrect_sdf(int x, int y, float w, float h, float r)
{
    float px = (float)x + 0.5f - w * 0.5f;
    float py = (float)y + 0.5f - h * 0.5f;
    float qx = fabsf(px) - (w * 0.5f - r);
    float qy = fabsf(py) - (h * 0.5f - r);
    float ox = qx > 0.0f ? qx : 0.0f;
    float oy = qy > 0.0f ? qy : 0.0f;
    float outside = sqrtf(ox * ox + oy * oy);
    float inside  = fminf(fmaxf(qx, qy), 0.0f);
    return outside + inside - r;
}

/* Fill the frame surface once. For each pixel: outer coverage Co (inside the outer
 * rounded-rect boundary) and inner coverage Ci (inside the boundary inset by the
 * stroke). alpha = 1-Ci (opaque over the cut+stroke, transparent over the video);
 * the opaque colour is the stroke scaled by its share of the opaque part (the rest
 * is the black cut). */
static void border_fill(void)
{
    const float w = (float)SCREEN_VIDEO_WIN_W;
    const float h = (float)SCREEN_VIDEO_WIN_H;

    for (uint32_t y = 0u; y < SCREEN_VIDEO_WIN_H; y++)
    {
        for (uint32_t x = 0u; x < SCREEN_VIDEO_WIN_W; x++)
        {
            float d  = rrect_sdf((int)x, (int)y, w, h, BORDER_RADIUS);
            float Co = clamp01(0.5f - d);
            float Ci = clamp01(0.5f - d - BORDER_STROKE);
            float a  = 1.0f - Ci;

            uint16_t px;
            if (a <= 0.0f)
            {
                px = 0x0000u;                       /* transparent — video shows */
            }
            else
            {
                float stroke_frac = (Co - Ci) / a;  /* opaque part that is stroke */
                uint32_t c4 = (uint32_t)(BORDER_C4 * stroke_frac + 0.5f);
                uint32_t a4 = (uint32_t)(a * 15.0f + 0.5f);
                if (c4 > 15u) { c4 = 15u; }
                if (a4 > 15u) { a4 = 15u; }
                px = (uint16_t)((a4 << 12) | (c4 << 8) | (c4 << 4) | c4);
            }
            s_border_fb[y * SCREEN_VIDEO_WIN_W + x] = px;
        }
    }
}

static leWidgetVTable s_dash_vt;
static void (*s_dash_touch)(leWidget *, leWidgetEvent_TouchDown *);
static leBool s_dash_vt_ready = LE_FALSE;

/* Gate the dashboard's interactive widgets from picking without repainting them.
 * All interactive widgets live under DASHBOARD_TOP (header) and DASHBOARD_BOTTOM
 * (control columns + video); leUtils_PickFromWidget descends only into ENABLED
 * children, so clearing the flag on those two makes every tap resolve to the
 * dashboard panel. Toggle the flag directly (not setEnabled) so the surface isn't
 * invalidated — same pick-only gate as ui_manager's panel_set_pickable. */
static void set_dashboard_input(bool on)
{
    leWidget *titlebar = ScreenDashboard_Titlebar();   /* shared component, built in code */

    if (on)
    {
        if (titlebar != NULL) { titlebar->flags |= LE_WIDGET_ENABLED; }
        Marvin_PANEL_DASHBOARD_BOTTOM->flags |= LE_WIDGET_ENABLED;
    }
    else
    {
        if (titlebar != NULL) { titlebar->flags &= ~LE_WIDGET_ENABLED; }
        Marvin_PANEL_DASHBOARD_BOTTOM->flags &= ~LE_WIDGET_ENABLED;
    }
}

static void enter_fullscreen(void)
{
    UiManager_VideoOverlayHide();               /* edge-to-edge; no frame */
    UiManager_VideoShow(0u, 0u, BASE_W, BASE_H);
    set_dashboard_input(false);
    s_fullscreen = true;
}

void ScreenVideo_ShowWindowed(void)
{
    set_dashboard_input(true);
    UiManager_VideoShow(SCREEN_VIDEO_WIN_X, SCREEN_VIDEO_WIN_Y,
                        SCREEN_VIDEO_WIN_W, SCREEN_VIDEO_WIN_H);
    UiManager_VideoOverlayShow(s_border_fb, SCREEN_VIDEO_WIN_X, SCREEN_VIDEO_WIN_Y,
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
    border_fill();

    if (!s_dash_vt_ready)
    {
        s_dash_vt = *Marvin_PANEL_DASHBOARD->fn;
        s_dash_touch = Marvin_PANEL_DASHBOARD->fn->touchDownEvent;
        s_dash_vt.touchDownEvent = dashboard_touchDown;
        s_dash_vt_ready = LE_TRUE;
    }
    Marvin_PANEL_DASHBOARD->fn = &s_dash_vt;
}
