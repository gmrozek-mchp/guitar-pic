#ifndef LEGATO_INIT_H
#define LEGATO_INIT_H

/* Compatibility stub for the MGS-generated le_gen_init.h.
 *
 * With "Generate Screen State Machine" disabled, MGS deletes its own
 * gfx/legato/generated/le_gen_init.{c,h} but still emits `#include
 * "gfx/legato/generated/le_gen_init.h"` in le_gen_harmony.h — leaving a dangling
 * include (MGS bug). Rather than re-patch le_gen_harmony.h after every regen,
 * this stub lives outside the regenerated tree (firmware/marvin/default/src/compat,
 * added to the include path in user.cmake) and satisfies that include. It is
 * only found when the real generated header is absent (state machine off); if
 * the state machine is ever re-enabled, the generated copy in its own directory
 * wins.
 *
 * It provides only what le_gen_harmony.c actually needs (the global palette via
 * le_gen_scheme.h). Screen headers are intentionally NOT included here — nothing
 * that includes le_gen_init.h references screen symbols, and leaving them out
 * keeps this stub immune to screen add/rename/remove churn. Each UI module
 * includes the specific screen header it uses and calls screenInit_/screenShow_
 * directly (screen orchestration is owned by the app — see ui/ui_manager.c). */

#include "gfx/legato/legato.h"

#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"

#endif /* LEGATO_INIT_H */
