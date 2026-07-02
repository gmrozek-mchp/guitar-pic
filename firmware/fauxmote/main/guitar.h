#pragma once

#include <stdint.h>

/* Guitar Hero guitar extension. Init() registers it with the base Wiimote. Frets,
 * strum, etc. are driven by name through Wiimote_SetButton/TapButton (green, red,
 * yellow, blue, orange, strumup, strumdown, gplus, gminus, pedal). */
void Guitar_Init(void);
void Guitar_SetWhammy(uint8_t value);       /* 0..31 (rest ~0x10) */
void Guitar_SetStick(uint8_t x, uint8_t y); /* 6-bit analog stick 0..63, center 0x20 */
