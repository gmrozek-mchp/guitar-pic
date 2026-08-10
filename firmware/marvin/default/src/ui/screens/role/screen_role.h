#ifndef UI_SCREENS_ROLE_SCREEN_ROLE_H
#define UI_SCREENS_ROLE_SCREEN_ROLE_H

#include <stdbool.h>
#include <stdint.h>

#include "results/results.h"   /* results_affil_t — what this dialog answers */

#ifdef __cplusplus
extern "C" {
#endif

/* Player-affiliation modal (Marvin layer 10 / CANVAS_ROLE, a 480x268 centered dialog):
 * "ARE YOU <name>" over CLIENT / PARTNER and MICROCHIP EMPLOYEE.
 *
 * Second half of the 2-player prompt — it opens straight after the keyboard's OK, so a
 * name and an affiliation are collected as one act, and the run starts from the answer
 * here rather than from the name. It is deliberately its own layer rather than a second
 * panel on the keyboard's: a Legato layer holds one root panel, and the keyboard's already
 * carries the keyboard's own fill and 1060x560 geometry. It cannot go on the dashboard
 * layer either — HEO composites the live video above BASE and would cut through a centred
 * dialog, which is why every modal here has a layer of its own.
 *
 * Same division of labour as the keyboard: ui_manager owns show/hide (binding the canvas
 * to a hardware layer, the scrim, the corner cut); this module owns the content and the
 * dispatch. Built programmatically into Marvin_PANEL_ROLE, static widget storage. */

/* Invoked when the operator picks a role. The dialog is closed immediately after.
 *
 * `commit` fires on a role button and ONLY on one — dismissing with X cancels the session
 * and never calls it, which is what makes X abort the whole run start rather than just the
 * affiliation question. Fires at most once per session. Mirrors keyboard_commit_fn. */
typedef void (*role_commit_fn)(results_affil_t affil);

/* Assign the layer-10 canvas surface. Pre-scheduler (from UiManager_Initialize). */
void ScreenRole_InitSurface(void);

/* Build the dialog once into Marvin_PANEL_ROLE. Called from ui_manager's screen setup;
 * leaves it input-gated off (not shown). */
void ScreenRole_Setup(void);

/* Seed one session: the name to ask about (shown verbatim) and the commit callback. Call
 * before showing the dialog — UiManager_OpenRole does this. */
void ScreenRole_Prepare(const char *name, role_commit_fn commit);

/* Gate the dialog's subtree in/out of touch picking (ui_manager toggles this on open/close
 * so the always-attached layer doesn't swallow touches while hidden). */
void ScreenRole_SetInput(bool on);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREENS_ROLE_SCREEN_ROLE_H */
