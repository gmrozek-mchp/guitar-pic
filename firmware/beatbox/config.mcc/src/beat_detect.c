#include <math.h>
#include <string.h>
#include "beat_detect.h"

#define SAMPLE_RATE         48000.0f
#define FFT_SIZE            512
#define FFT_HALF            (FFT_SIZE / 2)
#define DOWNSAMPLE_FACTOR   4
#define DS_RATE             (SAMPLE_RATE / DOWNSAMPLE_FACTOR)
#define FRAME_RATE          (DS_RATE / FFT_SIZE)  // ~23.4 Hz

// FFT state
static float ds_accum = 0.0f;
static uint8_t ds_count = 0;
static float fft_input[FFT_SIZE];
static uint16_t fft_sample_idx = 0;
static volatile bool fft_ready = false;

static float fft_real[FFT_SIZE];
static float fft_imag[FFT_SIZE];
static float fft_mag_prev[FFT_HALF];

// Precomputed
static float twiddle_cos[FFT_HALF];
static float twiddle_sin[FFT_HALF];
static float hanning[FFT_SIZE];

// Audio envelope: track peak sample in current frame
static float frame_peak = 0.0f;

// Output values (updated each FFT frame)
static volatile uint16_t out_envelope = 0;    // 0-1000 (auto-ranged)
static volatile uint16_t out_flux = 0;        // 0-1000 (auto-ranged)
static volatile uint16_t out_raw_envelope = 0; // 0-10000 (absolute, 1.0 = 10000)
static volatile bool frame_ready = false;

// Spectrum output (64 bins, 0-255, sent every 3rd frame)
#define SPEC_BINS           64
static volatile uint8_t out_spectrum[SPEC_BINS];
static volatile bool spectrum_ready = false;
static uint8_t spec_frame_count = 0;
static float spec_display_max = 0.01f;

// Auto-ranging
static float envelope_max = 0.01f;
static float flux_max = 0.01f;

// Bass-band beat detection
#define BASS_BIN_START      1
#define BASS_BIN_END        10      // bins 1-10 = ~23-234 Hz (kick drum range)
#define BASS_RATIO_THRESH   0.35f   // bass is "dominant" if >35% of total energy
#define BASS_RATIO_SMOOTH   0.05f   // smoothing factor for bass ratio tracker

static float bass_flux_max = 0.01f;
static volatile uint16_t out_bass_flux = 0;       // 0-1000 (auto-ranged)
static float bass_ratio_smooth = 0.0f;
static volatile bool bass_dominant = false;
static volatile uint16_t out_bass_peak_bin = 0;   // bin with largest onset in bass range
static volatile uint16_t out_full_peak_bin = 0;   // bin with largest onset in mid+high range

// Kick drum detector: energy in bins 2-4 (~47-94 Hz) with adaptive threshold
#define KICK_BIN_START      2
#define KICK_BIN_END        4
#define KICK_ATTACK         0.7f    // fast attack for energy envelope
#define KICK_RELEASE        0.04f   // slow release
#define KICK_AVG_ALPHA      0.015f  // slow-moving average (adaptive threshold base)
#define KICK_THRESH_MULT    2.0f    // beat = energy > mult * average
#define KICK_MIN_INTERVAL   6       // minimum frames between kicks (~256ms)

static float kick_energy_env = 0.0f;
static float kick_avg = 0.0f;
static uint8_t kick_cooldown = 0;
static volatile uint8_t out_kick_beat = 0;    // 0=none, 1=normal, 2=strong
static volatile uint16_t out_kick_strength = 0; // 0-1000 proportional to hit intensity

// =============================================================================
// FFT
// =============================================================================

static void fft_init(void)
{
    for (int i = 0; i < FFT_HALF; i++) {
        float angle = -6.2831853f * (float)i / (float)FFT_SIZE;
        twiddle_cos[i] = cosf(angle);
        twiddle_sin[i] = sinf(angle);
    }
    for (int i = 0; i < FFT_SIZE; i++) {
        hanning[i] = 0.5f * (1.0f - cosf(6.2831853f * (float)i / (float)(FFT_SIZE - 1)));
    }
}

static void fft_bit_reverse(float *re, float *im, int n)
{
    int j = 0;
    for (int i = 1; i < n - 1; i++) {
        int bit = n >> 1;
        while (j & bit) { j ^= bit; bit >>= 1; }
        j ^= bit;
        if (i < j) {
            float tmp = re[i]; re[i] = re[j]; re[j] = tmp;
            tmp = im[i]; im[i] = im[j]; im[j] = tmp;
        }
    }
}

static void fft_compute(float *re, float *im, int n)
{
    fft_bit_reverse(re, im, n);
    for (int len = 2; len <= n; len <<= 1) {
        int half = len >> 1;
        int step = n / len;
        for (int i = 0; i < n; i += len) {
            for (int k = 0; k < half; k++) {
                int tw = k * step;
                float tre = re[i+k+half]*twiddle_cos[tw] - im[i+k+half]*twiddle_sin[tw];
                float tim = re[i+k+half]*twiddle_sin[tw] + im[i+k+half]*twiddle_cos[tw];
                re[i+k+half] = re[i+k] - tre;
                im[i+k+half] = im[i+k] - tim;
                re[i+k] += tre;
                im[i+k] += tim;
            }
        }
    }
}

// =============================================================================
// Public API
// =============================================================================

void BeatDetect_Init(void)
{
    fft_init();
    memset(fft_input, 0, sizeof(fft_input));
    memset(fft_mag_prev, 0, sizeof(fft_mag_prev));
    ds_accum = 0.0f;
    ds_count = 0;
    fft_sample_idx = 0;
    fft_ready = false;
    frame_peak = 0.0f;
    out_envelope = 0;
    out_flux = 0;
    frame_ready = false;
    envelope_max = 0.01f;
    flux_max = 0.01f;
    kick_energy_env = 0.0f;
    kick_avg = 0.0f;
    kick_cooldown = 0;
    out_kick_beat = 0;
    out_kick_strength = 0;
}

void BeatDetect_Process(float mono_sample)
{
    // Track peak amplitude for envelope
    float abs_val = (mono_sample > 0) ? mono_sample : -mono_sample;
    if (abs_val > frame_peak) frame_peak = abs_val;

    // Downsample and buffer for FFT
    ds_accum += mono_sample;
    ds_count++;
    if (ds_count >= DOWNSAMPLE_FACTOR) {
        fft_input[fft_sample_idx] = ds_accum / (float)DOWNSAMPLE_FACTOR;
        fft_sample_idx++;
        ds_accum = 0.0f;
        ds_count = 0;
        if (fft_sample_idx >= FFT_SIZE) {
            fft_sample_idx = 0;
            fft_ready = true;
        }
    }
}

void BeatDetect_RunFFT(void)
{
    if (!fft_ready) return;
    fft_ready = false;

    // --- Envelope: peak sample this frame ---
    float env = frame_peak;
    frame_peak = 0.0f;  // reset for next frame

    // Raw absolute envelope (0-10000 scale, 1.0 full-scale = 10000)
    float raw_scaled = env * 10000.0f;
    if (raw_scaled > 10000.0f) raw_scaled = 10000.0f;
    out_raw_envelope = (uint16_t)raw_scaled;

    // Auto-ranged 0-1000 (faster decay for more dynamic range)
    // Gate: if raw signal is negligible, output 0 and don't let max decay into noise
    if (env < 0.002f) {
        out_envelope = 0;
    } else {
        if (env > envelope_max) envelope_max = env;
        else { envelope_max *= 0.995f; if (envelope_max < 0.01f) envelope_max = 0.01f; }

        float env_scaled = (env / envelope_max) * 1000.0f;
        if (env_scaled > 1000.0f) env_scaled = 1000.0f;
        out_envelope = (uint16_t)env_scaled;
    }

    // --- FFT + Spectral Flux ---
    for (int i = 0; i < FFT_SIZE; i++) {
        fft_real[i] = fft_input[i] * hanning[i];
        fft_imag[i] = 0.0f;
    }
    fft_compute(fft_real, fft_imag, FFT_SIZE);

    float mid_high_flux = 0.0f;
    float bass_flux = 0.0f;
    float bass_energy = 0.0f;
    float total_energy = 0.0f;
    float bass_peak_diff = 0.0f;
    uint16_t bass_peak_i = 0;
    float full_peak_diff = 0.0f;
    uint16_t full_peak_i = 0;
    for (int i = 1; i < FFT_HALF; i++) {
        float mag = sqrtf(fft_real[i]*fft_real[i] + fft_imag[i]*fft_imag[i]);
        float diff = mag - fft_mag_prev[i];
        if (i >= BASS_BIN_START && i <= BASS_BIN_END) {
            if (diff > 0.0f) {
                bass_flux += diff;
                if (diff > bass_peak_diff) { bass_peak_diff = diff; bass_peak_i = i; }
            }
            bass_energy += mag;
        } else {
            if (diff > 0.0f) {
                mid_high_flux += diff;
                if (diff > full_peak_diff) { full_peak_diff = diff; full_peak_i = i; }
            }
        }
        total_energy += mag;
        fft_mag_prev[i] = mag;
    }
    out_bass_peak_bin = bass_peak_i;
    out_full_peak_bin = full_peak_i;

    // --- Kick drum detector (bins 2-4, ~47-94 Hz) ---
    float kick_raw = 0.0f;
    for (int i = KICK_BIN_START; i <= KICK_BIN_END; i++) {
        kick_raw += fft_mag_prev[i];
    }

    // Fast attack / slow release envelope
    if (kick_raw > kick_energy_env) {
        kick_energy_env += KICK_ATTACK * (kick_raw - kick_energy_env);
    } else {
        kick_energy_env += KICK_RELEASE * (kick_raw - kick_energy_env);
    }

    // Slow-moving average for adaptive threshold
    kick_avg += KICK_AVG_ALPHA * (kick_energy_env - kick_avg);
    if (kick_avg < 0.001f) kick_avg = 0.001f;

    // Detect kick — require absolute minimum energy to prevent noise triggers
    // When raw_env is below noise gate, suppress all kick detection
    if (kick_cooldown > 0) {
        kick_cooldown--;
        out_kick_beat = 0;
        out_kick_strength = 0;
    } else if (out_raw_envelope > 40 &&
               kick_energy_env > kick_avg * KICK_THRESH_MULT && kick_raw > 0.02f) {
        float ratio = kick_energy_env / kick_avg;
        out_kick_beat = (ratio > KICK_THRESH_MULT * 2.0f) ? 2 : 1;
        // Strength: how far above threshold (2.0x = 0, 6.0x+ = 1000)
        float str = (ratio - KICK_THRESH_MULT) / 4.0f;
        if (str > 1.0f) str = 1.0f;
        if (str < 0.0f) str = 0.0f;
        out_kick_strength = (uint16_t)(str * 1000.0f);
        kick_cooldown = KICK_MIN_INTERVAL;
    } else {
        out_kick_beat = 0;
        out_kick_strength = 0;
    }

    // Prevent adaptive threshold from decaying into noise floor
    // Reset kick_avg to a minimum floor so normalized noise can't trigger
    if (out_raw_envelope < 30) {
        kick_avg = kick_energy_env + 0.01f;
    }

    // Bass dominance detection
    float bass_ratio = (total_energy > 0.001f) ? (bass_energy / total_energy) : 0.0f;
    bass_ratio_smooth += BASS_RATIO_SMOOTH * (bass_ratio - bass_ratio_smooth);
    bass_dominant = (bass_ratio_smooth > BASS_RATIO_THRESH);

    // Mid+High flux (bins 11-255, ~257-6000 Hz)
    if (mid_high_flux > flux_max) flux_max = mid_high_flux;
    else { flux_max *= 0.999f; if (flux_max < 0.001f) flux_max = 0.001f; }

    float flux_scaled = (mid_high_flux / flux_max) * 1000.0f;
    if (flux_scaled > 1000.0f) flux_scaled = 1000.0f;
    out_flux = (uint16_t)flux_scaled;

    // Bass-band flux (bins 1-10, ~23-234 Hz)
    if (bass_flux > bass_flux_max) bass_flux_max = bass_flux;
    else { bass_flux_max *= 0.999f; if (bass_flux_max < 0.001f) bass_flux_max = 0.001f; }

    float bass_flux_scaled = (bass_flux / bass_flux_max) * 1000.0f;
    if (bass_flux_scaled > 1000.0f) bass_flux_scaled = 1000.0f;
    out_bass_flux = (uint16_t)bass_flux_scaled;

    frame_ready = true;

    // Spectrum output every 3rd frame (~8 Hz)
    spec_frame_count++;
    if (spec_frame_count >= 3) {
        spec_frame_count = 0;
        float smax = 0.0f;
        for (int i = 0; i < SPEC_BINS; i++) {
            if (fft_mag_prev[i+1] > smax) smax = fft_mag_prev[i+1];
        }
        if (smax > spec_display_max) spec_display_max = smax;
        else { spec_display_max *= 0.99f; if (spec_display_max < 0.001f) spec_display_max = 0.001f; }

        for (int i = 0; i < SPEC_BINS; i++) {
            float s = (fft_mag_prev[i+1] / spec_display_max) * 220.0f;
            if (s > 255.0f) s = 255.0f;
            out_spectrum[i] = (uint8_t)s;
        }
        spectrum_ready = true;
    }
}

bool BeatDetect_HasSpectrumFrame(void)
{
    if (spectrum_ready) {
        spectrum_ready = false;
        return true;
    }
    return false;
}

void BeatDetect_GetSpectrumData(uint8_t *out)
{
    memcpy(out, (const void*)out_spectrum, SPEC_BINS);
}

bool BeatDetect_HasFrame(void)
{
    if (frame_ready) {
        frame_ready = false;
        return true;
    }
    return false;
}

uint16_t BeatDetect_GetEnvelopeValue(void) { return out_envelope; }
uint16_t BeatDetect_GetFluxValue(void) { return out_flux; }
uint16_t BeatDetect_GetBassFluxValue(void) { return out_bass_flux; }
bool BeatDetect_IsBassDominant(void) { return bass_dominant; }
uint16_t BeatDetect_GetBassPeakBin(void) { return out_bass_peak_bin; }
uint16_t BeatDetect_GetFullPeakBin(void) { return out_full_peak_bin; }
uint16_t BeatDetect_GetRawEnvelope(void) { return out_raw_envelope; }
uint8_t BeatDetect_GetKickBeat(void) { return out_kick_beat; }
uint16_t BeatDetect_GetKickStrength(void) { return out_kick_strength; }
