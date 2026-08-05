#ifndef UI_WIDGET_TILT_H
#define UI_WIDGET_TILT_H

#include <stdint.h>

#include "gfx/legato/widget/legato_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Quarter-arc tilt control for simulating the guitar's neck angle, painted entirely
 * by this module onto a plain (background-less) leWidget. The arc sweeps the
 * upper-left quadrant — 0 degrees at the left (guitar held at rest), 90 degrees at
 * the top (neck straight up) — with a thick round-capped track, a fill from the
 * start to the thumb, and a round thumb. Touch anywhere in the widget to aim the
 * thumb at that angle and drag to sweep; unlike the whammy this does NOT spring
 * back, since a tilted guitar stays tilted.
 *
 * Value is 0..90 degrees. Single instance — one tilt control on the wiimotes
 * screen — so the state lives in the module, like widget_gauge. */

typedef void (*TiltChangeFn)(int32_t degrees);

/* Take over `arc`'s paint and touch handling. Give the widget no background and no
 * border; this module draws all of it. Call once after construction. */
void Tilt_Enable(leWidget *arc, TiltChangeFn onChange);

/* Current angle, 0..90 degrees. */
int32_t Tilt_Degrees(void);

/* Force the angle; repaints and fires the change callback if it moved. */
void Tilt_Set(int32_t degrees);

#ifdef __cplusplus
}
#endif

#endif /* UI_WIDGET_TILT_H */
