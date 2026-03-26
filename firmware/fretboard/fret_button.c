#include "fret_button.h"
#include "fret_detect.h"
#include "definitions.h"

#define SW0_DEBOUNCE_SWEEPS  30

/* ---- event queue ---- */

typedef enum {
    EVT_PRESS,
    EVT_RELEASE,
    EVT_STRUM,
} event_type_t;

typedef struct {
    uint32_t fire_at;
    uint8_t  type;
    uint8_t  channel;
} event_t;

#define EVQ_CAP  32
static event_t evq[EVQ_CAP];
static uint8_t evq_count;

static bool evq_push(uint32_t fire_at, event_type_t type, uint8_t channel)
{
    if (evq_count >= EVQ_CAP)
        return false;
    evq[evq_count++] = (event_t){ fire_at, type, channel };
    return true;
}

/* ---- chord window ---- */

static bool     chord_open;
static uint32_t chord_start_sweep;
static uint8_t  chord_channels;       /* bitmask */

/* ---- strum state ---- */

static uint16_t strum_pulse_remaining;
static bool     strum_direction;

/* ---- enable / SW0 ---- */

static bool    enabled;
static bool    sw0_prev;
static uint8_t sw0_stable_count;

/* ---- button pin helpers ---- */

static void button_assert(uint8_t i)
{
    switch ((fret_channel_t)i) {
    case FRET_GREEN:  BUTTON_GREEN_Clear();  BUTTON_GREEN_OutputEnable();  break;
    case FRET_RED:    BUTTON_RED_Clear();    BUTTON_RED_OutputEnable();    break;
    case FRET_YELLOW: BUTTON_YELLOW_Clear(); BUTTON_YELLOW_OutputEnable(); break;
    case FRET_BLUE:   BUTTON_BLUE_Clear();   BUTTON_BLUE_OutputEnable();   break;
    case FRET_ORANGE: BUTTON_ORANGE_Clear(); BUTTON_ORANGE_OutputEnable(); break;
    default: break;
    }
}

static void button_release(uint8_t i)
{
    switch ((fret_channel_t)i) {
    case FRET_GREEN:  BUTTON_GREEN_InputEnable();  break;
    case FRET_RED:    BUTTON_RED_InputEnable();    break;
    case FRET_YELLOW: BUTTON_YELLOW_InputEnable(); break;
    case FRET_BLUE:   BUTTON_BLUE_InputEnable();   break;
    case FRET_ORANGE: BUTTON_ORANGE_InputEnable(); break;
    default: break;
    }
}

static void strum_release(void)
{
    BUTTON_STRUM_DOWN_InputEnable();
    BUTTON_STRUM_UP_InputEnable();
    strum_pulse_remaining = 0;
}

static void strum_trigger(void)
{
    strum_release();
    if (strum_direction) {
        BUTTON_STRUM_UP_Clear();
        BUTTON_STRUM_UP_OutputEnable();
    } else {
        BUTTON_STRUM_DOWN_Clear();
        BUTTON_STRUM_DOWN_OutputEnable();
    }
    strum_direction = !strum_direction;
    strum_pulse_remaining = STRUM_PULSE_SWEEPS;
}

static void release_all(void)
{
    for (uint8_t i = 0; i < FRET_COUNT; i++)
        button_release(i);
    strum_release();
    evq_count = 0;
    chord_open = false;
    chord_channels = 0;
}

/* ---- enable / disable ---- */

static void set_enabled(bool en)
{
    enabled = en;
    if (en)
        LED0_Clear();
    else {
        LED0_Set();
        release_all();
    }
}

static void sw0_poll(void)
{
    bool raw = !SW0_Get();
    if (raw == sw0_prev) {
        sw0_stable_count = 0;
        return;
    }
    sw0_stable_count++;
    if (sw0_stable_count >= SW0_DEBOUNCE_SWEEPS) {
        sw0_prev = raw;
        sw0_stable_count = 0;
        if (raw)
            set_enabled(!enabled);
    }
}

/* ---- chord window management ---- */

static bool prev_detected[FRET_COUNT];

static void chord_commit(uint32_t now)
{
    uint32_t press_at = chord_start_sweep + STRUM_DELAY_SWEEPS - FRET_EARLY_SWEEPS;
    uint32_t strum_at = chord_start_sweep + STRUM_DELAY_SWEEPS;

    if (press_at <= now) press_at = now;
    if (strum_at <= now) strum_at = now;

    for (uint8_t i = 0; i < FRET_COUNT; i++) {
        if (chord_channels & (1U << i))
            evq_push(press_at, EVT_PRESS, i);
    }

    evq_push(strum_at, EVT_STRUM, 0);

    chord_open = false;
    chord_channels = 0;
}

/* ---- main update ---- */

void fret_button_init(void)
{
    evq_count = 0;
    chord_open = false;
    chord_channels = 0;
    strum_pulse_remaining = 0;
    strum_direction = false;
    sw0_prev = !SW0_Get();
    sw0_stable_count = 0;

    for (uint8_t i = 0; i < FRET_COUNT; i++) {
        prev_detected[i] = false;
        button_release(i);
    }
    strum_release();
    set_enabled(false);
}

void fret_button_update(void)
{
    sw0_poll();

    if (!enabled)
        return;

    uint32_t now = fret_scan_sweep_count();

    /* Detect edges and build chord / schedule releases */
    for (uint8_t i = 0; i < FRET_COUNT; i++) {
        bool detected = fret_is_pressed((fret_channel_t)i);

        if (detected && !prev_detected[i]) {
            if (!chord_open) {
                chord_open = true;
                chord_start_sweep = now;
                chord_channels = 0;
            }
            chord_channels |= (1U << i);
        }

        if (!detected && prev_detected[i]) {
            uint32_t release_at = now + STRUM_DELAY_SWEEPS;
            evq_push(release_at, EVT_RELEASE, i);
        }

        prev_detected[i] = detected;
    }

    /* Close chord window when it expires */
    if (chord_open && (now - chord_start_sweep) >= CHORD_WINDOW_SWEEPS)
        chord_commit(now);

    /* Process due events */
    uint8_t j = 0;
    while (j < evq_count) {
        if (evq[j].fire_at <= now) {
            switch (evq[j].type) {
            case EVT_PRESS:   button_assert(evq[j].channel);  break;
            case EVT_RELEASE: button_release(evq[j].channel); break;
            case EVT_STRUM:   strum_trigger();                 break;
            }
            evq[j] = evq[--evq_count];
        } else {
            j++;
        }
    }

    /* Strum pulse auto-release */
    if (strum_pulse_remaining > 0) {
        strum_pulse_remaining--;
        if (strum_pulse_remaining == 0)
            strum_release();
    }
}

bool fret_button_is_enabled(void)
{
    return enabled;
}
