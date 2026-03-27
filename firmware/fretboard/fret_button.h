#ifndef FRET_BUTTON_H
#define FRET_BUTTON_H

#include <stdint.h>
#include <stdbool.h>
#include "fret_scan.h"

/* All delays in milliseconds (using SYSTICK_GetTickCounter). */

/* Ms from chord window close (commit) to strike line / strum. */
#define STRUM_DELAY_MS       220

/* Ms the fret output must be on before the strike — strum fires this many ms
   after fret press (fret at strike - FRET_EARLY, strum at strike). */
#define FRET_EARLY_MS        50

/* How long the strum button is held. */
#define STRUM_PULSE_MS       50

/* Notes detected within this window are grouped as a chord (one strum). */
#define CHORD_WINDOW_MS      20

_Static_assert(STRUM_DELAY_MS > FRET_EARLY_MS,
               "STRUM_DELAY_MS must exceed FRET_EARLY_MS");
_Static_assert(STRUM_DELAY_MS > STRUM_PULSE_MS,
               "strum pulse must fit within delay window");

void fret_button_init(void);
void fret_button_update(uint32_t now);
bool fret_button_is_enabled(void);

#endif
