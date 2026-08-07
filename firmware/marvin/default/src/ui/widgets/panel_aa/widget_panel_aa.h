#ifndef UI_WIDGET_PANEL_AA_H
#define UI_WIDGET_PANEL_AA_H

#include "gfx/legato/widget/legato_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Anti-aliased rounded corners for a panel (a plain leWidget). The panel-flavoured
 * counterpart to ButtonAA_Enable: re-points the widget's vtable at a copy whose
 * paint smooths the corner boxes after the normal draw — backdrop → border → fill,
 * at whatever cornerRadius the panel has (set the radius, and border, first).
 *
 * The backdrop is sampled from the panel's own corner pixel, so this is for a panel
 * drawn over an OPAQUE parent (a rounded child panel on another panel) — NOT a
 * top-level overlay panel whose corners must reveal a layer below (that needs
 * per-pixel alpha on the layer; see the journal). Call once after construction. */
void PanelAA_Enable(leWidget* panel);

/* Rounded corners for a transparent overlay panel drawn directly over an image on
 * the SAME layer (e.g. the album-art overlay over the cover strip): after the empty
 * overlay paints, the four corners of its rect are eaten back to the panel's BASE
 * colour, anti-aliased against the image pixels underneath. Set the radius first,
 * and set the scheme so BASE equals the solid backdrop the corners should match
 * (the dialog gray). Avoids the per-pixel-alpha problem PanelAA_Enable can't: the
 * corner is filled opaque with the known backdrop colour rather than made
 * transparent. Call once after construction. */
void PanelAA_EnableRoundImage(leWidget* panel);

/* Filled anti-aliased CAPSULE in the panel's BASE colour: a circle for a square
 * widget (a status dot), a stadium for an oblong one (a pill track), with the ends
 * rounded by half the smaller dimension. Drawn with Legato's vector rasterizer, so it
 * blends against whatever is behind the panel and needs no solid surround to sample.
 *
 * Takes the panel's background over: this CLEARS the background type, because a skin
 * fill underneath would show square corners around the shape. Does not set or rely on
 * cornerRadius either — Legato's stock rounded-rect paint hangs once cornerRadius
 * reaches half the widget size. Call once after construction.
 *
 * Drawn at the panel's Legato alpha, so setAlphaEnabled + setAlphaAmount fade the shape
 * (the titlebar's pulsing status LED); left alone it is opaque. */
void PanelAA_EnableDot(leWidget* panel);

/* Filled anti-aliased column in the panel's BASE colour with only its TOP two corners
 * rounded — a bottom-anchored chart bar. Drawn with the vector rasterizer from the
 * widget's rect at paint time, so the panel may be resized freely between paints.
 *
 * The radius is stored in cornerRadius, which is inert here because this also CLEARS
 * the background type (as PanelAA_EnableDot does, and for the same reason): the stock
 * rounded-rect paint would otherwise draw all four corners underneath. The rasterizer
 * clamps each radius to a quarter of the smaller side, so a bar shorter than 4×radius
 * rounds by less and a 1px bar comes out flat. Call once after construction. */
void PanelAA_EnableRoundTop(leWidget* panel, uint32_t radius);

/* Recolours the LEFT EDGE of a rounded rect already drawn beneath this panel, in the
 * panel's BASE colour, following the corners the way CSS does for the mockup's
 * `borderLeftWidth` on a rounded card — the band tapers around each corner to meet the
 * 1px border instead of stopping square (see AaCorners_RenderLeftEdge).
 *
 * The panel is a transparent overlay covering the WHOLE card and must be a LATER
 * sibling than the card, since it blends over what the card painted. Give it the
 * card's own cornerRadius; `edgeWidth` is the thick side, `borderWidth` the other
 * three. Both widths are shared by every user of this variant, as the vtable is —
 * fine while the only caller is the node-card grid. Call once after construction. */
void PanelAA_EnableLeftAccent(leWidget* panel, uint32_t edgeWidth, uint32_t borderWidth);

#ifdef __cplusplus
}
#endif

#endif /* UI_WIDGET_PANEL_AA_H */
