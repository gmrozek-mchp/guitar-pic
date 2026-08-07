#ifndef UI_GFX_RENDER_PROBE_H
#define UI_GFX_RENDER_PROBE_H

#include <stdint.h>

#include "gfx/legato/widget/legato_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Time one Legato frame that damages `target`, `iters` times, and return the mean in
 * microseconds. 0 if the arguments are unusable.
 *
 * How to read a result: subtract what a *contentless* widget of the same rect costs (see
 * Titlebar_ProbeFrameUs, which parks an invisible resizable widget for exactly this) and the
 * remainder is the target's own drawing. A frame carries ~85 µs of fixed overhead plus about
 * 30 ns per damaged pixel for the parent's fill, both measured.
 *
 * Caveats worth knowing before trusting small deltas:
 *  - The render lock suspends LEGATO_Tasks and the input task, but NOT the capture DMA, T1S
 *    or USB, so results move with DDR contention. The fixed cost has been seen at 82 µs and
 *    at 165 µs on the same build.
 *  - A widget whose paint depends on its data (a plot, a slider at some value) costs a
 *    different amount every run. Probe those at a fixed value or treat the number as an
 *    order of magnitude.
 *
 * Driving the painter outside LEGATO_Tasks is legitimate here because this takes the render
 * lock, whose contract is precisely that LEGATO_Tasks is suspended and no paint is in
 * flight. Must not be called from LEGATO_Tasks itself. */
uint32_t RenderProbe_WidgetUs(leWidget *target, unsigned iters);

#ifdef __cplusplus
}
#endif

#endif /* UI_GFX_RENDER_PROBE_H */
