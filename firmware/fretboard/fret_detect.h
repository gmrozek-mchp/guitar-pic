#ifndef FRET_DETECT_H
#define FRET_DETECT_H

#include <stdint.h>
#include <stdbool.h>
#include "fret_scan.h"

#define FRET_PRESS_THRESHOLD    2000
#define FRET_RELEASE_THRESHOLD  2500

void fret_detect_init(void);

/* Call after each completed scan sweep. */
void fret_detect_update(void);

bool fret_is_pressed(fret_channel_t ch);

/* Returns a bitmask of channels that transitioned to pressed since
   the last call (edge-triggered, auto-clears). */
uint8_t fret_detect_new_presses(void);

#endif
