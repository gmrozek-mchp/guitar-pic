#ifndef FRET_BUTTON_H
#define FRET_BUTTON_H

#include <stdint.h>
#include <stdbool.h>
#include "fret_scan.h"

/* Delay from sensor detection to strum (strike line), in scan sweeps.
   At ~950 sweeps/sec, 500 ≈ 526ms.  Tune to match sensor-to-strike distance. */
#define STRUM_DELAY_SWEEPS   1450

/* Fret buttons press this many sweeps before the strum fires,
   so the game sees the chord held when the strum lands. */
#define FRET_EARLY_SWEEPS    200

/* How long the strum button is held per note, in scan sweeps. */
#define STRUM_PULSE_SWEEPS   200

void fret_button_init(void);

/* Call once per scan sweep, after fret_detect_update().
   Handles SW0 toggle, LED0 indication, and delayed button actuation. */
void fret_button_update(void);

bool fret_button_is_enabled(void);

#endif
