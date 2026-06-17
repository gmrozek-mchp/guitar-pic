#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "definitions.h"
#include "fret_scan.h"
#include "data_stream.h"
#include "fretboard_config.h"
#include "t1s_detector.h"
#include "cli.h"
#include "model_infer.h"

#if MODEL_INFER_STREAMING
#include "model_infer_stream.h"
#define ADC_Q_LEN 16   /* ISR->main sample queue; streaming must consume every sample */
#endif

/* SW0 (PB03) momentary button arms guitar actuation on each press; LED0 (PB02)
 * lit while armed. Boots disarmed: the command sent to the guitar is 0 (released),
 * LED off, until pressed. (Interim manual "active detector" gate until marvin
 * coordinates active-detector/active-guitar selection.)
 *
 * Board polarity: the MCC pin config (PB03 pull-up, PB02 boots high) implies SW0
 * and LED0 are both active-low. If LED0 is inverted, set LED0_ACTIVE_LOW to 0. */
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

/* Latest inference (main loop writes, ISR reads). Single byte → atomic on M0+. */
static volatile uint8_t s_latest_cmd;

/* Gated command driven this tick (ISR writes, main loop forwards to the guitar).
 * Also the applied_mask reported in the data frame. */
static volatile uint8_t s_current_cmd;

static bool    s_armed;               /* actuation enabled (SW0 toggle) */
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

static bool actuation_armed(void)
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
            s_armed = !s_armed;
            if (s_armed) { LED0_ON(); } else { LED0_OFF(); }
        }
    }
    return s_armed;
}

void Callback_TC0 (TC_TIMER_STATUS status, uintptr_t context)
{
    fret_scan_all();

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

    /* The heavy inference runs in the main loop; here we just gate the most
     * recent result and stream the (ADC + driven mask) frame to the coordinator.
     * The main loop forwards s_current_cmd to the guitar. */
    uint8_t cmd = actuation_armed() ? s_latest_cmd : 0u;
    s_current_cmd = cmd;
    data_stream_send(cmd);
}

int main(void)
{
    SYS_Initialize(NULL);

    fret_scan_init();
    data_stream_init();
    T1SDetector_Initialize();
    CLI_Initialize();
#if MODEL_INFER_STREAMING
    model_infer_stream_init();
#else
    model_infer_init();
#endif
    LED0_OFF();   /* boot disarmed: LED off */

    TC0_TimerCallbackRegister( Callback_TC0, NULL );
    TC0_TimerStart();

    while (true) {
#if MODEL_INFER_STREAMING
        /* Drain the sample queue: one streaming step per sample, in order. */
        while (s_q_rd != s_q_wr)
        {
            uint16_t ri = s_q_rd;
            uint16_t s[FRET_COUNT];
            for (int c = 0; c < FRET_COUNT; c++) { s[c] = s_adc_q[ri % ADC_Q_LEN][c]; }
            s_latest_cmd = model_infer_stream_step(s);
            s_q_rd = (uint16_t)(ri + 1u);
        }
#else
        s_latest_cmd = model_infer_run();
#endif
        /* Forward the gated command to the guitar (edge-triggered + refreshed) and
         * service the MAC-PHY / data-frame flush / heartbeat, then the CLI. */
        T1SDetector_SetCommand(s_current_cmd);
        T1SDetector_Tasks();
        CLI_Tasks();
    }

    return EXIT_FAILURE;
}
