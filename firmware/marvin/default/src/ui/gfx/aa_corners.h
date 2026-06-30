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

#ifdef __cplusplus
}
#endif

#endif /* UI_GFX_AA_CORNERS_H */
