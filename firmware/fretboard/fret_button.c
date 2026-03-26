#include "fret_button.h"
#include "fret_detect.h"
#include "definitions.h"

#define NO_PENDING  0xFFFFU
#define SW0_DEBOUNCE_SWEEPS  30   /* ~32ms at 950 Hz */

typedef struct {
    bool     prev_detected;
    bool     output_active;
    uint16_t press_countdown;
    uint16_t release_countdown;
} button_ch_t;

static button_ch_t ch[FRET_COUNT];
static bool enabled;
static bool sw0_prev;
static uint8_t sw0_stable_count;

static uint16_t strum_countdown;          /* single global strum delay */
static uint16_t strum_pulse_remaining;
static bool     strum_direction;          /* false = down, true = up */

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
    ch[i].output_active = true;
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
    ch[i].output_active = false;
}

static void strum_release(void)
{
    BUTTON_STRUM_DOWN_InputEnable();
    BUTTON_STRUM_UP_InputEnable();
    strum_pulse_remaining = 0;
    BUTTON_ORANGE_InputEnable();    // use orange button as visible indicator of strum        
}

static void strum_trigger(void)
{
    strum_release();
    if (strum_direction) {
        BUTTON_STRUM_UP_Clear();
        BUTTON_STRUM_UP_OutputEnable();
        BUTTON_ORANGE_OutputEnable();   // use orange button as visible indicator of strum        
    } else {
        BUTTON_STRUM_DOWN_Clear();
        BUTTON_STRUM_DOWN_OutputEnable();
        BUTTON_ORANGE_OutputEnable();   // use orange button as visible indicator of strum        
    }
    strum_direction = !strum_direction;
    strum_pulse_remaining = STRUM_PULSE_SWEEPS;
}

static void release_all(void)
{
    for (uint8_t i = 0; i < FRET_COUNT; i++) {
        button_release(i);
        ch[i].prev_detected     = false;
        ch[i].press_countdown   = NO_PENDING;
        ch[i].release_countdown = NO_PENDING;
    }
    strum_countdown = NO_PENDING;
    strum_release();
}

static void set_enabled(bool en)
{
    enabled = en;
    if (en)
        LED0_Clear();       /* active-low: on */
    else {
        LED0_Set();         /* active-low: off */
        release_all();
    }
}

static void sw0_poll(void)
{
    bool raw = !SW0_Get();  /* active-low → true when pressed */

    if (raw == sw0_prev) {
        sw0_stable_count = 0;
        return;
    }

    sw0_stable_count++;
    if (sw0_stable_count >= SW0_DEBOUNCE_SWEEPS) {
        sw0_prev = raw;
        sw0_stable_count = 0;
        if (raw)            /* falling edge (press) → toggle */
            set_enabled(!enabled);
    }
}

void fret_button_init(void)
{
    for (uint8_t i = 0; i < FRET_COUNT; i++) {
        ch[i].prev_detected     = false;
        ch[i].output_active     = false;
        ch[i].press_countdown   = NO_PENDING;
        ch[i].release_countdown = NO_PENDING;
        button_release(i);
    }
    sw0_prev = !SW0_Get();
    sw0_stable_count = 0;
    strum_countdown = NO_PENDING;
    strum_pulse_remaining = 0;
    strum_direction = false;
    strum_release();
    set_enabled(false);
}

void fret_button_update(void)
{
    sw0_poll();

    if (!enabled)
        return;

    for (uint8_t i = 0; i < FRET_COUNT; i++) {
        bool detected = fret_is_pressed((fret_channel_t)i);

        if (detected && !ch[i].prev_detected) {
            ch[i].press_countdown = STRUM_DELAY_SWEEPS - FRET_EARLY_SWEEPS;
            if (strum_countdown == NO_PENDING)
                strum_countdown = STRUM_DELAY_SWEEPS;
        }

        if (!detected && ch[i].prev_detected)
            ch[i].release_countdown = STRUM_DELAY_SWEEPS;

        ch[i].prev_detected = detected;

        if (ch[i].press_countdown != NO_PENDING) {
            if (ch[i].press_countdown == 0) {
                button_assert(i);
                ch[i].press_countdown = NO_PENDING;
            } else {
                ch[i].press_countdown--;
            }
        }

        if (ch[i].release_countdown != NO_PENDING) {
            if (ch[i].release_countdown == 0) {
                button_release(i);
                ch[i].release_countdown = NO_PENDING;
            } else {
                ch[i].release_countdown--;
            }
        }
    }

    if (strum_countdown != NO_PENDING) {
        if (strum_countdown == 0) {
            strum_trigger();
            strum_countdown = NO_PENDING;
        } else {
            strum_countdown--;
        }
    }

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
