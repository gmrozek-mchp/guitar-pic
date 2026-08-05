#ifndef UI_WIDGET_FRET_H
#define UI_WIDGET_FRET_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/legato/widget/legato_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A fret pad on the manual-override screen: an AA rounded rect that is a dimmed
 * colour at rest and a brighter one, ringed, while held. Painted by this module onto
 * a plain (background-less) leWidget so the ring can be a different shade from the
 * fill — something a Legato scheme cannot express, which is why these are not just
 * schemed panels.
 *
 * Up to FRET_MAX instances, each with its own colours and held state, so unlike
 * widget_gauge the state is per-widget and looked up by pointer. */

#define FRET_MAX  5u

/* Notified on press (held=true) and release, with the index passed to Fret_Enable. */
typedef void (*FretChangeFn)(unsigned index, bool held);

/* Register `pad` with its colours and take over its paint and touch handling.
 * Colours are RGB888: `idle` is drawn at 75% over whatever is behind the pad,
 * `held` and `ring` opaque. The pad is momentary — held only while pressed.
 * Call once after construction. */
void Fret_Enable(leWidget *pad, unsigned index, uint32_t idle, uint32_t held,
                 uint32_t ring, FretChangeFn onChange);

/* Set/clear the held look; repaints only on a real change. */
void Fret_SetHeld(leWidget *pad, bool held);

bool Fret_IsHeld(const leWidget *pad);

#ifdef __cplusplus
}
#endif

#endif /* UI_WIDGET_FRET_H */
