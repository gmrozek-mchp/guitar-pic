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

#ifdef __cplusplus
}
#endif

#endif /* UI_WIDGET_PANEL_AA_H */
