#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "definitions.h"
#include "fret_scan.h"
#include "cmd_receive.h"
#include "data_stream.h"
#include "model_infer.h"

/* Build-time operating mode.
 *   MARVIN_DRIVEN — apply the command byte streamed from marvin over SERCOM1.
 *   MODEL_DRIVEN  — run the on-device model and drive the outputs from its
 *                   output (standalone, marvin disconnected). */
#define MARVIN_DRIVEN 0
#define MODEL_DRIVEN  1
#ifndef FRETBOARD_MODE
#define FRETBOARD_MODE MODEL_DRIVEN
#endif

#if FRETBOARD_MODE == MODEL_DRIVEN
/* SW0 (PB03) momentary button toggles model control on each press; LED0 (PB02)
 * lit while enabled. Boots disabled: outputs released, LED off, until pressed.
 *
 * Board polarity: the MCC pin config (PB03 pull-up, PB02 boots high) implies
 * SW0 and LED0 are both active-low. If at power-up the model auto-enables and
 * thrashes before you touch SW0, the pin idles low — set SW0_ACTIVE_LOW to 0.
 * If LED0 is inverted, set LED0_ACTIVE_LOW to 0. */
#define SW0_ACTIVE_LOW          1
#define LED0_ACTIVE_LOW         1
#define ENABLE_DEBOUNCE_TICKS   12u   /* ~50 ms at 240 Hz */

#if SW0_ACTIVE_LOW
#define SW0_PRESSED()   (SW0_Get() == 0U)
#else
#define SW0_PRESSED()   (SW0_Get() != 0U)
#endif
#if LED0_ACTIVE_LOW
#define LED0_ON()       LED0_Clear()
#define LED0_OFF()      LED0_Set()
#else
#define LED0_ON()       LED0_Set()
#define LED0_OFF()      LED0_Clear()
#endif

/* Latest command from the (main-loop) inference, applied by the TC0 ISR. A
 * single byte, so reads/writes are atomic on the M0+ — inference runs outside
 * the ISR so it can't starve the 240 Hz sampling or the serial TX interrupt. */
static volatile uint8_t s_latest_cmd;

static bool    s_model_enabled;
static bool    s_sw_pressed;          /* debounced button state */
static uint8_t s_sw_stable;           /* consecutive reads pushing toward a flip */

static bool model_control_enabled(void)
{
    bool raw = SW0_PRESSED();
    if (raw == s_sw_pressed)
    {
        s_sw_stable = 0u;
    }
    else if (++s_sw_stable >= ENABLE_DEBOUNCE_TICKS)
    {
        s_sw_pressed = raw;
        s_sw_stable = 0u;
        if (s_sw_pressed)             /* toggle on a confirmed press edge */
        {
            s_model_enabled = !s_model_enabled;
            if (s_model_enabled) { LED0_ON(); } else { LED0_OFF(); }
        }
    }
    return s_model_enabled;
}
#endif

void Callback_TC0 (TC_TIMER_STATUS status, uintptr_t context)
{
    fret_scan_all();

#if FRETBOARD_MODE == MODEL_DRIVEN
    uint16_t scan[FRET_COUNT] = {
        fret_scan_result(FRET_GREEN),
        fret_scan_result(FRET_RED),
        fret_scan_result(FRET_YELLOW),
        fret_scan_result(FRET_BLUE),
        fret_scan_result(FRET_ORANGE),
    };
    model_infer_push(scan);   /* sample the window at a clean 240 Hz */
    /* Apply the most recent inference result; the heavy model_infer_run() runs
     * in the main loop, not here, so this ISR stays short. */
    cmd_receive_apply_mask(model_control_enabled() ? s_latest_cmd : 0u);
    data_stream_send();   /* applied_mask carries the model's command (0 if disabled) */
#else
    data_stream_send();
    cmd_receive_update();
#endif
}

int main(void)
{
    SYS_Initialize(NULL);

    fret_scan_init();
    cmd_receive_init();
    data_stream_init();
#if FRETBOARD_MODE == MODEL_DRIVEN
    model_infer_init();
    LED0_OFF();   /* boot disabled: LED off */
#endif

    TC0_TimerCallbackRegister( Callback_TC0, NULL );
    TC0_TimerStart();

    while (true) {
#if FRETBOARD_MODE == MODEL_DRIVEN
        /* Best-effort inference, decoupled from the 240 Hz sampling/apply in the
         * ISR. Publishes the latest command for the ISR to apply. */
        s_latest_cmd = model_infer_run();
#endif
    }

    return EXIT_FAILURE;
}
