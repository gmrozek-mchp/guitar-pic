#ifndef FRET_BUTTON_H
#define FRET_BUTTON_H

#include <stdint.h>
#include <stdbool.h>
#include "fret_scan.h"

/* All delays in milliseconds (using SYSTICK_GetTickCounter). */

/* Ms from chord window close (commit) to strike line / strum. */
#define STRUM_DELAY_MS       200

/* Ms the fret output must be on before the strike — strum fires this many ms
   after fret press (fret at strike − FRET_EARLY, strum at strike). */
#define FRET_EARLY_MS        50

/* How long the strum button is held. */
#define STRUM_PULSE_MS       50

/* Notes detected within this window are grouped as a chord (one strum). */
#define CHORD_WINDOW_MS      40

/* Set to 1 to enable hammer-on / pull-off (skip strum when chain rules match). */
#define FRET_ENABLE_HO_PO    0

#if FRET_ENABLE_HO_PO
/* HO/PO: new strike must fall within this many ms of the previous strike. */
#define HO_PO_MAX_MS       200
#endif

void fret_button_init(void);
void fret_button_update(void);
bool fret_button_is_enabled(void);

#endif
