#ifndef UI_GFX_AA_SHAPE_H
#define UI_GFX_AA_SHAPE_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/legato/legato.h"   /* leRect, leColor */

#ifdef __cplusplus
extern "C" {
#endif

/* Filled anti-aliased capsule, blended over whatever is already in the framebuffer: a disc
 * for a square rect, a stadium for an oblong one, with the ends rounded by half the smaller
 * dimension. `alpha` scales the whole shape (255 = opaque).
 *
 * Coverage is analytic — the distance from each pixel centre to the capsule's spine, with
 * the interior and the exterior settled by squared-distance comparison so only the ~1 px rim
 * needs a square root.
 *
 * This exists because `leDraw_VectorArcFill` cannot be used for small shapes on this core.
 * Measured on hardware, a radius-6 disc through it costs **5.7 ms**: it takes 8 supersamples
 * per pixel and each one runs `leVector2_Normalize` (software square root plus a 64-bit
 * divide) and `leReal_i16_Atan2` (more 64-bit divides), on an ARM926 with neither an FPU nor
 * a hardware integer divide — ~32,000 cycles per pixel. The same 12×12 dot costs ~36 `sqrtf`
 * calls here. See the journal (2026-08-07 night) and the 2026-08-07 correction at the top of
 * `legato_vector_review.md`.
 *
 * Call from a widget's paint override. Clipped per pixel to the active draw rect, so it is
 * safe to call with a rect wider than the current damage. */
void AaShape_Capsule(const leRect *rect, leColor color, uint32_t alpha);

/* Filled anti-aliased disc, centre and radius in HALF-PIXEL units (a pixel centre is
 * `2·p + 1`, so this addresses half-pixel positions and half-pixel radii that a leRect
 * cannot express). Needed where several discs must share one centre while having radii of
 * different parity — the tilt gauge's thumb is a 13 px rim around a 10.5 px body, and its
 * arc caps sit at arbitrary points along the arc. */
void AaShape_Disc(int32_t cx, int32_t cy, int32_t r, leColor color, uint32_t alpha);

/* Filled anti-aliased rounded rectangle, `radius` in PIXELS (the widget-facing unit). */
void AaShape_RRect(const leRect *rect, int32_t radius, leColor color, uint32_t alpha);

/* Rounded rectangle with only the named corners rounded; `radius` in PIXELS. */
void AaShape_RRectCorners(const leRect *rect, int32_t radius, uint8_t corners,
                          leColor color, uint32_t alpha);

/* Coverage queries, for a widget that composites several overlapping capsules per pixel and
 * so cannot use the blit above — the whammy slider clips a fill capsule and a two-tone thumb
 * to a track capsule. Prepare each shape once, then query per pixel.
 *
 * Everything is in HALF-PIXEL units, which is what keeps it integer: a pixel centre is at
 * `2·px + 1`, a rect's centre at `2·rect.x + rect.width`, and a half-extent is just the
 * width. Coverage comes back 0..AA_COV_ONE; multiply two of them with
 * `(a * b) / AA_COV_ONE`. */
#define AA_COV_ONE  64u

/* A rounded rectangle, which is the general case every shape here is: a capsule is one whose
 * radius equals its shorter half-extent, and a disc is one with no straight part at all. The
 * skeleton is the inner rectangle inset by the radius, so the distance to it — and therefore
 * the coverage — is one expression for all three. */
typedef struct {
    int32_t cx, cy;      /* centre, half-pixel units                        */
    int32_t hw, hh;      /* half-extents                                    */
    int32_t ix, iy;      /* inner-rect half-extents (hw - r, hh - r)        */
    int32_t r;           /* corner radius, half-pixel units                 */
    int32_t r_in, r_out; /* (r ∓ 1)², the saturated-coverage thresholds     */
    uint8_t corners;     /* which corners are rounded (AA_CORNER_*)         */
} AaRRect;

/* Rounding is per corner so a bottom-anchored chart bar can round only its top — the shape
 * `panel_aa`'s round-top variant draws. A square corner simply falls back to the straight
 * edges, which the distance form already produces. */
#define AA_CORNER_TL   0x1u
#define AA_CORNER_TR   0x2u
#define AA_CORNER_BL   0x4u
#define AA_CORNER_BR   0x8u
#define AA_CORNER_TOP  (AA_CORNER_TL | AA_CORNER_TR)
#define AA_CORNER_ALL  (AA_CORNER_TOP | AA_CORNER_BL | AA_CORNER_BR)

/* `hw`/`hh`/`r` are in half-pixel units: a half-extent is numerically the rect's width or
 * height, and a radius is twice its pixel value. `r` is clamped to the shorter half-extent. */
void AaShape_RRectSet(AaRRect *s, int32_t cx, int32_t cy, int32_t hw, int32_t hh, int32_t r);

/* As above, rounding only the named corners. */
void AaShape_RRectSetCorners(AaRRect *s, int32_t cx, int32_t cy,
                             int32_t hw, int32_t hh, int32_t r, uint8_t corners);

uint32_t AaShape_RRectCov(const AaRRect *s, int32_t x, int32_t y);

/* Capsule: a rounded rect whose radius is its shorter half-extent. */
typedef AaRRect AaCapsule;

void AaShape_CapsuleSet(AaCapsule *c, int32_t cx, int32_t cy, int32_t hw, int32_t hh);

#define AaShape_CapsuleCov(c, x, y)  AaShape_RRectCov((c), (x), (y))

/* Coverage of a 1px-wide vertical column centred on `cx` — the whammy's centre tick. */
uint32_t AaShape_ColumnCov(int32_t cx, int32_t x);

/* Sine and cosine in Q12 (4096 = 1.0) for any whole-degree angle, from a 91-entry quadrant
 * table plus symmetry. Angles are measured as Legato's vector API does: 0 to the right,
 * counter-clockwise, y upward. A table because a soft-float `sinf` is a libgcc call here. */
int32_t AaShape_SinQ12(int32_t deg);
int32_t AaShape_CosQ12(int32_t deg);

/* Anti-aliased circular ring band (an annulus of half-width `hw` about radius `r`), two-toned
 * by an angular wedge. All lengths in HALF-PIXEL units, all angles in whole degrees.
 *
 * The ring covers [a0, a1]; within it, [f0, f1] is drawn in `fill` and the rest in `track`.
 * Pass f1 <= f0 for a single-colour ring. **Neither span may exceed 180 degrees** — both
 * callers are a quadrant or a half-turn — which is what lets the angular test be two
 * half-plane cross products, with no wraparound case and no `atan2` anywhere.
 *
 * Colouring per pixel in one pass, rather than stroking two arcs, is deliberate: overlapping
 * strokes blend their antialiased seam twice and darken it.
 *
 * Draws the band only. Round caps are discs — the caller places them, since each end's colour
 * depends on whether it falls inside the wedge. */
void AaShape_ArcRing(int32_t cx, int32_t cy, int32_t r, int32_t hw,
                     int32_t a0, int32_t a1, int32_t f0, int32_t f1,
                     leColor track, leColor fill, uint32_t alpha);

/* Integer square root, bit-by-bit: ~16 shifts and conditional subtracts against a soft-float
 * `sqrtf` libgcc call. Exposed because every analytic-coverage widget here needs it and this
 * core has no FPU. */
uint32_t AaShape_Isqrt(uint32_t v);

#ifdef __cplusplus
}
#endif

#endif /* UI_GFX_AA_SHAPE_H */
