#ifndef UI_WIDGET_WHAMMY_H
#define UI_WIDGET_WHAMMY_H

#include <stdint.h>

#include "gfx/legato/widget/legato_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bipolar centre-detented slider for the guitar's whammy bar, painted entirely by
 * this module onto a plain (background-less) leWidget: a capsule track with a
 * centre tick, a fill growing out of the centre toward the thumb, and a capsule
 * thumb. Drag anywhere on the track to set the value; releasing (or dragging off)
 * springs it back to centre, the way a real whammy bar returns.
 *
 * Value is -100..+100 with 0 at centre. Single instance — one whammy on the
 * wiimotes screen — so the state lives in the module, like widget_gauge. */

/* Notified on every value change, including the spring back to 0 on release. */
typedef void (*WhammyChangeFn)(int32_t value);

/* Take over `track`'s paint and touch handling. Give the widget no background and
 * no border; this module draws all of it. Call once after construction. */
void Whammy_Enable(leWidget *track, WhammyChangeFn onChange);

/* Current value, -100..+100. */
int32_t Whammy_Value(void);

/* Force the value (e.g. back to rest when the screen is hidden); repaints and
 * fires the change callback if it moved. */
void Whammy_Set(int32_t value);

#ifdef __cplusplus
}
#endif

#endif /* UI_WIDGET_WHAMMY_H */
