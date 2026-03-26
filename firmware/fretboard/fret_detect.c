#include "fret_detect.h"

static bool pressed[FRET_COUNT];
static uint8_t new_press_flags;

void fret_detect_init(void)
{
    for (uint8_t i = 0; i < FRET_COUNT; i++)
        pressed[i] = false;
    new_press_flags = 0;
}

void fret_detect_update(void)
{
    for (uint8_t i = 0; i < FRET_COUNT; i++) {
        uint16_t val = fret_scan_result((fret_channel_t)i);

        if (!pressed[i]) {
            if (val < FRET_PRESS_THRESHOLD) {
                pressed[i] = true;
                new_press_flags |= (1U << i);
            }
        } else {
            if (val > FRET_RELEASE_THRESHOLD) {
                pressed[i] = false;
            }
        }
    }
}

bool fret_is_pressed(fret_channel_t ch)
{
    return pressed[ch];
}

uint8_t fret_detect_new_presses(void)
{
    uint8_t flags = new_press_flags;
    new_press_flags = 0;
    return flags;
}
