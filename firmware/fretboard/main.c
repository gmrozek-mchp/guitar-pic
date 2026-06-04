#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "definitions.h"
#include "fret_scan.h"
#include "cmd_receive.h"
#include "data_stream.h"
#include "fretboard_config.h"
#include "model_infer.h"

#if FRETBOARD_MODE == MODEL_DRIVEN && MODEL_INFER_STREAMING
#include "model_infer_stream.h"
#define ADC_Q_LEN 16   /* ISR->main sample queue; streaming must consume every sample */
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

/* Total inferences run in the main loop; streamed each tick so the host can
 * measure the real inference rate (vs the 240 Hz sample rate). 32-bit aligned
 * → atomic single-word access on the M0+. */
static volatile uint32_t s_infer_count;

static bool    s_model_enabled;
static bool    s_sw_pressed;          /* debounced button state */
static uint8_t s_sw_stable;           /* consecutive reads pushing toward a flip */

#if MODEL_INFER_STREAMING
/* SPSC sample queue: the ISR pushes one scan/tick, the main loop steps the
 * stateful streaming model once per sample (it must consume every sample in
 * order). Stays near-empty while inference keeps up (>240 Hz). */
static volatile uint16_t s_adc_q[ADC_Q_LEN][FRET_COUNT];
static volatile uint16_t s_q_wr;      /* ISR-advanced write index */
static volatile uint16_t s_q_rd;      /* main-advanced read index */
#endif

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
#if MODEL_INFER_STREAMING
    uint16_t wi = s_q_wr;
    for (int c = 0; c < FRET_COUNT; c++)
    {
        s_adc_q[wi % ADC_Q_LEN][c] = fret_scan_result((fret_channel_t)c);
    }
    s_q_wr = (uint16_t)(wi + 1u);   /* publish the sample to the main loop */
#else
    uint16_t scan[FRET_COUNT] = {
        fret_scan_result(FRET_GREEN),
        fret_scan_result(FRET_RED),
        fret_scan_result(FRET_YELLOW),
        fret_scan_result(FRET_BLUE),
        fret_scan_result(FRET_ORANGE),
    };
    model_infer_push(scan);   /* sample the window at a clean 240 Hz */
#endif
    /* Apply the most recent inference result; the heavy inference runs in the
     * main loop, not here, so this ISR stays short. */
    cmd_receive_apply_mask(model_control_enabled() ? s_latest_cmd : 0u);
    data_stream_send_model(s_infer_count);   /* carries applied_mask + inference count */
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
#if MODEL_INFER_STREAMING
    model_infer_stream_init();
#else
    model_infer_init();
#endif
    LED0_OFF();   /* boot disabled: LED off */
#endif

    TC0_TimerCallbackRegister( Callback_TC0, NULL );
    TC0_TimerStart();

    while (true) {
#if FRETBOARD_MODE == MODEL_DRIVEN
#if MODEL_INFER_STREAMING
        /* Drain the sample queue: one streaming step per sample, in order. */
        while (s_q_rd != s_q_wr)
        {
            uint16_t ri = s_q_rd;
            uint16_t s[FRET_COUNT];
            for (int c = 0; c < FRET_COUNT; c++) { s[c] = s_adc_q[ri % ADC_Q_LEN][c]; }
            s_latest_cmd = model_infer_stream_step(s);
            s_q_rd = (uint16_t)(ri + 1u);
            s_infer_count++;
        }
#else
        /* Best-effort recompute inference, decoupled from the 240 Hz sampling/apply
         * in the ISR. Publishes the latest command for the ISR to apply. */
        s_latest_cmd = model_infer_run();
        s_infer_count++;
#endif
#endif
    }

    return EXIT_FAILURE;
}
