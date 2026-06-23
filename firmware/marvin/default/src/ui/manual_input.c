/* Glue between the Composer-generated Marvin screen strum buttons and the
 * manual_control producer. screenShow_Marvin wires setPressedEventCallback /
 * setReleasedEventCallback for the strum buttons referencing the extern
 * event_Marvin_BUTTON_GUITAR_STRUM_* symbols below; the linker resolves them
 * here. No public API — bind happens implicitly at screen-show time. */

#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"

#include "actuator/manual_control.h"

void event_Marvin_BUTTON_GUITAR_STRUM_UP_OnPressed(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetStrum(false, true);
}

void event_Marvin_BUTTON_GUITAR_STRUM_UP_OnReleased(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetStrum(false, false);
}

void event_Marvin_BUTTON_GUITAR_STRUM_DOWN_OnPressed(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetStrum(true, true);
}

void event_Marvin_BUTTON_GUITAR_STRUM_DOWN_OnReleased(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetStrum(true, false);
}
