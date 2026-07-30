#ifndef WS2812_H
#define WS2812_H

#include <stdint.h>
#include <stdbool.h>

// Maximum LED count. Each LED costs 9 bytes of DMA buffer + 3 bytes of pixel RAM.
#define WS2812_MAX_LEDS  96U

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} WS2812_Pixel_t;

// Call once after SYSTEM_Initialize(). Configures RA11, SPI3, and DMA0.
void WS2812_Init(uint16_t num_leds);

void WS2812_SetPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b);
void WS2812_SetAll(uint8_t r, uint8_t g, uint8_t b);
void WS2812_Clear(void);

// Returns true while a DMA transfer is in progress (CHEN self-clears in one-shot mode).
bool WS2812_IsBusy(void);

// Encode pixel buffer and transmit via SPI3+DMA0. Non-blocking; blocks only long
// enough to wait for the previous transfer to finish before starting the new one.
void WS2812_Show(void);

#endif // WS2812_H
