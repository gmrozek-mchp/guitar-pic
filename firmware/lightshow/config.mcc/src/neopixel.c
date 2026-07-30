#include "neopixel.h"
#include <string.h>
#include "definitions.h"   /* TC0_*, DMAC_*, TC0_REGS, CMSIS */

/* TC0 8-bit NPWM: period = PER + 1 counts at 24 MHz / DIV1.
 * 30 counts = 1.25 us -> 800 kHz bit clock. High-time per bit selects 0/1. */
#define TC_PERIOD   (29u)   /* TOP; 30 counts */
#define T0H_TICKS   (8u)    /* ~0.33 us high  (WS2812B T0H 0.4 us +/-0.15) */
#define T1H_TICKS   (19u)   /* ~0.79 us high  (WS2812B T1H 0.8 us +/-0.15) */

#define BITS_PER_PIXEL   (24u)
#define FRAME_BITS       (NEOPIXEL_COUNT * BITS_PER_PIXEL)

/* Per-bit duty, both strands interleaved into one HWORD: low byte -> CCBUF0
 * (WO0/strand 0), high byte -> CCBUF1 (WO1/strand 1). The trailing entry is 0
 * so the last CCBUF write drives both lines low and holds them there (the
 * WS2812 reset/latch) once the transfer completes. */
static uint16_t s_duty[FRAME_BITS + 1u];

/* Framebuffer, R/G/B per pixel per strand. WS2812 wants G,R,B on the wire. */
static uint8_t s_fb[NEOPIXEL_STRANDS][NEOPIXEL_COUNT][3];

static const uint8_t GRB_ORDER[3] = { 1u, 0u, 2u };   /* fb index -> wire order */

void NeoPixel_Initialize(void)
{
    (void)memset(s_fb, 0, sizeof(s_fb));

    /* Own the bit-clock period here so a TC0 regeneration can't silently
     * retune it. TC0 is initialized-but-stopped by SYS_Initialize. */
    TC0_REGS->COUNT8.TC_PER = TC_PERIOD;
    TC0_REGS->COUNT8.TC_CC[0] = 0u;   /* both strands idle low until first frame */
    TC0_REGS->COUNT8.TC_CC[1] = 0u;

    TC0_CompareStart();
}

void NeoPixel_Clear(void)
{
    (void)memset(s_fb, 0, sizeof(s_fb));
}

void NeoPixel_SetPixel(uint8_t strand, uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if ((strand >= NEOPIXEL_STRANDS) || (index >= NEOPIXEL_COUNT))
    {
        return;
    }

    s_fb[strand][index][0] = r;
    s_fb[strand][index][1] = g;
    s_fb[strand][index][2] = b;
}

static void build_duty(void)
{
    uint16_t *dst = s_duty;

    for (uint16_t p = 0u; p < NEOPIXEL_COUNT; p++)
    {
        for (uint8_t c = 0u; c < 3u; c++)
        {
            uint8_t b0 = s_fb[0][p][GRB_ORDER[c]];
            uint8_t b1 = s_fb[1][p][GRB_ORDER[c]];

            for (uint8_t mask = 0x80u; mask != 0u; mask >>= 1)
            {
                uint8_t d0 = ((b0 & mask) != 0u) ? T1H_TICKS : T0H_TICKS;
                uint8_t d1 = ((b1 & mask) != 0u) ? T1H_TICKS : T0H_TICKS;
                *dst++ = (uint16_t)d0 | ((uint16_t)d1 << 8);
            }
        }
    }

    *dst = 0u;   /* drain: both lines low, held after the transfer ends */
}

bool NeoPixel_Show(void)
{
    if (DMAC_ChannelIsBusy(DMAC_CHANNEL_0))
    {
        return false;
    }

    build_duty();

    return DMAC_ChannelTransfer(DMAC_CHANNEL_0,
                                (const void *)s_duty,
                                (const void *)&TC0_REGS->COUNT8.TC_CCBUF[0],
                                sizeof(s_duty));
}

bool NeoPixel_IsBusy(void)
{
    return DMAC_ChannelIsBusy(DMAC_CHANNEL_0);
}
