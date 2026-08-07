#include "ui/widgets/whammy/widget_whammy.h"

#include "ui/gfx/aa_shape.h"

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

#define THUMB_W_PX   16
#define THUMB_H_PX   40
#define RIM_PX        2

/* A half-extent expressed in HALF-pixel units is numerically the size in pixels (half the
 * size, doubled), which is why these serve as both. */
#define THUMB_W   THUMB_W_PX
#define THUMB_H   THUMB_H_PX
#define RIM_HP    (2 * RIM_PX)

/* Coverage of a shape's boundary, and the blend, both integer. The float versions of these
 * cost this widget 116 ms for a full repaint — four soft-float signed-distance evaluations
 * plus three blends per pixel over 17,472 pixels, ~5300 cycles each, on a core with no FPU.
 * The capsule maths now lives in ui/gfx/aa_shape.h in half-pixel integer units; see the
 * journal, 2026-08-07 (night). */
static void blend(int32_t x, int32_t y, leColor c, uint32_t cov, leColorMode mode)
{
    if (cov == 0u) { return; }
    if (cov > AA_COV_ONE) { cov = AA_COV_ONE; }

    /* Saturated coverage is an opaque write: no read-back, no interpolation. Interior pixels
     * are the majority of every shape here. */
    if (cov == AA_COV_ONE)
    {
        leRenderer_PutPixel(x, y, c);
        return;
    }

    leColor bg = leRenderer_GetPixel(x, y);

    leRenderer_PutPixel(x, y,
                        leColorLerp(bg, c,
                                    ((cov * 100u) + (AA_COV_ONE / 2u)) / AA_COV_ONE, mode));
}

/* a·b over 0..AA_COV_ONE — clipping one shape to another. */
static uint32_t cov_mul(uint32_t a, uint32_t b)
{
    return (a * b) / AA_COV_ONE;
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

    /* Half-pixel units throughout: a half-extent is the rect's own width/height, a centre is
     * 2·origin + extent, and a pixel centre is 2·p + 1. */
    int32_t hw = rect.width;
    int32_t hh = rect.height;
    int32_t cx = (2 * rect.x) + hw;
    int32_t cy = (2 * rect.y) + hh;

    /* Thumb centre travels the track minus half a thumb at each end, so the thumb stays
     * inside the capsule at the extremes. */
    int32_t span = hw - THUMB_W;
    int32_t num  = span * s_value;
    /* Rounded, not truncated: truncation toward zero cost up to a half pixel of thumb
     * position, which is visible as the rim edge landing a pixel out. Rounding puts it on
     * the nearest half pixel — finer than the 1.28 half-pixels the thumb moves per value
     * step, so the quantisation is below the control's own resolution. */
    int32_t thumbx = cx + ((num >= 0) ? ((num + 50) / 100) : ((num - 50) / 100));

    /* Fill capsule spans centre -> thumb centre, padded to an end cap; degenerate at rest. */
    int32_t fa      = (cx < thumbx) ? cx : thumbx;
    int32_t fb      = (cx > thumbx) ? cx : thumbx;
    int32_t fill_hw = ((fb - fa) / 2) + hh;
    int32_t fill_cx = (fa + fb) / 2;

    AaCapsule c_track, c_fill, c_rim, c_body;

    AaShape_CapsuleSet(&c_track, cx, cy, hw, hh);
    AaShape_CapsuleSet(&c_fill,  fill_cx, cy, fill_hw, hh);
    AaShape_CapsuleSet(&c_rim,   thumbx, cy, THUMB_W, THUMB_H);
    AaShape_CapsuleSet(&c_body,  thumbx, cy, THUMB_W - RIM_HP, THUMB_H - RIM_HP);

    for (int32_t sy = scan.y; sy < scan.y + scan.height; sy++)
    {
        int32_t y = (2 * sy) + 1;

        for (int32_t sx = scan.x; sx < scan.x + scan.width; sx++)
        {
            int32_t  x = (2 * sx) + 1;
            uint32_t cov;

            /* Track capsule; everything else is clipped to it. */
            cov = AaShape_CapsuleCov(&c_track, x, y);
            if (cov == 0u) { continue; }

            blend(sx, sy, track, cov, mode);

            /* Centre tick: a 1px column, only where no fill will cover it. */
            if (s_value == 0)
            {
                blend(sx, sy, tick, cov_mul(AaShape_ColumnCov(cx, x), cov), mode);
            }
            else
            {
                blend(sx, sy, fill, cov_mul(AaShape_CapsuleCov(&c_fill, x, y), cov), mode);
            }

            /* Thumb: rim capsule, then the body inset by the rim on every side. */
            blend(sx, sy, rim,   cov_mul(AaShape_CapsuleCov(&c_rim,  x, y), cov), mode);
            blend(sx, sy, thumb, cov_mul(AaShape_CapsuleCov(&c_body, x, y), cov), mode);
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

    int32_t hw   = rect.width / 2;
    int32_t hh   = rect.height / 2;
    int32_t cx   = rect.x + hw;
    int32_t span = hw - (THUMB_W_PX / 2);

    if (span < 1)
    {
        s_track->fn->invalidate(s_track);
        return;
    }

    int32_t xa = cx + ((span * from) / 100);
    int32_t xb = cx + ((span * to)   / 100);
    int32_t lo = ((xa < xb) ? xa : xb) - hh - 1;
    int32_t hi = ((xa > xb) ? xa : xb) + hh + 1;

    /* One more pixel each way than the geometry needs, covering the integer division above
     * and the antialiased edge. */
    d.x      = lo - 1;
    d.y      = rect.y;
    d.width  = (hi + 1) - d.x + 1;
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

    int32_t hw   = rect.width / 2;
    int32_t span = hw - (THUMB_W_PX / 2);
    int32_t rel, v;

    if (span < 1) { return; }

    rel = screen_x - (rect.x + hw);
    v   = (rel * 100) / span;

    if (v < -100) { v = -100; }
    if (v >  100) { v =  100; }

    value_set(v);
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
