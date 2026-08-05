#ifndef UI_WIDGET_GAUGE_H
#define UI_WIDGET_GAUGE_H

#include <stdint.h>

#include "gfx/legato/widget/legato_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Semicircular gauge painted over a plain (transparent) leWidget: a rounded track
 * arc sweeping left→right across the top half of the widget, filled up to the
 * current value. Anti-aliased by ring-distance coverage, drawn with the same
 * bounded PutPixel loop AaCorners uses (deliberately not Legato's stock ArcLine).
 *
 * Radius is derived from the widget width, so size the widget ~(2r + t) wide by
 * ~(r + t) tall — e.g. 76x44 with thickness 7 matches the mockup.
 *
 * Single instance (the bus screen's utilization card). Colours come from schemes'
 * BASE, so pass fill/track schemes whose base is the colour you want. Call
 * Gauge_Enable once after construction, then Gauge_Set on each refresh. */
void Gauge_Enable(leWidget *panel, uint32_t thickness, const leScheme *track);

/* Set the sweep (permille of full scale, 0..1000) and the fill colour. Takes
 * effect on the next paint; the caller invalidates. */
void Gauge_Set(uint32_t permille, const leScheme *fill);

#ifdef __cplusplus
}
#endif

#endif /* UI_WIDGET_GAUGE_H */
