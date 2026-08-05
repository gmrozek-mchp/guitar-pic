#ifndef UI_WIDGET_SPARKLINE_H
#define UI_WIDGET_SPARKLINE_H

#include <stdint.h>

#include "gfx/legato/widget/legato_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Time-series sparkline painted over a plain (transparent) leWidget: the samples
 * are drawn as a connected 2px polyline, oldest at the left edge, newest at the
 * right. Drawn with bounded per-column VertLine runs (no stock curve paint).
 *
 * Owns its own ring buffer — push a sample per refresh tick and the widget scrolls
 * itself. Single instance (the bus screen's utilization history). The line colour
 * is the widget scheme's BASE, so give it a scheme whose base is the line colour.
 * Call Sparkline_Enable once after construction. */
#define SPARKLINE_SAMPLES  40u

void Sparkline_Enable(leWidget *panel);

/* Append a sample (permille of full scale) and drop the oldest. */
void Sparkline_Push(uint32_t permille);

/* Full-scale value the plot's top edge represents (permille; default 600 = 60%,
 * matching the mockup's Y domain). */
void Sparkline_SetScale(uint32_t permille_max);

#ifdef __cplusplus
}
#endif

#endif /* UI_WIDGET_SPARKLINE_H */
