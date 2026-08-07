#include "ui/widgets/tilt/widget_tilt.h"

#include "ui/gfx/aa_shape.h"

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
/* Thumb radii in HALF-PIXEL units: 26 and 21 are 13 px and 10.5 px, so the rim comes out at
 * the mockup's 2.5 px exactly. Odd and even radii about one shared centre is precisely what
 * a leRect cannot express, hence AaShape_Disc. */
#define THUMB_RIM_HP    26
#define THUMB_BODY_HP   21

/* The swept quadrant runs up and to the left, which is 90°..180° in the vector API's
 * frame (0° to the right, counter-clockwise), so a tilt of d degrees off the left ray
 * sits at 180 - d. */
#define TILT_DEG16(d)   UI_VEC_DEG16(180 - (d))

/* Arc centre and radius in HALF-PIXEL units. False if the widget is too small to draw. */
static leBool arc_geom_hp(const leRect *rect, int32_t *cx, int32_t *cy, int32_t *r)
{
    int32_t r_px;

    if (rect->width < 40 || rect->height < 40) { return LE_FALSE; }

    r_px = ((rect->width < rect->height) ? rect->width : rect->height)
           - INSET - MARGIN - STROKE / 2;

    if (r_px < STROKE) { return LE_FALSE; }

    *cx = 2 * (rect->x + rect->width  - INSET);
    *cy = 2 * (rect->y + rect->height - INSET);
    *r  = 2 * r_px;

    return LE_TRUE;
}

/* Point on the arc for a tilt of `deg`, in HALF-PIXEL units relative to the arc centre. The
 * quadrant sweeps up and to the left, so leftward is -x and upward is -y. */
static void arc_point(int32_t cx, int32_t cy, int32_t r_half, int32_t deg,
                      int32_t *x, int32_t *y)
{
    int32_t cos_d = AaShape_CosQ12(deg);
    int32_t sin_d = AaShape_SinQ12(deg);

    *x = cx - ((r_half * cos_d) >> 12);
    *y = cy - ((r_half * sin_d) >> 12);
}

static void tilt_paint(leWidget *wgt)
{
    s_orig_paint(wgt);

    if (wgt->status.drawState != LE_WIDGET_DRAW_STATE_DONE) { return; }

    leRect      rect;
    leColorMode mode = leRenderer_CurrentColorMode();

    wgt->fn->rectToScreen(wgt, &rect);

    if (rect.width < 40 || rect.height < 40) { return; }

    leColor track = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_TRACK);
    leColor fill  = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_FILL);
    leColor thumb = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_THUMB);
    leColor rim   = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_RIM);

    int32_t cx, cy, r;

    if (!arc_geom_hp(&rect, &cx, &cy, &r)) { return; }

    /* The quadrant sweeps up and to the left, 90..180 degrees; a tilt of d sits at 180 - d, so
     * the fill wedge is the top d degrees of that range. One annulus pass, coloured per pixel
     * by the wedge test — see ui/gfx/aa_shape.h. */
    AaShape_ArcRing(cx, cy, r, STROKE, 90, 180,
                    180 - s_degrees, (s_degrees > 0) ? 180 : 0,
                    track, fill, 255u);

    /* Round caps, reproducing the stroke's LE_CAPSTYLE_ROUND: a disc of the stroke's
     * half-width at each end. The left end belongs to the fill once there is any tilt, and
     * the top end once the tilt reaches the full quadrant. */
    int32_t ex, ey;

    arc_point(cx, cy, r, 90, &ex, &ey);
    AaShape_Disc(ex, ey, STROKE, (s_degrees >= 90) ? fill : track, 255u);

    arc_point(cx, cy, r, 0, &ex, &ey);
    AaShape_Disc(ex, ey, STROKE, (s_degrees > 0) ? fill : track, 255u);

    if (s_degrees > 0 && s_degrees < 90)
    {
        arc_point(cx, cy, r, s_degrees, &ex, &ey);
        AaShape_Disc(ex, ey, STROKE, fill, 255u);
    }

    /* Thumb: rim disc then the body inside it. Radii 13 and 10.5 px give the mockup's 2.5px
     * rim exactly, which is why these are half-pixel and not a leRect. */
    arc_point(cx, cy, r, s_degrees, &ex, &ey);
    AaShape_Disc(ex, ey, THUMB_RIM_HP,  rim,   255u);
    AaShape_Disc(ex, ey, THUMB_BODY_HP, thumb, 255u);
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
    leRect  rect;
    leRect  d;
    int32_t cx, cy, r;
    int32_t ax, ay, bx, by;
    int32_t half = STROKE / 2 + 1;

    s_arc->fn->rectToScreen(s_arc, &rect);

    if (!arc_geom_hp(&rect, &cx, &cy, &r))
    {
        s_arc->fn->invalidate(s_arc);
        return;
    }

    arc_point(cx, cy, r, from, &ax, &ay);
    arc_point(cx, cy, r, to,   &bx, &by);

    /* Half-pixel to pixel, rounding outward. */
    ax >>= 1; ay >>= 1; bx >>= 1; by >>= 1;

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

/* Aim the thumb at the touch point: the angle of (touch - centre) within the quadrant,
 * ignoring distance so a touch anywhere in the widget works.
 *
 * No atan2: the paint's own test — a point is at or below the ray at tilt d iff
 * cos(d)·v <= sin(d)·u — is monotonic in d, so a binary search over 0..90 finds the angle in
 * seven integer comparisons against the sine table. */
static void value_from_point(int32_t screen_x, int32_t screen_y)
{
    leRect  rect;
    int32_t cx, cy, r;
    int32_t u, v, lo, hi;

    s_arc->fn->rectToScreen(s_arc, &rect);

    if (!arc_geom_hp(&rect, &cx, &cy, &r)) { return; }

    u = cx - ((2 * screen_x) + 1);      /* leftward, half-pixels */
    v = cy - ((2 * screen_y) + 1);      /* upward                */

    if (u <= 0 && v <= 0) { return; }
    if (u < 0) { u = 0; }
    if (v < 0) { v = 0; }

    lo = 0;
    hi = 90;

    while (lo < hi)
    {
        int32_t mid = (lo + hi) / 2;

        if ((AaShape_CosQ12(mid) * v) <= (AaShape_SinQ12(mid) * u))
        {
            hi = mid;
        }
        else
        {
            lo = mid + 1;
        }
    }

    value_set(lo);
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
