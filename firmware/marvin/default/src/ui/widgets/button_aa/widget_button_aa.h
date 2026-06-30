#ifndef UI_WIDGET_BUTTON_AA_H
#define UI_WIDGET_BUTTON_AA_H

#include "gfx/legato/widget/button/legato_widget_button.h"

#ifdef __cplusplus
extern "C" {
#endif

/* House default corner radius, shared so AA'd buttons look consistent. The AA path
 * supports any radius (coverage is computed analytically — see ui/gfx/aa_corners),
 * so this is just a convenient default, not a constraint. */
#define BUTTON_AA_RADIUS  12u

/* Anti-aliased rounded corners for a button. The classic skin draws rounded
 * corners (and any LINE border) with hard, stepped edges; calling this re-points
 * the button's vtable at a copy whose paint smooths the corner boxes after the
 * normal draw — backdrop → border → fill, at whatever cornerRadius the button has.
 * Set the radius (and border) first. Safe to call once per button after it is
 * constructed. */
void ButtonAA_Enable(leButtonWidget* btn);

#ifdef __cplusplus
}
#endif

#endif /* UI_WIDGET_BUTTON_AA_H */
