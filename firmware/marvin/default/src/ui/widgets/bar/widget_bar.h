#ifndef UI_WIDGET_BAR_H
#define UI_WIDGET_BAR_H

#include <stdint.h>

#include "gfx/legato/widget/legato_widget.h"
#include "gfx/legato/core/legato_scheme.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A value bar: a track, a fill of an exact pixel width, anti-aliased pill ends, and an
 * optional dithered gradient — built on a plain `leWidget`, not `leProgressBarWidget`.
 *
 * Why not the stock progress bar: MGS enables Legato widget types from what the
 * *design* instantiates, so a type used only by hand-built code silently disappears the
 * moment the last design widget of that type goes (which is exactly what happened when
 * the dashboard's imported tree was replaced). Nothing was lost by dropping it — this
 * widget already drew its own fill, and the stock one contributed only a square track
 * and a vtable to override.
 *
 * The widget's own scheme supplies the track (its BASE colour, painted by the classic
 * skin as a plain filled rect); the fill and its gradient come from the arguments here.
 *
 * `radius` is carried by this module, NOT the widget's cornerRadius style: a nonzero
 * cornerRadius makes the classic skin draw its own rounded track, which oversteps the
 * rect. Leave cornerRadius 0 and pass height/2 for full pill ends.
 *
 * Per instance — several bars may be enabled independently. Call once after
 * construction, before the first paint. */
void Bar_Enable(leWidget *bar, uint32_t radius, const leScheme *bg,
                uint32_t rgb_from, uint32_t rgb_to);

/* Set the fill at pixel resolution: `permille` is 0..1000 (tenths of a percent), mapped
 * to an exact fill width, so the bar advances by single pixels rather than the stock
 * widget's coarse 1%-of-width steps. Repaints only when the pixel width actually
 * changes. Values above 1000 clamp to full; a bar that was never enabled is ignored.
 *
 * A bar left at 1000 with a gradient is simply a dithered gradient rect — which is how
 * the dashboard draws the test pattern's black→white ramp, since the Legato gradient
 * widget is subject to the same design-coupled availability described above. */
void Bar_SetPermille(leWidget *bar, uint32_t permille);

#ifdef __cplusplus
}
#endif

#endif /* UI_WIDGET_BAR_H */
