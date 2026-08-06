#include "ui/widgets/tilt/widget_tilt.h"

#include <math.h>

#include "ui/gfx/vec_draw.h"

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
#define INSET      20
#define MARGIN     10
#define STROKE     38
#define THUMB_R    12.0f
#define RIM_HALF    1.25f      /* SVG stroke-width 2.5 straddles the boundary */

/* Outer/inner thumb radii, Q16.16. The parentheses around the argument are load
 * bearing: LE_REAL_I16_FROM_FLOAT does not add its own. */
#define THUMB_RIM_R    LE_REAL_I16_FROM_FLOAT((THUMB_R + RIM_HALF))
#define THUMB_BODY_R   LE_REAL_I16_FROM_FLOAT((THUMB_R - RIM_HALF))

#define DEG2RAD   0.01745329f

/* The swept quadrant runs up and to the left, which is 90°..180° in the vector API's
 * frame (0° to the right, counter-clockwise), so a tilt of d degrees off the left ray
 * sits at 180 - d. */
#define TILT_DEG16(d)   UI_VEC_DEG16(180 - (d))

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Arc centre and radius in screen space. False if the widget is too small to draw. */
static leBool arc_geom(const leRect *rect, leVector2 *centre, leReal_i16 *radius)
{
    int32_t r;

    if (rect->width < 40 || rect->height < 40) { return LE_FALSE; }

    r = ((rect->width < rect->height) ? rect->width : rect->height)
        - INSET - MARGIN - STROKE / 2;

    if (r < STROKE) { return LE_FALSE; }

    *radius   = LE_REAL_I16_FROM_INT(r);
    centre->x = LE_REAL_I16_FROM_INT(rect->x + rect->width  - INSET);
    centre->y = LE_REAL_I16_FROM_INT(rect->y + rect->height - INSET);

    return LE_TRUE;
}

static void tilt_paint(leWidget *wgt)
{
    s_orig_paint(wgt);

    if (wgt->status.drawState != LE_WIDGET_DRAW_STATE_DONE) { return; }

    leRect      rect;
    leVector2   centre;
    leReal_i16  radius;
    leColorMode mode = leRenderer_CurrentColorMode();

    wgt->fn->rectToScreen(wgt, &rect);

    if (!arc_geom(&rect, &centre, &radius)) { return; }

    leVectorArc_StrokeAttr arc =
    {
        .color    = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_TRACK),
        .alpha    = 255u,
        .width    = LE_REAL_I16_FROM_INT(STROKE),
        .hardness = LE_REAL_I16_ONE,
        .mask     = LE_STROKEMASK_ALL,
        .aaMode   = UI_VEC_AA,
        .capStyle = LE_CAPSTYLE_ROUND,
    };

    /* Both arcs start at their high-tilt end and sweep back to tilt 0 (the left ray),
     * since the vector API's spans run counter-clockwise. */
    leDraw_VectorArcStroke(&centre, radius, TILT_DEG16(90), UI_VEC_DEG16(90), &arc);

    if (s_degrees > 0)
    {
        arc.color = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_FILL);
        leDraw_VectorArcStroke(&centre, radius, TILT_DEG16(s_degrees),
                               UI_VEC_DEG16(s_degrees), &arc);
    }

    /* Thumb: white rim disc, then the body inset by half the rim width. */
    leVector2 thumb;
    UiVec_ArcPoint(&centre, radius, TILT_DEG16(s_degrees), &thumb);

    leVectorArc_FillAttr disc =
    {
        .color    = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_RIM),
        .alpha    = 255u,
        .hardness = LE_REAL_I16_ONE,
        .aaMode   = UI_VEC_AA,
    };

    leDraw_VectorArcFill(&thumb, THUMB_RIM_R, 0, UI_VEC_FULL_CIRCLE, &disc);

    disc.color = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_THUMB);
    leDraw_VectorArcFill(&thumb, THUMB_BODY_R, 0, UI_VEC_FULL_CIRCLE, &disc);
}

/* Damage only what a move from `from` to `to` degrees can change: the round-cap box at
 * each of the two angles. That box also holds the thumb (a smaller disc on the same
 * centre), and since cos and sin are monotonic across the swept quadrant the two boxes
 * together bound the sector between the angles — so nothing else on the arc moves. One
 * pixel of margin for the anti-aliased fringe.
 *
 * Worth the arithmetic because the vector rasterizer clips its scan to the damage rect:
 * a one-degree drag step scans ~1/9 of the widget and lands a result identical to a
 * full repaint. A full invalidate would repaint all 157x157 for a 2 px thumb move. */
static void damage_between(int32_t from, int32_t to)
{
    leRect     rect;
    leVector2  centre;
    leVector2  a;
    leVector2  b;
    leReal_i16 radius;
    int32_t    half = STROKE / 2 + 1;
    int32_t    ax, ay, bx, by;
    leRect     d;

    s_arc->fn->rectToScreen(s_arc, &rect);

    if (!arc_geom(&rect, &centre, &radius))
    {
        s_arc->fn->invalidate(s_arc);
        return;
    }

    UiVec_ArcPoint(&centre, radius, TILT_DEG16(from), &a);
    UiVec_ArcPoint(&centre, radius, TILT_DEG16(to),   &b);

    ax = leReal_i16_ToInt(a.x);
    ay = leReal_i16_ToInt(a.y);
    bx = leReal_i16_ToInt(b.x);
    by = leReal_i16_ToInt(b.y);

    d.x      = ((ax < bx) ? ax : bx) - half;
    d.y      = ((ay < by) ? ay : by) - half;
    d.width  = (((ax > bx) ? ax : bx) + half) - d.x + 1;
    d.height = (((ay > by) ? ay : by) + half) - d.y + 1;

    leRectClip(&d, &rect, &d);

    s_arc->fn->_damageArea(s_arc, &d);
}

static void value_set(int32_t deg)
{
    int32_t prev = s_degrees;

    if (deg <  0) { deg =  0; }
    if (deg > 90) { deg = 90; }
    if (deg == s_degrees) { return; }

    s_degrees = deg;

    if (s_arc != NULL) { damage_between(prev, deg); }
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
