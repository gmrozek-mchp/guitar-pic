#ifndef UI_GFX_VEC_DRAW_H
#define UI_GFX_VEC_DRAW_H

#include "gfx/legato/common/legato_rect.h"
#include "gfx/legato/vector/legato_vector.h"

/* Shared conventions for Legato's vector rasterizer (`gfx/legato/vector/`), which the
 * custom widgets use instead of hand-rolled per-pixel coverage: it is all Q16.16 fixed
 * point (no soft float on this FPU-less core), clips to the widget's damage rect, and
 * blends against the real framebuffer.
 *
 * Every leDraw_Vector* call passes UI_VEC_AA and `hardness = LE_REAL_I16_ONE`. The
 * hardness pin is load-bearing, not cosmetic: a soft-edged call leaves the gradient
 * shader installed on that vector module for every later call (see
 * docs/legato_vector_review.md §5). */
#define UI_VEC_AA           LE_ANTIALIASING_8X

#define UI_VEC_DEG16(deg)   ((int32_t)(deg) * 16)
#define UI_VEC_FULL_CIRCLE  UI_VEC_DEG16(360)

/* leRectF (centre + half extents) covering the whole pixel area of `rect`.
 * leRectF_FromRect spans pixel *centres* instead, which leaves every edge pixel of a
 * fill half covered. */
static inline void UiVec_RectF(const leRect *rect, leRectF *out)
{
    out->extents.x = LE_REAL_I16_FROM_INT(rect->width)  / 2;
    out->extents.y = LE_REAL_I16_FROM_INT(rect->height) / 2;
    out->origin.x  = LE_REAL_I16_FROM_INT(rect->x) + out->extents.x;
    out->origin.y  = LE_REAL_I16_FROM_INT(rect->y) + out->extents.y;
}

/* The point at `deg16` (1/16 degrees, 0 to the right, counter-clockwise) on the circle
 * of `radius` about `centre` — the same conversion leDraw_VectorArc* applies to its end
 * caps, so a shape placed here lands exactly on an arc's end. */
static inline void UiVec_ArcPoint(const leVector2 *centre,
                                  leReal_i16 radius,
                                  int32_t deg16,
                                  leVector2 *out)
{
    leReal_i16 rad = LE_REAL_I16_MULTIPLY(LE_REAL_I16_FROM_INT(deg16),
                                          LE_REAL_I16_RADIANS) / 16;
    leVector2  v   = { .x = radius, .y = 0 };

    leVector2_Rotate(&v, -rad, &v);

    out->x = centre->x + v.x;
    out->y = centre->y + v.y;
}

#endif /* UI_GFX_VEC_DRAW_H */
