#ifndef UI_GFX_AA_CORNERS_H
#define UI_GFX_AA_CORNERS_H

#include "gfx/legato/legato.h"   /* leRect, leColor, leColorMode */

#ifdef __cplusplus
extern "C" {
#endif

/* Anti-alias the four rounded corners of a rect that a classic skin just drew with
 * a stepped arc. For each corner it repaints a radius×radius box: the backdrop
 * outside the arc (sampled once from the box's outer pixel — assumes a solid
 * backdrop behind the corner, true for our panels), an optional borderWidth-px
 * ring in `border`, then `fill` inside, with 1px anti-aliased bands at the arc
 * edges. borderWidth 0 → no border (backdrop↔fill).
 *
 * Coverage is computed analytically per pixel, so `radius` is arbitrary (bounded
 * by half the rect's smaller side). `mode` is the active render colour mode
 * (leRenderer_CurrentColorMode()). Call from a widget's paint override after the
 * normal draw; it only touches the corner boxes, so the straight edges the skin
 * drew (and any centred text) are left intact. Cheap, and runs only when the
 * widget repaints (state change) — not per frame.
 *
 * Widget-agnostic: the caller extracts fill/border/radius from its own widget +
 * scheme (see widget_button_aa.c). */
void AaCorners_Render(const leRect *rect, uint32_t radius, uint32_t borderWidth,
                      leColor fill, leColor border, leColorMode mode);

/* Round the corners of content already drawn into `rect` (e.g. an album-art image)
 * by AA-blending the corner wedges toward `bg`, a KNOWN solid backdrop colour.
 * Unlike AaCorners_Render, the inside of the arc is left untouched — only pixels
 * outside the arc are replaced (fully → bg) with a 1px anti-aliased band, so the
 * image keeps every interior pixel and only its square corners are eaten back to
 * `bg`. Use when the backdrop behind the rounded shape is a solid colour you can
 * name (here the dialog's 0x18181B); a varying backdrop would have to be sampled
 * per pixel instead. `mode` is leRenderer_CurrentColorMode(). */
void AaCorners_RenderRoundImage(const leRect *rect, uint32_t radius,
                                leColor bg, leColorMode mode);

/* Recolour the left edge of an already-drawn rounded rect, following its corners.
 * Reproduces what CSS does for `borderLeftWidth: 6px; borderLeftColor: <accent>` on a
 * `rounded-2xl border` box (the mockup's node cards), which is two things a plain bar
 * cannot express:
 *
 *  - The band's INNER edge is an ellipse, radii (radius - edgeWidth) horizontally and
 *    (radius - borderWidth) vertically, so it is edgeWidth thick where it meets the
 *    straight left edge and tapers to borderWidth where it meets the top/bottom one.
 *  - The colour hands over along the MITRE — the line from the outer corner toward
 *    (edgeWidth, borderWidth) — so the arc is accent below it and left alone above.
 *
 * Only band pixels are touched, and they are blended over what is already there
 * rather than over a sampled backdrop: the widget beneath has by then painted its
 * fill, its 1px border and its own AA corners, so the outer AA edge lands on real
 * backdrop and the inner one on real fill, both already correct. Call from a paint
 * override on a transparent sibling drawn AFTER the shape (see widget_panel_aa.c).
 *
 * `rect` is the whole rounded rect, not just the edge. `mode` is
 * leRenderer_CurrentColorMode(). */
void AaCorners_RenderLeftEdge(const leRect *rect, uint32_t radius,
                              uint32_t edgeWidth, uint32_t borderWidth,
                              leColor accent, leColorMode mode);

/* Backdrop colour to show outside the arc at (x, y), in surface coordinates. */
typedef leColor (*aa_backdrop_fn)(void *ctx, int32_t x, int32_t y);

/* Round the corners of a rect inside a raw RGB565 surface, blending toward a
 * PER-PIXEL backdrop from `sample` (backdrop → borderWidth-px `border` → `fill`,
 * as AaCorners_Render). Two differences from the functions above, both deliberate:
 *
 *  - It writes the surface directly instead of going through leRenderer, so it runs
 *    OUTSIDE a paint pass — the caller picks the moment.
 *  - The backdrop is sampled per pixel, so it can be content from a DIFFERENT
 *    surface. That is what lets a top-level overlay panel appear rounded without
 *    per-pixel alpha: the corner is filled with the pixels the layer below has at
 *    the same screen position (see screen_song_select.c), which reads as
 *    transparency as long as that content is static while the overlay is up.
 *
 * `stride` is in pixels. Coordinates are surface-relative and are not clipped —
 * `rect` must lie inside the surface. */
void AaCorners_RenderSurface565(uint16_t *surface, uint32_t stride,
                                const leRect *rect, uint32_t radius, uint32_t borderWidth,
                                leColor fill, leColor border,
                                aa_backdrop_fn sample, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* UI_GFX_AA_CORNERS_H */
