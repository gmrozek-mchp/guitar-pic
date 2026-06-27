#ifndef UI_WIDGET_BUTTON_AA_H
#define UI_WIDGET_BUTTON_AA_H

#include "gfx/legato/widget/button/legato_widget_button.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The single corner radius the AA path supports (one precomputed mask). Buttons
 * must use this radius for the smoothing to apply. */
#define BUTTON_AA_RADIUS  12u

/* Anti-aliased rounded corners for a button. The classic skin draws rounded
 * corners with hard, stepped edges; calling this re-points the button's vtable
 * at a copy whose paint smooths the corner band after the normal draw. Only
 * affects buttons with cornerRadius == BUTTON_AA_RADIUS; set the radius first.
 * Safe to call once per button after it is constructed. */
void ButtonAA_Enable(leButtonWidget* btn);

#ifdef __cplusplus
}
#endif

#endif /* UI_WIDGET_BUTTON_AA_H */
