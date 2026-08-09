#pragma once

#include <stdint.h>

/* The Feather V2's single onboard NeoPixel (data GPIO 0, power-enable GPIO 2 —
 * which also feeds the STEMMA QT connector). One pixel means one colour at a
 * time; status_led.c time-slices red and blue across it. */
void Neopixel_Init(void);

/* Latch one colour. Values are raw 0..255 per channel; the pixel is bright, so
 * status use wants small numbers (see LED_LEVEL in status_led.c). */
void Neopixel_Set(uint8_t r, uint8_t g, uint8_t b);
