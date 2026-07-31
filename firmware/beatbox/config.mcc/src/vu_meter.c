#include "vu_meter.h"

#include <stddef.h>

#include "../mcc_generated_files/system/pins.h"

/* Full-scale envelope. raw_env is 0-10000 absolute; a loud line-in peaks around
 * 4000-5000, so 6000 fills most of the bar at full volume while keeping a little
 * headroom. Lower it to make the meter livelier, raise it for more headroom. */
#define VU_FULL_SCALE   6000u
/* Envelope units the level sheds per frame (~23.4 Hz) once the signal falls off
 * -- sets how fast the bar drops back after a peak. */
#define VU_DECAY        700u
#define VU_SEGMENTS     8u

static uint16_t s_level;

static void set_led(uint8_t i, bool on)
{
    switch (i)
    {
        case 0: if (on) { LED0_SetHigh(); } else { LED0_SetLow(); } break;
        case 1: if (on) { LED1_SetHigh(); } else { LED1_SetLow(); } break;
        case 2: if (on) { LED2_SetHigh(); } else { LED2_SetLow(); } break;
        case 3: if (on) { LED3_SetHigh(); } else { LED3_SetLow(); } break;
        case 4: if (on) { LED4_SetHigh(); } else { LED4_SetLow(); } break;
        case 5: if (on) { LED5_SetHigh(); } else { LED5_SetLow(); } break;
        case 6: if (on) { LED6_SetHigh(); } else { LED6_SetLow(); } break;
        case 7: if (on) { LED7_SetHigh(); } else { LED7_SetLow(); } break;
        default: break;
    }
}

/* Light the bottom `segments` LEDs. The bar bottom is LED7, so bar segment k
 * (0 = bottom) is LED index 7 - k; LED i is lit while (7 - i) < segments. */
static void show_bar(uint8_t segments)
{
    for (uint8_t i = 0u; i < VU_SEGMENTS; i++)
    {
        set_led(i, ((VU_SEGMENTS - 1u - i) < segments));
    }
}

void VU_Initialize(void)
{
    s_level = 0u;
    show_bar(0u);
}

void VU_Update(const BeatFrame *f)
{
    if (f == NULL)
    {
        return;
    }

    if (f->raw_env > s_level)
    {
        s_level = f->raw_env;                    /* instant attack */
    }
    else if (s_level > VU_DECAY)
    {
        s_level = (uint16_t)(s_level - VU_DECAY);
    }
    else
    {
        s_level = 0u;
    }

    uint16_t clamped = (s_level > VU_FULL_SCALE) ? VU_FULL_SCALE : s_level;
    uint8_t  segments = (uint8_t)(((uint32_t)clamped * VU_SEGMENTS) / VU_FULL_SCALE);

    show_bar(segments);
}

void VU_Off(void)
{
    s_level = 0u;
    show_bar(0u);
}
