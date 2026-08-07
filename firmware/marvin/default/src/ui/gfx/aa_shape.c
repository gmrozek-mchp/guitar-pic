#include "ui/gfx/aa_shape.h"

#include "gfx/legato/renderer/legato_renderer.h"

/* Integer square root, bit-by-bit — the same one aa_corners.c uses, and for the same reason:
 * this core has no FPU, so a soft-float `sqrtf` is a libgcc call. */
uint32_t AaShape_Isqrt(uint32_t v)
{
    uint32_t res = 0u;
    uint32_t bit = 1u << 30;

    while (bit > v) { bit >>= 2; }

    while (bit != 0u)
    {
        if (v >= (res + bit))
        {
            v  -= res + bit;
            res = (res >> 1) + bit;
        }
        else
        {
            res >>= 1;
        }
        bit >>= 2;
    }

    return res;
}

/* Coverage is carried as 0..AA_COV_ONE (see the header). 64 steps resolves an antialiased rim
 * finely enough to be indistinguishable in RGB565, and keeps `d2 << COV_SHIFT` inside uint32
 * for any capsule up to ~1000 px on its short axis. */
#define COV_SHIFT  12
#define COV_ONE    AA_COV_ONE

void AaShape_RRectSetCorners(AaRRect *s, int32_t cx, int32_t cy,
                             int32_t hw, int32_t hh, int32_t r, uint8_t corners)
{
    int32_t rmax;

    if (s == NULL) { return; }

    rmax = (hw < hh) ? hw : hh;
    if (r > rmax) { r = rmax; }
    if (r < 0)    { r = 0; }

    s->cx      = cx;
    s->cy      = cy;
    s->hw      = hw;
    s->hh      = hh;
    s->r       = r;
    s->ix      = hw - r;
    s->iy      = hh - r;
    s->r_in    = (r > 1) ? ((r - 1) * (r - 1)) : 0;
    s->r_out   = (r + 1) * (r + 1);
    s->corners = corners;
}

void AaShape_RRectSet(AaRRect *s, int32_t cx, int32_t cy, int32_t hw, int32_t hh, int32_t r)
{
    AaShape_RRectSetCorners(s, cx, cy, hw, hh, r, AA_CORNER_ALL);
}

void AaShape_CapsuleSet(AaCapsule *c, int32_t cx, int32_t cy, int32_t hw, int32_t hh)
{
    AaShape_RRectSet(c, cx, cy, hw, hh, (hw < hh) ? hw : hh);
}

uint32_t AaShape_RRectCov(const AaRRect *s, int32_t x, int32_t y)
{
    int32_t  sx = x - s->cx;
    int32_t  sy = y - s->cy;
    int32_t  dx = (sx < 0) ? -sx : sx;
    int32_t  dy = (sy < 0) ? -sy : sy;
    int32_t  qx, qy, d2;
    uint32_t sq, t;

    /* A square corner is just the straight edges meeting: clip to the box and stop. */
    if (s->corners != AA_CORNER_ALL)
    {
        uint8_t bit = (sy < 0) ? ((sx < 0) ? AA_CORNER_TL : AA_CORNER_TR)
                               : ((sx < 0) ? AA_CORNER_BL : AA_CORNER_BR);

        if ((s->corners & bit) == 0u)
        {
            int32_t ex = s->hw - dx;      /* half-pixels inside the vertical edge   */
            int32_t ey = s->hh - dy;      /* half-pixels inside the horizontal edge */
            int32_t e  = (ex < ey) ? ex : ey;
            int32_t c64;

            /* Same convention as the arcs: full half a pixel inside, empty half out. */
            c64 = 32 + (e * 32);
            if (c64 <= 0) { return 0u; }
            return (c64 >= (int32_t)COV_ONE) ? COV_ONE : (uint32_t)c64;
        }
    }

    /* Distance to the inner rectangle: zero on either axis the point is already within, so
     * the corners come out round and the straight edges come out straight, from one form. */
    qx = dx - s->ix;
    qy = dy - s->iy;

    /* Reject on one axis before squaring: past the radius on x alone already puts d2 beyond
     * r_out. Worth the two compares because a widget that composites several shapes queries
     * every one of them per pixel — the whammy's thumb covers 16x40 of a 273x64 track, so
     * this is the answer for the overwhelming majority of its pixels. */
    if (qx >= s->r + 1 || qy >= s->r + 1) { return 0u; }

    if (qx <= 0 && qy <= 0) { return COV_ONE; }   /* inside the straight part */
    if (qx < 0) { qx = 0; }
    if (qy < 0) { qy = 0; }

    d2 = (qx * qx) + (qy * qy);

    if (d2 >= s->r_out) { return 0u; }
    if (d2 <= s->r_in)  { return COV_ONE; }

    sq = AaShape_Isqrt(((uint32_t)d2) << COV_SHIFT);
    t  = (uint32_t)(s->r + 1) * COV_ONE;

    return (sq >= t) ? 0u : ((t - sq) / 2u);
}

/* Clip a shape's pixel bounding box to the rect actually being drawn, and say whether anything
 * survives. Load-bearing for partial invalidation: the blits below write through the culling
 * `_Safe` pixel calls, so without this they would still compute coverage for every pixel of the
 * shape and merely throw most of it away. A bus-screen TX bar is 46x204 but a value change
 * damages a band a few pixels tall. */
static leBool clip_to_draw(int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    leRect box = { .x = *x0, .y = *y0, .width = *x1 - *x0, .height = *y1 - *y0 };
    leRect clip;

    if (box.width < 1 || box.height < 1) { return LE_FALSE; }
    if (leRenderer_CullDrawRect(&box) == LE_TRUE) { return LE_FALSE; }

    leRenderer_ClipDrawRect(&box, &clip);

    if (clip.width < 1 || clip.height < 1) { return LE_FALSE; }

    *x0 = clip.x;
    *y0 = clip.y;
    *x1 = clip.x + clip.width;
    *y1 = clip.y + clip.height;

    return LE_TRUE;
}

/* sin(d) for d = 0..90 degrees, Q12. Everything else comes from symmetry. */
static const uint16_t SIN_Q12[91] =
{
        0,    71,   143,   214,   286,   357,   428,   499,   570,   641,
      711,   782,   852,   921,   991,  1060,  1129,  1198,  1266,  1334,
     1401,  1468,  1534,  1600,  1666,  1731,  1796,  1860,  1923,  1986,
     2048,  2110,  2171,  2231,  2290,  2349,  2408,  2465,  2522,  2578,
     2633,  2687,  2741,  2793,  2845,  2896,  2946,  2996,  3044,  3091,
     3138,  3183,  3228,  3271,  3314,  3355,  3396,  3435,  3474,  3511,
     3547,  3582,  3617,  3650,  3681,  3712,  3742,  3770,  3798,  3824,
     3849,  3873,  3896,  3917,  3937,  3956,  3974,  3991,  4006,  4021,
     4034,  4046,  4056,  4065,  4074,  4080,  4086,  4090,  4094,  4095,
     4096,
};

int32_t AaShape_SinQ12(int32_t deg)
{
    deg %= 360;
    if (deg < 0) { deg += 360; }

    if (deg <=  90) { return  (int32_t)SIN_Q12[deg]; }
    if (deg <= 180) { return  (int32_t)SIN_Q12[180 - deg]; }
    if (deg <= 270) { return -(int32_t)SIN_Q12[deg - 180]; }
    return                   -(int32_t)SIN_Q12[360 - deg];
}

int32_t AaShape_CosQ12(int32_t deg)
{
    return AaShape_SinQ12(deg + 90);
}

/* Is (X, Y) — maths frame, Y upward — inside the angular span [lo, hi]? Counter-clockwise of
 * the low ray and clockwise of the high one, each a cross product. Requires hi - lo <= 180. */
static bool in_span(int32_t X, int32_t Y,
                    int32_t cos_lo, int32_t sin_lo, int32_t cos_hi, int32_t sin_hi)
{
    return (((cos_lo * Y) - (sin_lo * X)) >= 0) &&
           (((cos_hi * Y) - (sin_hi * X)) <= 0);
}

void AaShape_ArcRing(int32_t cx, int32_t cy, int32_t r, int32_t hw,
                     int32_t a0, int32_t a1, int32_t f0, int32_t f1,
                     leColor track, leColor fill, uint32_t alpha)
{
    int32_t o_out = r + hw + 1;
    int32_t o_in  = r - hw - 1;
    int32_t o2_out, o2_in;
    int32_t cos_a0, sin_a0, cos_a1, sin_a1, cos_f0, sin_f0, cos_f1, sin_f1;
    bool    wedge = (f1 > f0);
    int32_t py;

    int32_t bx0 = (cx - o_out - 1) >> 1;
    int32_t bx1 = ((cx + o_out + 2) >> 1) + 1;
    int32_t by0 = (cy - o_out - 1) >> 1;
    int32_t by1 = ((cy + o_out + 2) >> 1) + 1;

    if (r < 1 || hw < 1 || alpha == 0u) { return; }

    if (clip_to_draw(&bx0, &by0, &bx1, &by1) == LE_FALSE) { return; }

    o2_out = o_out * o_out;
    o2_in  = (o_in > 0) ? (o_in * o_in) : 0;

    cos_a0 = AaShape_CosQ12(a0); sin_a0 = AaShape_SinQ12(a0);
    cos_a1 = AaShape_CosQ12(a1); sin_a1 = AaShape_SinQ12(a1);
    cos_f0 = AaShape_CosQ12(f0); sin_f0 = AaShape_SinQ12(f0);
    cos_f1 = AaShape_CosQ12(f1); sin_f1 = AaShape_SinQ12(f1);

    for (py = by0; py < by1; py++)
    {
        int32_t y  = (2 * py) + 1;
        int32_t vy = y - cy;
        int32_t v2 = vy * vy;
        int32_t u_lo, u_hi, side;

        if (v2 >= o2_out) { continue; }

        u_hi = (int32_t)AaShape_Isqrt((uint32_t)(o2_out - v2));
        u_lo = (v2 >= o2_in) ? 0 : (int32_t)AaShape_Isqrt((uint32_t)(o2_in - v2));

        /* Two x-intervals per row, left and right of centre; they merge when u_lo is 0. */
        for (side = 0; side < 2; side++)
        {
            int32_t px0, px1, px;

            if (side == 0)
            {
                px0 = ((cx - u_hi - 1) >> 1) - 1;
                /* When the row passes inside the inner radius the two arms merge into one
                 * span across the centre; emitting only the left arm silently drops the whole
                 * right-hand side of the ring. */
                px1 = (u_lo == 0) ? (((cx + u_hi + 1) >> 1) + 1)
                                  : (((cx - u_lo + 1) >> 1) + 1);
            }
            else
            {
                if (u_lo == 0) { break; }          /* merged above */
                px0 = ((cx + u_lo - 1) >> 1) - 1;
                px1 = ((cx + u_hi + 1) >> 1) + 1;
            }

            if (px0 < bx0) { px0 = bx0; }
            if (px1 > bx1 - 1) { px1 = bx1 - 1; }

            for (px = px0; px <= px1; px++)
            {
                int32_t  x  = (2 * px) + 1;
                int32_t  ux = x - cx;
                int32_t  d2 = (ux * ux) + v2;
                int32_t  dr, c64;
                uint32_t cov, a;
                leColor  c;

                if (d2 >= o2_out || d2 <= o2_in) { continue; }

                if (!in_span(ux, -vy, cos_a0, sin_a0, cos_a1, sin_a1)) { continue; }

                /* Band coverage: distance from the mid-radius against the half-width, in the
                 * project's usual convention (full half a pixel in, empty half a pixel out). */
                dr = (int32_t)AaShape_Isqrt(((uint32_t)d2) << 12) - (r << 6);
                if (dr < 0) { dr = -dr; }

                c64 = 32 + (((hw << 6) - dr) / 2);
                if (c64 <= 0) { continue; }
                cov = (c64 >= (int32_t)COV_ONE) ? COV_ONE : (uint32_t)c64;

                c = (wedge && in_span(ux, -vy, cos_f0, sin_f0, cos_f1, sin_f1)) ? fill : track;

                if (cov >= COV_ONE && alpha >= 255u)
                {
                    (void)leRenderer_PutPixel_Safe(px, py, c);
                    continue;
                }

                a = (cov * alpha) / COV_ONE;
                if (a != 0u) { (void)leRenderer_BlendPixel_Safe(px, py, c, a); }
            }
        }
    }
}

uint32_t AaShape_ColumnCov(int32_t cx, int32_t x)
{
    int32_t d = x - cx;

    if (d < 0) { d = -d; }

    /* Half-pixel distance d: full coverage on centre, none two half-pixels out. */
    return (d >= 2) ? 0u : (COV_ONE - ((uint32_t)d * (COV_ONE / 2u)));
}

/* The one rasteriser behind every public shape. */
static void rrect_blit(const AaRRect *sh, leColor color, uint32_t alpha)
{
    int32_t ex = sh->ix + sh->r;
    int32_t ey = sh->iy + sh->r;
    int32_t x0 = (sh->cx - ex - 1) >> 1;
    int32_t x1 = ((sh->cx + ex + 2) >> 1) + 1;
    int32_t y0 = (sh->cy - ey - 1) >> 1;
    int32_t y1 = ((sh->cy + ey + 2) >> 1) + 1;
    int32_t px, py;

    if (alpha == 0u || ex < 1 || ey < 1) { return; }

    if (clip_to_draw(&x0, &y0, &x1, &y1) == LE_FALSE) { return; }

    for (py = y0; py < y1; py++)
    {
        int32_t y = (2 * py) + 1;

        for (px = x0; px < x1; px++)
        {
            uint32_t cov = AaShape_RRectCov(sh, (2 * px) + 1, y);
            uint32_t a;

            if (cov == 0u) { continue; }

            if (cov >= COV_ONE && alpha >= 255u)
            {
                (void)leRenderer_PutPixel_Safe(px, py, color);
                continue;
            }

            a = (cov * alpha) / COV_ONE;
            if (a != 0u)
            {
                (void)leRenderer_BlendPixel_Safe(px, py, color, a);
            }
        }
    }
}

void AaShape_Capsule(const leRect *rect, leColor color, uint32_t alpha)
{
    AaCapsule c;

    if (rect == NULL || rect->width < 1 || rect->height < 1) { return; }

    AaShape_CapsuleSet(&c, (2 * rect->x) + rect->width, (2 * rect->y) + rect->height,
                       rect->width, rect->height);
    rrect_blit(&c, color, alpha);
}

void AaShape_Disc(int32_t cx, int32_t cy, int32_t r, leColor color, uint32_t alpha)
{
    AaRRect s;

    AaShape_RRectSet(&s, cx, cy, r, r, r);
    rrect_blit(&s, color, alpha);
}

void AaShape_RRectCorners(const leRect *rect, int32_t radius, uint8_t corners,
                          leColor color, uint32_t alpha)
{
    AaRRect s;

    if (rect == NULL || rect->width < 1 || rect->height < 1) { return; }

    AaShape_RRectSetCorners(&s, (2 * rect->x) + rect->width, (2 * rect->y) + rect->height,
                            rect->width, rect->height, 2 * radius, corners);
    rrect_blit(&s, color, alpha);
}

void AaShape_RRect(const leRect *rect, int32_t radius, leColor color, uint32_t alpha)
{
    AaShape_RRectCorners(rect, radius, AA_CORNER_ALL, color, alpha);
}
