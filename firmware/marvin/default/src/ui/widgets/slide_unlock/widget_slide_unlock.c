#include "ui/widgets/slide_unlock/widget_slide_unlock.h"

#include "ui/gfx/aa_shape.h"

#include "gfx/legato/common/legato_color.h"
#include "gfx/legato/renderer/legato_renderer.h"
#include "gfx/legato/string/legato_stringutils.h"

/* Shared vtable copy + captured original paint (the widget_whammy pattern). */
static leWidgetVTable s_vt;
static void (*s_orig_paint)(leWidget *);
static leBool s_vt_ready = LE_FALSE;

static leWidget      *s_track;
static leString      *s_caption;
static leString      *s_chevron;
static SlideUnlockFn  s_on_unlock;

/* Thumb travel in pixels from the locked position, and the latch. */
static int32_t s_pos;
static bool    s_unlocked;

/* A drag in progress, and where in the thumb it was grabbed (finger x minus thumb x), so the
 * thumb stays under the finger instead of jumping its centre there. */
static bool    s_dragging;
static int32_t s_grab;

/* Mockup colours (Tailwind): track zinc-950 in a zinc-700 rim, fill violet at low alpha,
 * thumb a violet-600 -> violet-400 diagonal, caption zinc-600. */
#define C_TRACK     0x18181Bu
#define C_BORDER    0x3F3F46u
#define C_FILL      0x8B5CF6u
#define C_THUMB_LO  0x7C3AEDu
#define C_THUMB_HI  0xA78BFAu
#define C_CAPTION   0x52525Bu
#define C_CHEVRON   0xFFFFFFu

/* The mockup's fill runs #7c3aed22 -> #a78bfa44; one value between the two, since at RGB565
 * over a dimmed card the two ends differ by a couple of LSBs. */
#define FILL_ALPHA   51u

#define THUMB_INSET   4      /* mockup's top-1 bottom-1 on a 64px track */
#define UNLOCK_PCT   97      /* mockup's 0.97 of travel */
#define GRAB_SLOP     8      /* forgiveness either side of the thumb when grabbing */

/* Coverage blend, as in widget_whammy: a saturated pixel is an opaque write with no
 * read-back, which is most of every shape here. */
static void blend(int32_t x, int32_t y, leColor c, uint32_t cov, leColorMode mode)
{
    if (cov == 0u) { return; }
    if (cov > AA_COV_ONE) { cov = AA_COV_ONE; }

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

/* Thumb size and travel, both derived from the rect so the widget can be sized freely:
 * the thumb is a square inset from the track on every side, and travel is what is left. */
static int32_t thumb_size(const leRect *rect)
{
    return rect->height - (2 * THUMB_INSET);
}

static int32_t travel_of(const leRect *rect)
{
    return rect->width - (2 * THUMB_INSET) - thumb_size(rect);
}

/* Draw a string centred in `box`, the way leLabelWidget does: position the KERNING rect,
 * then draw from its top-left. Two independent divisions, matching Legato's own arrange. */
static void draw_centered(leString *str, const leRect *box, leColor clr, uint32_t alpha)
{
    leRect kr;

    if (str == NULL || alpha == 0u) { return; }
    if (str->fn->getRect(str, &kr) != LE_SUCCESS) { return; }

    (void)leStringUtils_KerningRect((const leRasterFont *)str->fn->getFont(str), &kr);

    (void)str->fn->_draw(str,
                         box->x + (box->width / 2) - (kr.width / 2),
                         box->y + (box->height / 2) - (kr.height / 2),
                         LE_HALIGN_CENTER, clr, alpha);
}

/* The thumb's 135-degree gradient, per pixel: `t` is the position along the diagonal, so a
 * single coverage query and a lerp give the mockup's linear-gradient(135deg, ...). */
static void paint_thumb(const leRect *rect, int32_t size, leColorMode mode)
{
    leColor  lo = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_THUMB_LO);
    leColor  hi = leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_THUMB_HI);
    int32_t  x0 = rect->x + THUMB_INSET + s_pos;
    int32_t  y0 = rect->y + THUMB_INSET;
    int32_t  span = (2 * size) - 2;
    AaCapsule th;
    leRect    box = { (int16_t)x0, (int16_t)y0, (int16_t)size, (int16_t)size };
    leRect    scan;
    int32_t   sx, sy;

    if (span < 1) { return; }

    AaShape_CapsuleSet(&th, (2 * x0) + size, (2 * y0) + size, size, size);

    leRenderer_GetClipRect(&scan);
    leRectClip(&box, &scan, &scan);
    if (scan.width <= 0 || scan.height <= 0) { return; }

    for (sy = scan.y; sy < scan.y + scan.height; sy++)
    {
        int32_t y = (2 * sy) + 1;

        for (sx = scan.x; sx < scan.x + scan.width; sx++)
        {
            uint32_t cov = AaShape_CapsuleCov(&th, (2 * sx) + 1, y);
            int32_t  d;

            if (cov == 0u) { continue; }

            d = (sx - x0) + (sy - y0);
            if (d < 0)    { d = 0; }
            if (d > span) { d = span; }

            blend(sx, sy, leColorLerp(lo, hi, (uint32_t)((d * 100) / span), mode), cov, mode);
        }
    }

    /* Chevron last, so it sits on the finished thumb. */
    draw_centered(s_chevron, &box,
                  leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_CHEVRON), 255u);
}

static void slide_paint(leWidget *wgt)
{
    s_orig_paint(wgt);

    if (wgt->status.drawState != LE_WIDGET_DRAW_STATE_DONE) { return; }

    leRect rect;
    wgt->fn->rectToScreen(wgt, &rect);

    int32_t size   = thumb_size(&rect);
    int32_t travel = travel_of(&rect);

    if (size < 4 || travel < 1) { return; }

    leColorMode mode   = leRenderer_CurrentColorMode();
    int32_t     radius = rect.height / 2;

    /* Track: opaque capsule inside a 1px rim. */
    AaShape_RRectFramed(&rect, radius, 1,
                        leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_TRACK), 255u,
                        leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_BORDER));

    /* Fill: the left cap out to the thumb's leading edge, so at rest it is the disc under the
     * thumb and it never runs ahead of it. Inside the rim, which the mockup's border box also
     * puts it — and which keeps the rim at full strength instead of tinting it violet. */
    leRect fill = { (int16_t)(rect.x + 1), (int16_t)(rect.y + 1),
                    (int16_t)(s_pos + size + (2 * THUMB_INSET) - 2),
                    (int16_t)(rect.height - 2) };

    if (fill.width > rect.width - 2) { fill.width = rect.width - 2; }

    AaShape_RRect(&fill, radius - 1, leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_FILL),
                  FILL_ALPHA);

    /* Caption, gone by half travel (the mockup's `opacity: 1 - pct * 2`), then the thumb
     * over it. */
    if ((2 * s_pos) < travel)
    {
        uint32_t alpha = (uint32_t)(((travel - (2 * s_pos)) * 255) / travel);

        draw_centered(s_caption, &rect,
                      leColorConvert(LE_COLOR_MODE_RGB_888, mode, C_CAPTION), alpha);
    }

    paint_thumb(&rect, size, mode);
}

/* The whole track, not the band the thumb moved through: the fill's leading edge, the
 * caption's alpha and the thumb all change together on every step, and their union is most
 * of a 420x64 rect anyway. One invalidate against widget_whammy's damage_between, which
 * exists because that control is dragged continuously during play — this one is dragged
 * once per visit to the screen. */
static void pos_set(int32_t pos)
{
    leRect  rect;
    int32_t travel;

    if (s_track == NULL) { return; }

    s_track->fn->rectToScreen(s_track, &rect);
    travel = travel_of(&rect);

    if (travel < 1) { return; }
    if (pos < 0)      { pos = 0; }
    if (pos > travel) { pos = travel; }
    if (pos == s_pos) { return; }

    s_pos = pos;
    s_track->fn->invalidate(s_track);

    if (!s_unlocked && s_pos >= ((travel * UNLOCK_PCT) / 100))
    {
        s_unlocked = true;
        if (s_on_unlock != NULL) { s_on_unlock(); }
    }
}

/* A press only starts a drag if it lands on the thumb (plus a little slop). Pressing anywhere
 * else on the track does nothing at all — which is what makes this a slide rather than a tap:
 * setting the position straight from the press point would let a single touch at the far end
 * of the track open the gate. */
static void slide_touchDown(leWidget *wgt, leWidgetEvent_TouchDown *evt)
{
    leRect  rect;
    int32_t thumb_x;
    int32_t size;

    s_track->fn->rectToScreen(s_track, &rect);

    size    = thumb_size(&rect);
    thumb_x = rect.x + THUMB_INSET + s_pos;

    if (evt->x >= (thumb_x - GRAB_SLOP) && evt->x < (thumb_x + size + GRAB_SLOP))
    {
        s_dragging = true;
        s_grab     = evt->x - thumb_x;
    }

    leWidgetEvent_Accept(&evt->event, wgt);
}

/* The thumb keeps the point it was grabbed by, so it neither jumps to the finger nor lags it. */
static void slide_touchMove(leWidget *wgt, leWidgetEvent_TouchMove *evt)
{
    leRect rect;

    if (s_dragging)
    {
        s_track->fn->rectToScreen(s_track, &rect);
        pos_set(evt->x - s_grab - rect.x - THUMB_INSET);
    }

    leWidgetEvent_Accept(&evt->event, wgt);
}

/* Spring back unless the gate opened; once it has, the widget is about to be hidden and
 * the thumb should stay where the finger left it. */
static void slide_touchUp(leWidget *wgt, leWidgetEvent_TouchUp *evt)
{
    s_dragging = false;

    if (!s_unlocked) { pos_set(0); }
    leWidgetEvent_Accept(&evt->event, wgt);
}

void SlideUnlock_Enable(leWidget *track, leString *caption, leString *chevron,
                        SlideUnlockFn onUnlock)
{
    if (track == NULL) { return; }

    s_track     = track;
    s_caption   = caption;
    s_chevron   = chevron;
    s_on_unlock = onUnlock;

    if (!s_vt_ready)
    {
        s_vt = *track->fn;
        s_orig_paint = track->fn->_paint;
        s_vt._paint         = slide_paint;
        s_vt.touchDownEvent = slide_touchDown;
        s_vt.touchMoveEvent = slide_touchMove;
        s_vt.touchUpEvent   = slide_touchUp;
        s_vt_ready = LE_TRUE;
    }
    track->fn = &s_vt;
}

void SlideUnlock_Reset(void)
{
    s_unlocked = false;
    s_dragging = false;

    if (s_pos != 0)
    {
        s_pos = 0;
        if (s_track != NULL) { s_track->fn->invalidate(s_track); }
    }
}

bool SlideUnlock_IsUnlocked(void)
{
    return s_unlocked;
}
