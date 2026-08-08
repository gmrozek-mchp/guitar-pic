#ifndef UI_WIDGET_SLIDE_UNLOCK_H
#define UI_WIDGET_SLIDE_UNLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/legato/string/legato_string.h"
#include "gfx/legato/widget/legato_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Slide-to-unlock gate, painted entirely by this module onto a plain (background-less)
 * leWidget: a capsule track carrying a caption, a translucent fill growing behind the
 * thumb, and a capsule thumb with a chevron.
 *
 * Only a drag opens it. The press must land on the thumb to start one, and the thumb must
 * then be pulled to the right-hand end; releasing short of it springs the thumb back and
 * nothing happens, and a press anywhere else on the track does nothing at all. That is the
 * point of the control, so it is a contract and not an implementation detail: a gate that
 * could be opened by one touch would not be worth having.
 *
 * Track, fill, caption and thumb are one paint rather than four widgets because they
 * overlap in that order — the thumb has to cover the caption, and a child widget always
 * paints AFTER its parent, so no arrangement of widgets can interleave them.
 *
 * Single instance — one gate on the wiimotes screen — so the state lives in the module,
 * like widget_whammy. */

/* Fired once, when the thumb reaches the unlock threshold. */
typedef void (*SlideUnlockFn)(void);

/* Take over `track`'s paint and touch handling. `caption` is drawn centred on the track
 * and fades out as the thumb advances; `chevron` is drawn on the thumb. Either may be
 * NULL. Give the widget no background and no border; this module draws all of it. Call
 * once after construction. */
void SlideUnlock_Enable(leWidget *track, leString *caption, leString *chevron,
                        SlideUnlockFn onUnlock);

/* Back to the locked position, repainted. Does not fire the callback — this is the
 * caller re-arming the gate, not the user failing to open it. */
void SlideUnlock_Reset(void);

/* True once the thumb has reached the threshold, until the next Reset. */
bool SlideUnlock_IsUnlocked(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_WIDGET_SLIDE_UNLOCK_H */
