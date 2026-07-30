// WS2812 LED string driver — SPI3 + DMA0, data output on RA11 (RP12, PPS 0x18 = SDO3).
//
// SPI clock = Fp(100 MHz) / (2*(BRG+1)) = 100/42 = 2.381 MHz -> 420 ns/bit
//
// Each WS2812 bit is encoded as 3 SPI bits (1.26 us total):
//   WS2812 '1'  ->  1 1 0  (840 ns HIGH, 420 ns LOW)   spec: 800±150 / 450±150 ns
//   WS2812 '0'  ->  1 0 0  (420 ns HIGH, 840 ns LOW)   spec: 400±150 / 850±150 ns
//
// 1 LED (GRB) = 3 WS2812 bytes = 9 SPI bytes.
// Reset = 40 zero bytes * 8 bits / 2.381 MHz = 134 us LOW (spec: >=50 us).
//
// This file is self-contained: it initialises SPI3, DMA0, and pin RA11 directly
// inside WS2812_Init() with no dependency on MCC-generated spi3.c or dma.c.

#include "ws2812.h"
#include <xc.h>
#include <string.h>

#define SPI_BYTES_PER_LED   9U
#define RESET_BYTES         40U
#define BUF_SIZE            ((WS2812_MAX_LEDS * SPI_BYTES_PER_LED) + RESET_BYTES)

// DMA source buffer in data RAM (DMA window covers this range after DMALOW=0x0).
static uint8_t spi_buf[BUF_SIZE];
static WS2812_Pixel_t pixels[WS2812_MAX_LEDS];
static uint16_t num_leds;

// One WS2812 byte -> three SPI bytes.  Input bit 7 first; 1->0b110, 0->0b100.
static void encode_byte(uint8_t val, uint8_t *out)
{
    uint32_t w = 0;
    uint8_t bit;
    for (bit = 0; bit < 8U; bit++) {
        w <<= 3;
        w |= ((val & 0x80U) != 0U) ? 0x6U : 0x4U;
        val <<= 1;
    }
    out[0] = (uint8_t)(w >> 16);
    out[1] = (uint8_t)(w >> 8);
    out[2] = (uint8_t)(w);
}

void WS2812_Init(uint16_t leds)
{
    if (leds > WS2812_MAX_LEDS) leds = WS2812_MAX_LEDS;
    num_leds = leds;

    (void)memset(pixels, 0, sizeof(pixels));
    (void)memset(spi_buf, 0, sizeof(spi_buf));  // reset bytes stay zero permanently

    // ---- RA11: digital output -> SPI3 SDO3 via PPS ----
    ANSELAbits.ANSELA11 = 0;      // digital (default is analog)
    TRISAbits.TRISA11   = 0;      // output
    RPOR2bits.RP12R     = 0x18U;  // RP12 (RA11) -> SPI3 SDO3

    // ---- SPI3: 8-bit, Mode 1 (CKP=0 CKE=1), ENHBUF, Host, 2.381 MHz ----
    // Write to SPI3CON1 with ON=0 first so configuration bits are writable.
    SPI3CON2 = 0x0;
    SPI3STAT = 0x28;
    SPI3BUF  = 0x0;
    SPI3BRG  = 0x14;   // 100 MHz / (2*21) = 2.381 MHz
    SPI3URDT = 0x0;
    SPI3CON1 = 0x21;             // ENHBUF | MSTEN | CKE; ON=0
    SPI3CON1bits.ON = 1;         // enable SPI3
    // SPITBEN=1: SPI3TXIF fires when TX FIFO drains empty -> DMA trigger source.
    // Without this, SPI3TXIF never asserts and DMA never advances past byte 0.
    SPI3IMSKbits.SPITBEN = 1;

    // ---- DMA global ----
    DMACON  = 0x8000UL;   // ON=1, fixed priority
    DMABUF  = 0x0UL;
    // MCC default DMALOW=0x4000 (RAM start). SPI3BUF is at 0x184C in SFR space,
    // below RAM. Extend the window to 0x0 so DMA can write to SPI3BUF.
    DMALOW  = 0x0000UL;
    DMAHIGH = 0x7CFFFFUL;

    // ---- DMA0: one-shot, source-increment, destination-fixed, byte size ----
    DMA0CH  = 0x4000UL;           // TRMODE=One-Shot, SAMODE=Increment, SIZE=Byte
    DMA0SEL = 0x000BUL;           // trigger source = SPI3 TX (SPI3TXIF)
    DMA0DST = (unsigned long)&SPI3BUF;  // fixed destination; set once here
    DMA0SRC = 0UL;
    DMA0CNT = 0UL;
}

void WS2812_SetPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index < num_leds) {
        pixels[index].r = r;
        pixels[index].g = g;
        pixels[index].b = b;
    }
}

void WS2812_SetAll(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t i;
    for (i = 0; i < num_leds; i++) {
        pixels[i].r = r;
        pixels[i].g = g;
        pixels[i].b = b;
    }
}

void WS2812_Clear(void)
{
    WS2812_SetAll(0, 0, 0);
}

bool WS2812_IsBusy(void)
{
    // Polled path is synchronous — never busy after Show() returns.
    return false;
}

void WS2812_Show(void)
{
    uint16_t i, idx, total, tx;

    // Encode pixel data into SPI buffer (GRB order required by WS2812 protocol).
    idx = 0;
    for (i = 0; i < num_leds; i++) {
        encode_byte(pixels[i].g, &spi_buf[idx]);  idx += 3U;
        encode_byte(pixels[i].r, &spi_buf[idx]);  idx += 3U;
        encode_byte(pixels[i].b, &spi_buf[idx]);  idx += 3U;
    }
    // Bytes from idx to BUF_SIZE-1 are permanently 0x00 (reset signal).

    total = (uint16_t)((num_leds * SPI_BYTES_PER_LED) + RESET_BYTES);

    SPI3STATbits.SPIROV = 0;

    // Polled transmit: write one byte at a time, drain RX to prevent overflow.
    for (tx = 0; tx < total; tx++) {
        while (SPI3STATbits.SPITBF) {}
        SPI3BUF = (uint32_t)spi_buf[tx];
        if (!SPI3STATbits.SPIRBE) (void)SPI3BUF;
    }
    // Wait for last byte to finish shifting out of the shift register.
    while (!SPI3STATbits.SRMT) {}
}
