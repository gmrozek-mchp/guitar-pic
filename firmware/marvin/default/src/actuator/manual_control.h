#ifndef MANUAL_CONTROL_H
#define MANUAL_CONTROL_H

#include <stdbool.h>

#include "game/fret.h"

/* Manual fretboard control — peer producer to timing_pipeline. Driven by
 * on-device UI button events (Microchip Graphics Composer widgets, bound by
 * ui/manual_input.c). Primary use: manual game-menu navigation (Down strum
 * to scroll, Green to confirm, Mode toggle to flip detection on/off).
 *
 * Single-finger momentary model. Legato's input dispatch routes each touch
 * to a single focus widget (legato_input.c:340/429), so multi-finger chord
 * inputs aren't supported here — would require a custom maxtouch dispatcher,
 * and menu navigation doesn't need them.
 *
 * Mode arbitration with timing_pipeline lives in SetEnabled: entering manual
 * mode gates the pipeline off, exiting hands the wire back. */

void ManualControl_Initialize(void);

void ManualControl_SetEnabled(bool enabled);
bool ManualControl_IsEnabled(void);

/* Momentary fret. Hold pressed=true while the UI button is down; pressed=false
 * on touch-up. */
void ManualControl_SetFret(fret_t fret, bool pressed);

/* Momentary strum. Hold pressed=true while the UI button is down. The bit
 * is held until release — Guitar Hero menus auto-repeat-scroll on a held
 * strum line, which is the primary thing this surface is used for. The
 * detector-driven gameplay path (timing_pipeline) does its own one-shot
 * pulsing; manual mode and gameplay don't share this code path. */
void ManualControl_SetStrum(bool down, bool pressed);

#endif
