/*
    Beat Detection — Phase-Locked Bob Cycle

    A free-running oscillator drives the LED brightness and head position.
    Default period: 2 seconds (A→B→A = one full cycle).
    Beat detection measures inter-beat intervals and smoothly adjusts
    the period to match the detected tempo.

    LED: 0.6% duty at position A, 25% duty at position B.
    GUI: receives phase (0-1000) each frame, drives Beavis in sync.
*/
#include "mcc_generated_files/system/system.h"
#include "beat_detect.h"
#include "uart_debug.h"
#include "rgb_led.h"
#include "servo.h"
#include "nod_engine.h"
#include "ws2812.h"

#define LED_BEAT  LATDbits.LATD1

// Beat detection
#define BEAT_HIST_LEN   4
#define BEAT_COOLDOWN   8
#define BEAT_DELTA_THR  250
#define NOISE_GATE_THR  30      // raw envelope threshold (0-10000); raised to reject creeping noise
#define NOISE_GATE_BYPASS_DELTA 250  // flux delta strong enough to bypass noise gate

// Phase oscillator
#define DEFAULT_PERIOD_FRAMES   47      // 2 seconds at ~23.4 fps (47 frames = 2.0s)
#define MIN_PERIOD_FRAMES       8       // ~340ms min (~176 BPM max)
#define MAX_PERIOD_FRAMES       70      // ~3 seconds max (~40 BPM min)

// LED duty range
#define DUTY_A      20          // 0.6% at position A (neutral)
#define DUTY_B      781         // 25% at position B (peak)

// WS2812 LED count — change this one value when the strip length changes.
#define NUM_LEDS    70U

// ---- WS2812 beat-reactive effects (~23.4 Hz frame rate) ----

// Hue (0-767) + brightness (0-255) → RGB. 0=red, 256=green, 512=blue.
static void hue_rgb(uint16_t h, uint8_t val, uint8_t *r, uint8_t *g, uint8_t *b)
{
    h %= 768U;
    if (h < 256U)      { *r = (uint8_t)(255U - h); *g = (uint8_t)h;        *b = 0U; }
    else if (h < 512U) { *r = 0U; *g = (uint8_t)(511U - h); *b = (uint8_t)(h - 256U); }
    else               { *r = (uint8_t)(h - 512U); *g = 0U; *b = (uint8_t)(767U - h); }
    *r = (uint8_t)((uint16_t)*r * val / 255U);
    *g = (uint8_t)((uint16_t)*g * val / 255U);
    *b = (uint8_t)((uint16_t)*b * val / 255U);
}

// Effect 0: Beat Flash — beat fires a bright flash; bass flux pre-lights the strip
// as energy builds toward the hit. Hue drifts slowly through the full color wheel
// (~2 steps/frame = full cycle in ~16 s) so the color of each pulse changes over time.
static void ef_pulse(uint8_t bass_beat, uint8_t full_beat, uint16_t flux_bass)
{
    static uint16_t bright = 0U;
    static uint16_t hue    = 0U;
    uint16_t glow, total;
    uint8_t r, g, b;
    if (bass_beat > 0U || full_beat > 0U) bright = 255U;
    else if (bright > 0U) bright = (bright * 205U) >> 8U;
    glow  = (uint16_t)((uint32_t)flux_bass * 40UL / 1000UL);
    total = bright + glow;
    if (total > 255U) total = 255U;
    hue = (hue + 2U) % 768U;
    hue_rgb(hue, (uint8_t)total, &r, &g, &b);
    WS2812_SetAll(r, g, b);
}

// Effect 1: Dual Comet — two comets driven by the phase oscillator converge at the
// center on each nod and diverge back. Warm (yellow-white) at phase→95; cool
// (blue-white) at the mirror position. Beat fires a background flash.
static void ef_dual_comet(uint16_t phase, uint8_t bass_beat, uint16_t raw_env)
{
    static uint16_t beat_flash = 0U;
    uint16_t i, pos_a, pos_b;
    uint8_t  t, bg;

    if (bass_beat > 0U) beat_flash = 200U;
    else if (beat_flash > 0U) beat_flash = (beat_flash * 210U) >> 8U;

    bg = (uint8_t)((uint32_t)raw_env * 5UL / 10000UL + (beat_flash >> 3U));
    for (i = 0U; i < NUM_LEDS; i++) WS2812_SetPixel(i, bg, (uint8_t)(bg >> 1U), 0U);

    pos_a = (uint16_t)((uint32_t)phase * (NUM_LEDS - 1U) / 1000UL);
    for (i = 0U; i < 8U; i++) {
        if (pos_a >= i) {
            t = (uint8_t)(200U * (8U - i) / 8U);
            WS2812_SetPixel(pos_a - i, t, (uint8_t)(t * 3U / 4U), (uint8_t)(t / 4U));
        }
    }
    WS2812_SetPixel(pos_a, 255U, 220U, 80U);   // warm yellow-white head

    pos_b = (NUM_LEDS - 1U) - pos_a;
    for (i = 0U; i < 8U; i++) {
        if (pos_b + i < NUM_LEDS) {
            t = (uint8_t)(200U * (8U - i) / 8U);
            WS2812_SetPixel(pos_b + i, (uint8_t)(t / 4U), (uint8_t)(t * 3U / 4U), t);
        }
    }
    WS2812_SetPixel(pos_b, 80U, 220U, 255U);   // cool blue-white head

}

// Effect 2: Split Energy — bass flux fills left half (warm amber, center→left);
// treble flux fills right half (cool blue, center→right). Beat hits flash each side.
static void ef_split(uint16_t flux_bass, uint16_t flux_full,
                      uint8_t bass_beat, uint8_t full_beat)
{
    static uint16_t bf = 0U, tf = 0U;
    uint16_t i, total, flux_treble;
    uint8_t  fill_b, fill_t, br;

    if (bass_beat > 0U) bf = 240U; else if (bf > 0U) bf = (bf * 210U) >> 8U;
    if (full_beat > 0U) tf = 240U; else if (tf > 0U) tf = (tf * 210U) >> 8U;

    flux_treble = (flux_full > flux_bass) ? (flux_full - flux_bass) : 0U;
    fill_b = (uint8_t)((uint32_t)flux_bass   * (NUM_LEDS / 2U) / 1000UL);
    fill_t = (uint8_t)((uint32_t)flux_treble * (NUM_LEDS / 2U) / 1000UL);

    for (i = 0U; i < (NUM_LEDS / 2U); i++) {
        // Warm amber: center-left LED, fills toward LED 0
        br = (i < fill_b) ? (uint8_t)(15U + (uint16_t)((uint32_t)flux_bass   * 35UL / 1000UL)) : 0U;
        total = (uint16_t)br + (bf >> 2U);
        if (total > 255U) total = 255U;
        br = (uint8_t)total;
        WS2812_SetPixel((NUM_LEDS / 2U - 1U) - i, br, (uint8_t)(br >> 3U), 0U);

        // Cool blue: center-right LED, fills toward last LED
        br = (i < fill_t) ? (uint8_t)(15U + (uint16_t)((uint32_t)flux_treble * 35UL / 1000UL)) : 0U;
        total = (uint16_t)br + (tf >> 2U);
        if (total > 255U) total = 255U;
        br = (uint8_t)total;
        WS2812_SetPixel((NUM_LEDS / 2U) + i, 0U, (uint8_t)(br >> 3U), br);
    }
}

static void LED_Effect(uint8_t idx, uint8_t bass_beat, uint8_t full_beat,
                       uint16_t phase, uint16_t raw_env,
                       uint16_t flux_bass, uint16_t flux_full)
{
    switch (idx) {
        case 0:  ef_pulse(bass_beat, full_beat, flux_bass);              break;
        case 1:  ef_dual_comet(phase, bass_beat, raw_env);               break;
        default: ef_split(flux_bass, flux_full, bass_beat, full_beat);   break;
    }
}

int main(void)
{
    SYSTEM_Initialize();
    WS2812_Init(NUM_LEDS);

    // Startup: confirm all 96 LEDs — red → green → blue (~500ms each)
    {
        volatile uint32_t d;
        WS2812_SetAll(32, 0, 0); WS2812_Show();
        for (d = 0; d < 33000000UL; d++) {}
        WS2812_SetAll(0, 32, 0); WS2812_Show();
        for (d = 0; d < 33000000UL; d++) {}
        WS2812_SetAll(0, 0, 32); WS2812_Show();
        for (d = 0; d < 33000000UL; d++) {}
        WS2812_Clear(); WS2812_Show();
    }

    // Beat detection state — two independent detectors
    uint16_t bass_flux_history[BEAT_HIST_LEN] = {0};
    uint8_t bass_flux_idx = 0;
    uint8_t bass_cooldown = 0;

    uint16_t full_flux_history[BEAT_HIST_LEN] = {0};
    uint8_t full_flux_idx = 0;
    uint8_t full_cooldown = 0;

    // Phase oscillator: 0 → 1000 → 0 (triangle wave)
    // Phase 0 = position A (head up, dim LED)
    // Phase 1000 = position B (head down, bright LED)
    uint16_t phase = 0;
    int16_t phase_direction = 1;    // +1 going toward B, -1 going toward A
    uint16_t period_frames = DEFAULT_PERIOD_FRAMES;
    uint16_t phase_step = 2000 / DEFAULT_PERIOD_FRAMES;  // phase increment per frame

    // Tempo tracking: measure inter-beat intervals
    #define INTERVAL_HIST_LEN   8
    uint16_t interval_history[INTERVAL_HIST_LEN] = {0};
    uint8_t interval_idx = 0;
    uint16_t frames_since_beat = 0;
    uint8_t valid_intervals = 0;

    // Smoothed BPM output
    uint16_t bpm_smooth = 0;

    // Yellow flash overlay
    uint16_t green_duty = 0;

    (void)0; // removed last_raw_env

    // Kick-only window: after first movement, spend 20s trying kick-only detection
    // If it locks well, stay kick-only. If not, fall back to combined mode.
    #define KICK_ONLY_WINDOW    (23 * 20)   // 20 seconds
    #define KICK_ONLY_SUCCESS   8           // confidence threshold to stay kick-only
    (void)0; // removed kick_window_timer, detection_mode

    // Potentiometer: controls big-slam sensitivity threshold
    // ADC1 CH0 on RA3/AD1AN2, software triggered, raw 12-bit (0-4095)
    AD1CH0CON1bits.PINSEL = 2;          // AD1AN2 (RA3 = pot)
    AD1CH0CON1bits.SAMC = 15;           // max sample time (not speed-critical)
    AD1CH0CON1bits.TRG1SRC = 1;        // software trigger
    AD1CH0CON1bits.MODE = 0;            // single conversion, no oversampling
    AD1CH0CON1bits.IRQSEL = 0;
    AD1CONbits.ON = 1;
    while (AD1CONbits.ADRDY == 0) {}
    uint16_t pot_value = 2048;          // start at midpoint

    while (1)
    {
        UART_Debug_ProcessRx();

        // In servo test mode, skip all beat processing and UART TX
        if (servo_test_position >= 0) {
            Servo_SetPosition((uint16_t)servo_test_position);
            RGB_LED_Set(0, RGB_PWM_PERIOD / 4, 0);
            LED_BEAT = 0;
            continue;
        }

        BeatDetect_RunFFT();

        if (BeatDetect_HasFrame()) {
            uint16_t flux_full = BeatDetect_GetFluxValue();
            uint16_t flux_bass = BeatDetect_GetBassFluxValue();
            (void)BeatDetect_GetEnvelopeValue();
            uint16_t raw_env = BeatDetect_GetRawEnvelope();
            (void)0; // removed last_raw_env assignment
            frames_since_beat++;

            // --- Bass beat detector ---
            uint32_t bass_sum = 0;
            for (int i = 0; i < BEAT_HIST_LEN; i++) bass_sum += bass_flux_history[i];
            uint16_t bass_avg = (uint16_t)(bass_sum / BEAT_HIST_LEN);
            bass_flux_history[bass_flux_idx] = flux_bass;
            bass_flux_idx = (bass_flux_idx + 1) % BEAT_HIST_LEN;

            uint8_t bass_beat = 0;
            uint16_t bass_delta = (flux_bass > bass_avg) ? (flux_bass - bass_avg) : 0;
            if (bass_cooldown > 0) {
                bass_cooldown--;
            } else if (bass_delta >= BEAT_DELTA_THR &&
                       (raw_env > NOISE_GATE_THR || bass_delta >= NOISE_GATE_BYPASS_DELTA)) {
                bass_beat = (bass_delta > BEAT_DELTA_THR * 3) ? 2 : 1;
                bass_cooldown = BEAT_COOLDOWN;
            }

            // --- Mid+High beat detector ---
            uint32_t full_sum = 0;
            for (int i = 0; i < BEAT_HIST_LEN; i++) full_sum += full_flux_history[i];
            uint16_t full_avg = (uint16_t)(full_sum / BEAT_HIST_LEN);
            full_flux_history[full_flux_idx] = flux_full;
            full_flux_idx = (full_flux_idx + 1) % BEAT_HIST_LEN;

            uint8_t full_beat = 0;
            uint16_t full_delta = (flux_full > full_avg) ? (flux_full - full_avg) : 0;
            if (full_cooldown > 0) {
                full_cooldown--;
            } else if (full_delta >= BEAT_DELTA_THR &&
                       (raw_env > NOISE_GATE_THR || full_delta >= NOISE_GATE_BYPASS_DELTA)) {
                full_beat = (full_delta > BEAT_DELTA_THR * 3) ? 2 : 1;
                full_cooldown = BEAT_COOLDOWN;
            }

            // --- Selected band drives head bob / LED / tempo ---
            uint8_t beat_fired;
            if (uart_band_select == 0) beat_fired = bass_beat;
            else if (uart_band_select == 1) beat_fired = full_beat;
            else beat_fired = BeatDetect_IsBassDominant() ? bass_beat : full_beat;

            if (beat_fired > 0) {
                // Sync phase for GUI display
                phase = 0;
                phase_direction = 1;

                // Record interval for tempo tracking
                if (frames_since_beat >= MIN_PERIOD_FRAMES &&
                    frames_since_beat <= MAX_PERIOD_FRAMES) {
                    interval_history[interval_idx] = frames_since_beat;
                    interval_idx = (interval_idx + 1) % INTERVAL_HIST_LEN;
                    if (valid_intervals < INTERVAL_HIST_LEN) valid_intervals++;
                }
                frames_since_beat = 0;
            }

            // --- Tempo adjustment: smoothly adapt period ---
            if (valid_intervals >= 3) {
                // Find median interval
                uint16_t sorted[INTERVAL_HIST_LEN];
                for (int i = 0; i < valid_intervals; i++) sorted[i] = interval_history[i];
                // Simple sort
                for (int i = 0; i < valid_intervals - 1; i++) {
                    for (int j = i + 1; j < valid_intervals; j++) {
                        if (sorted[j] < sorted[i]) {
                            uint16_t tmp = sorted[i];
                            sorted[i] = sorted[j];
                            sorted[j] = tmp;
                        }
                    }
                }
                uint16_t median_interval = sorted[valid_intervals / 2];

                // Smooth adjustment: move period 10% toward detected interval each beat
                if (median_interval > period_frames) {
                    period_frames += (median_interval - period_frames) / 10 + 1;
                } else if (median_interval < period_frames) {
                    period_frames -= (period_frames - median_interval) / 10 + 1;
                }

                // Clamp
                if (period_frames < MIN_PERIOD_FRAMES) period_frames = MIN_PERIOD_FRAMES;
                if (period_frames > MAX_PERIOD_FRAMES) period_frames = MAX_PERIOD_FRAMES;
            }

            // No music: slowly decay to stopped (increase period toward max)
            if (raw_env < NOISE_GATE_THR && frames_since_beat > MAX_PERIOD_FRAMES * 2) {
                if (period_frames < MAX_PERIOD_FRAMES) {
                    period_frames++;  // slow drift toward stopped
                }
                // Clear interval history so it starts fresh when music returns
                valid_intervals = 0;
            }

            // Recalculate step from period (full cycle A→B→A = period_frames)
            // Half period = A→B, so step = 1000 / (period_frames/2) = 2000/period_frames
            phase_step = 2000 / period_frames;
            if (phase_step < 1) phase_step = 1;

            // --- Advance phase (only if music present) ---
            if (raw_env > NOISE_GATE_THR || frames_since_beat < MAX_PERIOD_FRAMES * 2) {
                if (phase_direction > 0) {
                    phase += phase_step;
                    if (phase >= 1000) {
                        phase = 1000;
                        phase_direction = -1;
                    }
                } else {
                    if (phase >= phase_step) {
                        phase -= phase_step;
                    } else {
                        phase = 0;
                        phase_direction = 1;
                    }
                }
            } else {
                // No music: drift phase back to 0 (head up / neutral)
                if (phase > 0) {
                    if (phase > 10) phase -= 10;
                    else phase = 0;
                }
            }

            // --- LED output: interpolate duty from phase ---
            // phase=0 → DUTY_A, phase=1000 → DUTY_B
            uint16_t red_duty = DUTY_A + (uint16_t)(((uint32_t)(DUTY_B - DUTY_A) * phase) / 1000);

            // Send both beat flags to GUI
            if (bass_beat > 0) uart_bass_beat = bass_beat;
            if (full_beat > 0) uart_full_beat = full_beat;

            // Yellow flash on big beats (overlay green briefly)
            if (beat_fired == 2) {
                green_duty = RGB_PWM_PERIOD / 3;
            }
            if (green_duty > 0) {
                green_duty -= green_duty / 3;
            }

            RGB_LED_Set(red_duty, green_duty, 0);
            LED_BEAT = (phase > 500) ? 1 : 0;

            // --- Read pot and update slam threshold ---
            AD1SWTRGbits.CH0TRG = 1;                // trigger ADC1 conversion
            while (!AD1STATbits.CH0RDY) {}          // wait (fast, ~1us)
            pot_value = (uint16_t)(AD1CH0DATA & 0x0FFF);  // 12-bit result
            // Pot fine-tune: 0=slowest (+12 frames), 4095=fastest (-8 frames), center=no change
            int8_t pot_offset = (int8_t)(12 - (int32_t)pot_value * 20 / 4095);
            NodEngine_SetPotOffset(pot_offset);

            // --- Nod Engine: auto-band selection, pass both bands ---
            if (servo_test_position < 0) {
                NodEngine_Frame(bass_beat, full_beat, raw_env);
            }

            // WS2812 beat-reactive effects: 3 effects, ~20s each (~470 frames)
            {
                static uint16_t ef_frame = 0U;
                static uint8_t  ef_idx   = 0U;
                LED_Effect(ef_idx, bass_beat, full_beat, phase, raw_env, flux_bass, flux_full);
                if (!WS2812_IsBusy()) WS2812_Show();
                if (++ef_frame >= 470U) { ef_frame = 0U; ef_idx = (ef_idx + 1U) % 3U; }
            }

            // --- Send frame to GUI: BPM from nod engine oscillator ---
            uart_phase = phase;
            uart_bpm = NodEngine_GetLockedBPM();
            UART_Debug_SendFrame();
        }

        if (BeatDetect_HasSpectrumFrame()) {
            UART_Debug_SendSpectrum();
        }

        // Servo — test mode handled in continue block above
        // Beat-sync servo is updated inside BeatDetect_HasFrame() at 23 Hz
    }
}
