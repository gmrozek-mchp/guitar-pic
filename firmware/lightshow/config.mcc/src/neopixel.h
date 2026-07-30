#ifndef NEOPIXEL_H
#define NEOPIXEL_H

#include <stdint.h>
#include <stdbool.h>

/* WS2812 driver for two parallel strands on TC0/WO0 (PA10) and TC0/WO1 (PA11).
 *
 * TC0 runs in 8-bit NPWM at an 800 kHz bit clock; each WS2812 bit is one TC0
 * period whose high-time encodes 0 or 1. DMAC channel 0 feeds the per-bit duty:
 * one HWORD beat per TC0 overflow writes CCBUF0 (low byte -> WO0/strand 0) and
 * CCBUF1 (high byte -> WO1/strand 1) at once, so both strands clock in lockstep
 * off a single interleaved duty buffer.
 *
 * Call NeoPixel_Initialize() once after SYS_Initialize. Set pixels with
 * NeoPixel_SetPixel(), then NeoPixel_Show() to latch a frame out; Show() is
 * non-blocking and returns false while a previous frame is still transmitting. */

#define NEOPIXEL_STRANDS   (2u)
#define NEOPIXEL_COUNT     (33u)   /* pixels per strand */

void NeoPixel_Initialize(void);

/* Zero the framebuffer (all pixels off). Does not transmit. */
void NeoPixel_Clear(void);

/* Stage one pixel's colour in the framebuffer. strand < NEOPIXEL_STRANDS,
 * index < NEOPIXEL_COUNT; out-of-range calls are ignored. Does not transmit. */
void NeoPixel_SetPixel(uint8_t strand, uint16_t index, uint8_t r, uint8_t g, uint8_t b);

/* Expand the framebuffer into the duty buffer and arm the DMA transfer.
 * Returns false (and does nothing) if a frame is still in flight. */
bool NeoPixel_Show(void);

/* True while a frame is being transmitted. */
bool NeoPixel_IsBusy(void);

#endif /* NEOPIXEL_H */
