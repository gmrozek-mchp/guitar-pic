#pragma once

#include <stdint.h>

/* Guitar Hero guitar extension. Init() registers it with the base Wiimote. Frets,
 * strum, etc. are driven by name through Wiimote_SetButton/TapButton (green, red,
 * yellow, blue, orange, strumup, strumdown, gplus, gminus, pedal). */
void Guitar_Init(void);
void Guitar_SetWhammy(uint8_t value);   /* 0..31 */
