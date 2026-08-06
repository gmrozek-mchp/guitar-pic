#include "ui/widgets/whammy/widget_whammy.h"

#include <math.h>

#include "gfx/legato/common/legato_color.h"
#include "gfx/legato/renderer/legato_renderer.h"

/* Shared vtable copy + captured original paint (the widget_gauge pattern). */
static leWidgetVTable s_vt;
static void (*s_orig_paint)(leWidget *);
static leBool s_vt_ready = LE_FALSE;

static leWidget       *s_track;
static WhammyChangeFn  s_on_change;
static int32_t         s_value;      /* -100 .. +100, 0 = rest */

/* Mockup colours (Tailwind): track zinc-800, tick zinc-600, fill purple-600,
 * thumb purple-200 with a purple-100 rim. */
#define C_TRACK   0x27272Au
#define C_TICK    0x52525Cu
#define C_FILL    0x9810FAu
#define C_THUMB   0xDAB2FFu
#define C_RIM     0xF3E8FFu

#define THUMB_W   16.0f
#define THUMB_H   40.0f
#define RIM_PX     2.0f

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Signed distance from (px,py) to a rounded rect centred at (cx,cy) with half-
 * extents (hw,hh) and corner radius r. Negative inside. r = hh gives a capsule. */
static float rrect_sdf(float px, float py, float cx, float cy,
                       float hw, float hh, float r)
{
    float qx = fabsf(px - cx) - (hw - r);
    float qy = fabsf(py - cy) - (hh - r);
    float ox = qx > 0.0f ? qx : 0.0f;
    float oy = qy > 0.0f ? qy : 0.0f;
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f) - r;
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

static void whammy_paint(leWidget *wgt)
{
    s_orig_paint(wgt);

    if (wgt->status.drawState != LE_WIDGET_DRAW_STATE_DONE) { return; }

    leRect rect;
    leRect scan;
    wgt->fn->rectToScreen(wgt, &rect);
    if (rect.width < 16 || rect.height < 8) { return; }

    /* Scan only the damaged part of the widget — the geometry below still comes from
     * the full rect, but a drag step damages a narrow column band, and every pixel
     * outside it would have its write culled after paying for the coverage maths. */
    leRenderer_GetClipRect(&scan);
    leRectClip(&rect, &scan, &scan);
    if (scan.width <= 0 || scan.height <= 0) { return; }

    leColorMode mode = leRenderer_CurrentColorMode();
    leColor track = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_TRACK);
    leColor tick  = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_TICK);
    leColor fill  = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_FILL);
    leColor thumb = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_THUMB);
    leColor rim   = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_RIM);

    float hw = (float)rect.width  / 2.0f;
    float hh = (float)rect.height / 2.0f;
    float cx = (float)rect.x + hw;
    float cy = (float)rect.y + hh;

    /* Thumb centre travels the track minus half a thumb at each end, so the thumb
     * stays inside the capsule at the extremes. */
    float span   = hw - THUMB_W / 2.0f;
    float thumbx = cx + span * (float)s_value / 100.0f;

    /* Fill capsule spans centre -> thumb centre; degenerate (zero width) at rest. */
    float fa = fminf(cx, thumbx);
    float fb = fmaxf(cx, thumbx);
    float fill_hw = (fb - fa) / 2.0f + hh;      /* pad to a capsule end cap */
    float fill_cx = (fa + fb) / 2.0f;

    for (int32_t sy = scan.y; sy < scan.y + scan.height; sy++)
    {
        for (int32_t sx = scan.x; sx < scan.x + scan.width; sx++)
        {
            float fx = (float)sx + 0.5f;
            float fy = (float)sy + 0.5f;

            /* Track capsule; everything else is clipped to it. */
            float d_track = rrect_sdf(fx, fy, cx, cy, hw, hh, hh);
            float cov     = coverage(d_track);
            if (cov <= 0.0f) { continue; }

            blend(sx, sy, track, cov, mode);

            /* Centre tick: a 1px column, only where no fill will cover it. */
            if (s_value == 0)
            {
                float d_tick = fabsf(fx - cx) - 0.5f;
                blend(sx, sy, tick, coverage(d_tick) * cov, mode);
            }

            if (s_value != 0)
            {
                float d_fill = rrect_sdf(fx, fy, fill_cx, cy, fill_hw, hh, hh);
                blend(sx, sy, fill, coverage(d_fill) * cov, mode);
            }

            /* Thumb: rim capsule, then the inner body inset by the rim. */
            float d_thumb = rrect_sdf(fx, fy, thumbx, cy,
                                      THUMB_W / 2.0f, THUMB_H / 2.0f, THUMB_W / 2.0f);
            blend(sx, sy, rim,   coverage(d_thumb) * cov, mode);
            blend(sx, sy, thumb, coverage(d_thumb + RIM_PX) * cov, mode);
        }
    }
}

/* Damage only the column band a move from `from` to `to` can change: the thumb capsule
 * at either value, widened by the fill capsule's end cap (half the widget height, which
 * is how far the fill's rounded end reaches past the thumb centre) plus a pixel of AA
 * margin. Full height, since every shape spans the track. Both the fill's far edge and
 * the tick at centre fall inside this band, and paint scans only the damaged part — so
 * a drag step costs a fraction of the 273x64 rect. */
static void damage_between(int32_t from, int32_t to)
{
    leRect rect;
    leRect d;

    s_track->fn->rectToScreen(s_track, &rect);

    float hw   = (float)rect.width  / 2.0f;
    float hh   = (float)rect.height / 2.0f;
    float cx   = (float)rect.x + hw;
    float span = hw - THUMB_W / 2.0f;

    if (span < 1.0f)
    {
        s_track->fn->invalidate(s_track);
        return;
    }

    float xa = cx + span * (float)from / 100.0f;
    float xb = cx + span * (float)to   / 100.0f;
    float lo = fminf(xa, xb) - hh - 1.0f;
    float hi = fmaxf(xa, xb) + hh + 1.0f;

    d.x      = (int32_t)lo - 1;                 /* truncation is toward zero; -1 floors */
    d.y      = rect.y;
    d.width  = ((int32_t)hi + 1) - d.x + 1;
    d.height = rect.height;

    leRectClip(&d, &rect, &d);

    s_track->fn->_damageArea(s_track, &d);
}

static void value_set(int32_t v)
{
    int32_t prev = s_value;

    if (v < -100) { v = -100; }
    if (v >  100) { v =  100; }
    if (v == s_value) { return; }

    s_value = v;

    if (s_track != NULL) { damage_between(prev, v); }
    if (s_on_change != NULL) { s_on_change(s_value); }
}

/* Map a screen x to a value, with the same inset the thumb travel uses so dragging
 * to either end reaches the full +/-100. */
static void value_from_x(int32_t screen_x)
{
    leRect rect;
    s_track->fn->rectToScreen(s_track, &rect);

    float hw   = (float)rect.width / 2.0f;
    float span = hw - THUMB_W / 2.0f;
    if (span < 1.0f) { return; }

    float rel = ((float)screen_x + 0.5f) - ((float)rect.x + hw);
    value_set((int32_t)(clampf(rel / span, -1.0f, 1.0f) * 100.0f));
}

static void whammy_touchDown(leWidget *wgt, leWidgetEvent_TouchDown *evt)
{
    value_from_x(evt->x);
    leWidgetEvent_Accept(&evt->event, wgt);
}

static void whammy_touchMove(leWidget *wgt, leWidgetEvent_TouchMove *evt)
{
    value_from_x(evt->x);
    leWidgetEvent_Accept(&evt->event, wgt);
}

static void whammy_touchUp(leWidget *wgt, leWidgetEvent_TouchUp *evt)
{
    value_set(0);                       /* spring back to rest */
    leWidgetEvent_Accept(&evt->event, wgt);
}

void Whammy_Enable(leWidget *track, WhammyChangeFn onChange)
{
    if (track == NULL) { return; }

    s_track     = track;
    s_on_change = onChange;

    if (!s_vt_ready)
    {
        s_vt = *track->fn;
        s_orig_paint = track->fn->_paint;
        s_vt._paint         = whammy_paint;
        s_vt.touchDownEvent = whammy_touchDown;
        s_vt.touchMoveEvent = whammy_touchMove;
        s_vt.touchUpEvent   = whammy_touchUp;
        s_vt_ready = LE_TRUE;
    }
    track->fn = &s_vt;
}

int32_t Whammy_Value(void)
{
    return s_value;
}

void Whammy_Set(int32_t value)
{
    value_set(value);
}
