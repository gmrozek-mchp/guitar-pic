#include "fret_detect.h"

const fret_threshold_t fret_thresholds[FRET_COUNT] = {
    [FRET_GREEN]  = { .press = 2500, .release = 3000 },
    [FRET_RED]    = { .press = 2500, .release = 3000 },
    [FRET_YELLOW] = { .press = 2200, .release = 2400 },
    [FRET_BLUE]   = { .press = 2200, .release = 2500 },
    [FRET_ORANGE] = { .press = 2200, .release = 2500 },
};

static bool pressed[FRET_COUNT];
static uint8_t new_press_flags;
static uint8_t new_release_flags;

void fret_detect_init(void)
{
    for (uint8_t i = 0; i < FRET_COUNT; i++)
        pressed[i] = false;
    new_press_flags = 0;
    new_release_flags = 0;
}

void fret_detect_update(void)
{
    for (uint8_t i = 0; i < FRET_COUNT; i++) {
        uint16_t val = fret_scan_result((fret_channel_t)i);

        if (!pressed[i]) {
            if (val < fret_thresholds[i].press) {
                pressed[i] = true;
                new_press_flags |= (1U << i);
            }
        } else {
            if (val > fret_thresholds[i].release) {
                pressed[i] = false;
                new_release_flags |= (1U << i);
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

uint8_t fret_detect_new_releases(void)
{
    uint8_t flags = new_release_flags;
    new_release_flags = 0;
    return flags;
}
