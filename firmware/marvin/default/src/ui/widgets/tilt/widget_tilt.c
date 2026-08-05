#include "ui/widgets/tilt/widget_tilt.h"

#include <math.h>

#include "gfx/legato/common/legato_color.h"
#include "gfx/legato/renderer/legato_renderer.h"

/* Shared vtable copy + captured original paint (the widget_gauge pattern). */
static leWidgetVTable s_vt;
static void (*s_orig_paint)(leWidget *);
static leBool s_vt_ready = LE_FALSE;

static leWidget     *s_arc;
static TiltChangeFn  s_on_change;
static int32_t       s_degrees;

/* Mockup colours (Tailwind): track zinc-700, fill purple-500, thumb zinc-300 with
 * a white rim. */
#define C_TRACK   0x3F3F46u
#define C_FILL    0xA855F7u
#define C_THUMB   0xD4D4D8u
#define C_RIM     0xFFFFFFu

/* Arc geometry. The centre sits INSET from the bottom-right corner so the quadrant
 * sweeping up and to the left fills the widget; MARGIN is the clearance left
 * outside the track. At the mockup's 157x157 this yields its exact numbers:
 * centre (137,137), radius 108, 38px track. */
#define INSET      20.0f
#define MARGIN     10.0f
#define STROKE     38.0f
#define THUMB_R    12.0f
#define RIM_HALF    1.25f      /* SVG stroke-width 2.5 straddles the boundary */

#define DEG2RAD   0.01745329f

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Coverage of a shape whose boundary is at sdf == 0, anti-aliased over one pixel. */
static float coverage(float sdf)
{
    return clampf(0.5f - sdf, 0.0f, 1.0f);
}

static void blend(int32_t x, int32_t y, leColor c, float cov, leColorMode mode)
{
    if (cov <= 0.0f) { return; }
    if (cov > 1.0f)  { cov = 1.0f; }

    leColor bg = leRenderer_GetPixel(x, y);
    leRenderer_PutPixel(x, y, leColorLerp(bg, c, (uint32_t)(cov * 100.0f + 0.5f), mode));
}

/* Round cap coverage: a disc of radius STROKE/2 centred on the arc endpoint at
 * `deg`, in the (dx, dy-up) frame relative to the arc centre. */
static float cap_cov(float dx, float dy, float r, float deg)
{
    float a  = deg * DEG2RAD;
    float ex = -r * cosf(a);
    float ey =  r * sinf(a);
    float ux = dx - ex;
    float uy = dy - ey;

    return coverage(sqrtf(ux * ux + uy * uy) - STROKE / 2.0f);
}

static void tilt_paint(leWidget *wgt)
{
    s_orig_paint(wgt);

    if (wgt->status.drawState != LE_WIDGET_DRAW_STATE_DONE) { return; }

    leRect rect;
    wgt->fn->rectToScreen(wgt, &rect);
    if (rect.width < 40 || rect.height < 40) { return; }

    leColorMode mode = leRenderer_CurrentColorMode();
    leColor track = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_TRACK);
    leColor fill  = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_FILL);
    leColor thumb = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_THUMB);
    leColor rim   = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_RIM);

    float cx = (float)rect.x + (float)rect.width  - INSET;
    float cy = (float)rect.y + (float)rect.height - INSET;
    float r  = fminf((float)rect.width, (float)rect.height) - INSET - MARGIN - STROKE / 2.0f;
    if (r < STROKE) { return; }

    float tilt = (float)s_degrees;

    for (int32_t py = 0; py < rect.height; py++)
    {
        for (int32_t px = 0; px < rect.width; px++)
        {
            int32_t sx = rect.x + px;
            int32_t sy = rect.y + py;
            float   dx = (float)sx + 0.5f - cx;
            float   dy = cy - ((float)sy + 0.5f);      /* positive above the centre */

            /* Annulus coverage, anti-aliased on both edges. */
            float d    = sqrtf(dx * dx + dy * dy);
            float ring = coverage(fabsf(d - r) - STROKE / 2.0f);

            /* The sweep occupies the quadrant dx <= 0, dy >= 0; the round caps cover
             * the two straight ends, so a hard quadrant test is enough here. */
            float body = (dx <= 0.0f && dy >= 0.0f) ? ring : 0.0f;

            float cov = fmaxf(body, fmaxf(cap_cov(dx, dy, r, 0.0f),
                                          cap_cov(dx, dy, r, 90.0f)));
            if (cov > 0.0f) { blend(sx, sy, track, cov, mode); }

            if (tilt > 0.0f)
            {
                /* Fill spans 0..tilt of the same annulus, round-capped at both ends. */
                float fcov = 0.0f;
                if (body > 0.0f)
                {
                    float deg = atan2f(dy, -dx) / DEG2RAD;
                    if (deg <= tilt) { fcov = body; }
                }
                fcov = fmaxf(fcov, fmaxf(cap_cov(dx, dy, r, 0.0f),
                                         cap_cov(dx, dy, r, tilt)));
                blend(sx, sy, fill, fcov, mode);
            }

            /* Thumb: white rim disc, then the body inset by half the rim width. */
            float a  = tilt * DEG2RAD;
            float ux = dx - (-r * cosf(a));
            float uy = dy - ( r * sinf(a));
            float du = sqrtf(ux * ux + uy * uy);

            blend(sx, sy, rim,   coverage(du - (THUMB_R + RIM_HALF)), mode);
            blend(sx, sy, thumb, coverage(du - (THUMB_R - RIM_HALF)), mode);
        }
    }
}

static void value_set(int32_t deg)
{
    if (deg <  0) { deg =  0; }
    if (deg > 90) { deg = 90; }
    if (deg == s_degrees) { return; }

    s_degrees = deg;

    if (s_arc != NULL) { s_arc->fn->invalidate(s_arc); }
    if (s_on_change != NULL) { s_on_change(s_degrees); }
}

/* Aim the thumb at the touch point: the angle of (touch - centre) within the
 * quadrant, ignoring distance so a touch anywhere in the widget works. */
static void value_from_point(int32_t screen_x, int32_t screen_y)
{
    leRect rect;
    s_arc->fn->rectToScreen(s_arc, &rect);

    float cx = (float)rect.x + (float)rect.width  - INSET;
    float cy = (float)rect.y + (float)rect.height - INSET;
    float dx = (float)screen_x + 0.5f - cx;
    float dy = cy - ((float)screen_y + 0.5f);

    if (dx == 0.0f && dy == 0.0f) { return; }

    value_set((int32_t)(clampf(atan2f(dy, -dx) / DEG2RAD, 0.0f, 90.0f) + 0.5f));
}

static void tilt_touchDown(leWidget *wgt, leWidgetEvent_TouchDown *evt)
{
    value_from_point(evt->x, evt->y);
    leWidgetEvent_Accept(&evt->event, wgt);
}

static void tilt_touchMove(leWidget *wgt, leWidgetEvent_TouchMove *evt)
{
    value_from_point(evt->x, evt->y);
    leWidgetEvent_Accept(&evt->event, wgt);
}

void Tilt_Enable(leWidget *arc, TiltChangeFn onChange)
{
    if (arc == NULL) { return; }

    s_arc       = arc;
    s_on_change = onChange;

    if (!s_vt_ready)
    {
        s_vt = *arc->fn;
        s_orig_paint = arc->fn->_paint;
        s_vt._paint         = tilt_paint;
        s_vt.touchDownEvent = tilt_touchDown;
        s_vt.touchMoveEvent = tilt_touchMove;
        s_vt_ready = LE_TRUE;
    }
    arc->fn = &s_vt;
}

int32_t Tilt_Degrees(void)
{
    return s_degrees;
}

void Tilt_Set(int32_t degrees)
{
    value_set(degrees);
}
