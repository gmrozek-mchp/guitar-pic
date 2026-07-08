#ifndef UI_WIDGET_PROGRESSBAR_AA_H
#define UI_WIDGET_PROGRESSBAR_AA_H

#include "gfx/legato/widget/progressbar/legato_widget_progressbar.h"
#include "gfx/legato/core/legato_scheme.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Anti-aliased pill ends for a progress bar drawn over a solid backdrop. The stock
 * classic skin fills the track and the value bar as square rects; this re-points the
 * bar's vtable at a copy whose paint, after the normal draw, eats the four corners of
 * the bar's rect back to `bg`'s BASE colour — the known solid backdrop behind the bar.
 * The whole bar then reads as a rounded capsule, and the fill's leading cap rounds with
 * it (interior pixels are preserved, only the outside-arc wedges fade to bg). The colour
 * is resolved at paint time in the active render mode.
 *
 * `radius` is carried here, NOT via the widget's cornerRadius style — the stock classic
 * skin draws its own (mis-sized) rounded track when the widget cornerRadius is nonzero,
 * so the widget must keep cornerRadius 0 and let this pass do the rounding. Use e.g.
 * height/2 for full pill ends. Call once after construction. */
void ProgressBarAA_EnableRoundImage(leProgressBarWidget* bar, uint32_t radius,
                                    const leScheme* bg);

/* Set the fill at pixel resolution: `permille` is 0..1000 (tenths of a percent),
 * mapped to an exact fill width so the bar advances by single pixels instead of the
 * stock widget's coarse 1%-of-width steps (~3.5px on a 349px bar). Drives the paint
 * override directly (draws the fill over the stock track, then rounds) and repaints
 * the whole bar on change; use INSTEAD of setValue. Values above 1000 clamp to full. */
void ProgressBarAA_SetPermille(leProgressBarWidget* bar, uint32_t permille);

#ifdef __cplusplus
}
#endif

#endif /* UI_WIDGET_PROGRESSBAR_AA_H */
