#include "ui/widgets/fret/widget_fret.h"

#include <math.h>

#include "gfx/legato/common/legato_color.h"
#include "gfx/legato/renderer/legato_renderer.h"

/* Shared vtable copy + captured original paint (the widget_gauge pattern). */
static leWidgetVTable s_vt;
static void (*s_orig_paint)(leWidget *);
static leBool s_vt_ready = LE_FALSE;

typedef struct
{
    leWidget *pad;
    unsigned  index;
    uint32_t  idle;
    uint32_t  held_c;
    uint32_t  ring;
    bool      held;
} fret_t;

static fret_t      s_frets[FRET_MAX];
static unsigned    s_count;
static FretChangeFn s_on_change;

#define CORNER_R    8.0f    /* Tailwind rounded-lg */
#define RING_PX     2.0f
#define IDLE_ALPHA  0.75f   /* mockup: opacity-75 at rest */

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float rrect_sdf(float px, float py, float cx, float cy,
                       float hw, float hh, float r)
{
    float qx = fabsf(px - cx) - (hw - r);
    float qy = fabsf(py - cy) - (hh - r);
    float ox = qx > 0.0f ? qx : 0.0f;
    float oy = qy > 0.0f ? qy : 0.0f;
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f) - r;
}

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

static fret_t *find(const leWidget *pad)
{
    unsigned i;

    for (i = 0u; i < s_count; i++)
    {
        if (s_frets[i].pad == pad) { return &s_frets[i]; }
    }
    return NULL;
}

static void fret_paint(leWidget *wgt)
{
    s_orig_paint(wgt);

    if (wgt->status.drawState != LE_WIDGET_DRAW_STATE_DONE) { return; }

    fret_t *f = find(wgt);
    if (f == NULL) { return; }

    leRect rect;
    wgt->fn->rectToScreen(wgt, &rect);
    if (rect.width < 8 || rect.height < 8) { return; }

    leColorMode mode = leRenderer_CurrentColorMode();
    leColor body = leColorConvert(LE_COLOR_MODE_RGB_888, mode,
                                 f->held ? f->held_c : f->idle);
    leColor ring = leColorConvert(LE_COLOR_MODE_RGB_888, mode, f->ring);

    float alpha = f->held ? 1.0f : IDLE_ALPHA;
    float hw = (float)rect.width  / 2.0f;
    float hh = (float)rect.height / 2.0f;
    float cx = (float)rect.x + hw;
    float cy = (float)rect.y + hh;

    for (int32_t py = 0; py < rect.height; py++)
    {
        for (int32_t px = 0; px < rect.width; px++)
        {
            int32_t sx = rect.x + px;
            int32_t sy = rect.y + py;
            float   fx = (float)sx + 0.5f;
            float   fy = (float)sy + 0.5f;

            float d   = rrect_sdf(fx, fy, cx, cy, hw, hh, CORNER_R);
            float cov = coverage(d);
            if (cov <= 0.0f) { continue; }

            if (f->held)
            {
                /* Ring first, then the body inset by the ring width. Tailwind draws
                 * ring-2 outside the element; a widget cannot paint past its own
                 * rect, so it is inset here instead. */
                blend(sx, sy, ring, cov, mode);
                blend(sx, sy, body, coverage(d + RING_PX), mode);
            }
            else
            {
                blend(sx, sy, body, cov * alpha, mode);
            }
        }
    }
}

static void fret_set(fret_t *f, bool held, bool notify)
{
    if (f == NULL || f->held == held) { return; }

    f->held = held;
    f->pad->fn->invalidate(f->pad);

    if (notify && s_on_change != NULL) { s_on_change(f->index, held); }
}

static void fret_touchDown(leWidget *wgt, leWidgetEvent_TouchDown *evt)
{
    fret_set(find(wgt), true, true);
    leWidgetEvent_Accept(&evt->event, wgt);
}

static void fret_touchUp(leWidget *wgt, leWidgetEvent_TouchUp *evt)
{
    fret_set(find(wgt), false, true);
    leWidgetEvent_Accept(&evt->event, wgt);
}

void Fret_Enable(leWidget *pad, unsigned index, uint32_t idle, uint32_t held,
                 uint32_t ring, FretChangeFn onChange)
{
    if (pad == NULL || s_count >= FRET_MAX) { return; }

    fret_t *f = &s_frets[s_count++];
    f->pad    = pad;
    f->index  = index;
    f->idle   = idle;
    f->held_c = held;
    f->ring   = ring;
    f->held   = false;

    s_on_change = onChange;

    if (!s_vt_ready)
    {
        s_vt = *pad->fn;
        s_orig_paint = pad->fn->_paint;
        s_vt._paint         = fret_paint;
        s_vt.touchDownEvent = fret_touchDown;
        s_vt.touchUpEvent   = fret_touchUp;
        s_vt_ready = LE_TRUE;
    }
    pad->fn = &s_vt;
}

void Fret_SetHeld(leWidget *pad, bool held)
{
    fret_set(find(pad), held, false);
}

bool Fret_IsHeld(const leWidget *pad)
{
    const fret_t *f = find(pad);

    return (f != NULL) && f->held;
}
