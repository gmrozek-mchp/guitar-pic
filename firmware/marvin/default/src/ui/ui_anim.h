#ifndef UI_UI_ANIM_H
#define UI_UI_ANIM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-frame ticks for UI animation that has no other driver.
 *
 * Legato's own animation hook — the `update()` widget vtable slot — is NOT called in this
 * build. `updateWidgets` in legato_state.c is compiled out under
 * `!MARVIN_ANY_UPDATING_WIDGET` (re-apply patch #15), because the stock walk recursed every
 * widget on every layer at 100 Hz to reach four widget types that are all disabled here:
 * ~85,000 no-op vtable calls a second, measured at 3.85% CPU. That patch stays.
 *
 * This is the replacement, and it is cheap for the opposite reason: it knows what is
 * animating instead of searching for it. The ticker task **blocks on a semaphore whenever
 * nothing is registered**, so an idle system pays nothing at all — not even a poll. A
 * caller registers when motion starts, the task steps it under UiManager_RenderLock, and
 * the moment every step reports "finished" the task blocks again.
 *
 * Steps run on the ticker task with the render lock held, so a step may edit widgets and
 * invalidate, but must not block.
 *
 * Timing: a step is passed nothing and should measure elapsed time itself (xTaskGetTickCount)
 * rather than assume the tick period. UIANIM_STEP_MS is a smoothness knob, not part of the
 * physics — a paint slower than the step period simply drops frames, and time-based motion
 * covers the same distance regardless. */

/* Step period while anything is animating. 30 Hz rather than 60: the cheapest list this
 * drives repaints in an estimated 20–26 ms, so 33 ms leaves headroom for the rest of the
 * system instead of pinning the render task. */
#define UIANIM_STEP_MS   33u

/* Return true to keep being stepped, false when the animation is finished. */
typedef bool (*uianim_step_fn)(void *ctx);

/* Create the ticker task. Call once from UiManager_Initialize, pre-scheduler. */
void UiAnim_Initialize(void);

/* Register (fn, ctx) and wake the ticker. Idempotent — registering an already-registered
 * pair does nothing, so a caller may start on every gesture without tracking state. */
void UiAnim_Start(uianim_step_fn fn, void *ctx);

/* Deregister early. Not required: returning false from the step does the same thing. */
void UiAnim_Stop(uianim_step_fn fn, void *ctx);

/* True while anything is registered. For diagnostics. */
bool UiAnim_Active(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_UI_ANIM_H */
