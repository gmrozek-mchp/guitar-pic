#ifndef FRET_BUTTON_H
#define FRET_BUTTON_H

#include <stdint.h>
#include <stdbool.h>
#include "fret_scan.h"

/* Delay from sensor detection to strum (strike line), in scan sweeps. */
#define STRUM_DELAY_SWEEPS   1400

/* Fret buttons press this many sweeps before the strum fires. */
#define FRET_EARLY_SWEEPS    200

/* How long the strum button is held, in scan sweeps. */
#define STRUM_PULSE_SWEEPS   200

/* Notes detected within this window are grouped as a chord (one strum).
   At ~950 sweeps/sec, 40 ≈ 42ms. */
#define CHORD_WINDOW_SWEEPS  40

void fret_button_init(void);
void fret_button_update(void);
bool fret_button_is_enabled(void);

#endif
