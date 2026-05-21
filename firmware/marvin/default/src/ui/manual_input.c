/* Glue between the Composer-generated Screen0 button widgets and the
 * manual_control producer. Composer wires setPressedEventCallback /
 * setReleasedEventCallback in screenShow_Screen0 referencing the extern
 * event_Screen0_Button_Manual_* symbols below; the linker resolves them
 * here. No public API — bind happens implicitly at screen-show time. */

#include "gfx/legato/generated/screen/le_gen_screen_Screen0.h"

#include "actuator/manual_control.h"
#include "game/fret.h"

void event_Screen0_Button_Manual_Green_OnPressed(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetFret(FRET_GREEN, true);
}

void event_Screen0_Button_Manual_Green_OnReleased(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetFret(FRET_GREEN, false);
}

void event_Screen0_Button_Manual_Red_OnPressed(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetFret(FRET_RED, true);
}

void event_Screen0_Button_Manual_Red_OnReleased(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetFret(FRET_RED, false);
}

void event_Screen0_Button_Manual_Yellow_OnPressed(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetFret(FRET_YELLOW, true);
}

void event_Screen0_Button_Manual_Yellow_OnReleased(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetFret(FRET_YELLOW, false);
}

void event_Screen0_Button_Manual_Blue_OnPressed(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetFret(FRET_BLUE, true);
}

void event_Screen0_Button_Manual_Blue_OnReleased(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetFret(FRET_BLUE, false);
}

void event_Screen0_Button_Manual_Orange_OnPressed(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetFret(FRET_ORANGE, true);
}

void event_Screen0_Button_Manual_Orange_OnReleased(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetFret(FRET_ORANGE, false);
}

void event_Screen0_Button_Manual_StrumDown_OnPressed(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetStrum(true, true);
}

void event_Screen0_Button_Manual_StrumDown_OnReleased(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetStrum(true, false);
}

void event_Screen0_Button_Manual_StrumUp_OnPressed(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetStrum(false, true);
}

void event_Screen0_Button_Manual_StrumUp_OnReleased(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetStrum(false, false);
}

/* Toggleable button — Composer fires OnReleased after the visual toggle
 * state has flipped. Both ManualControl and the widget default to off,
 * so a simple flip stays in sync. */
void event_Screen0_Button_Manual_Enable_OnReleased(leButtonWidget* btn)
{
    (void)btn;
    ManualControl_SetEnabled(!ManualControl_IsEnabled());
}
