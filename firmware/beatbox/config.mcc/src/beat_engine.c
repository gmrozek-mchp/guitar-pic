#include "beat_engine.h"

#include <stddef.h>

#include "audio.h"
#include "beat_detect.h"

/* Beat-decision tuning (per-frame, ~23.4 Hz). A beat fires when a band's flux
 * jumps BEAT_DELTA_THR above its running average, subject to a cooldown and a
 * raw-envelope noise gate (bypassed by a strong enough flux delta). */
#define BEAT_HIST_LEN            4
#define BEAT_COOLDOWN            8
#define BEAT_DELTA_THR           250
#define NOISE_GATE_THR           30      /* raw envelope (0-10000) */
#define NOISE_GATE_BYPASS_DELTA  250     /* flux delta strong enough to bypass the gate */

static uint16_t s_bass_hist[BEAT_HIST_LEN];
static uint8_t  s_bass_idx;
static uint8_t  s_bass_cooldown;

static uint16_t s_full_hist[BEAT_HIST_LEN];
static uint8_t  s_full_idx;
static uint8_t  s_full_cooldown;

static BeatFrame s_frame;
static volatile bool s_frame_ready;

/* Fed the post-HPF L/R pair from the audio ISR; sums to mono for analysis. */
static void on_samples(float left, float right)
{
    BeatDetect_Process((left + right) * 0.5f);
}

void Beat_Initialize(void)
{
    BeatDetect_Init();

    for (uint8_t i = 0u; i < BEAT_HIST_LEN; i++)
    {
        s_bass_hist[i] = 0u;
        s_full_hist[i] = 0u;
    }
    s_bass_idx = 0u;
    s_bass_cooldown = 0u;
    s_full_idx = 0u;
    s_full_cooldown = 0u;

    s_frame_ready = false;

    Audio_SampleCallbackRegister(&on_samples);
}

/* One band's onset detector: beat when flux exceeds its running average by
 * BEAT_DELTA_THR, gated on raw envelope unless the delta is strong on its own. */
static uint8_t detect_band(uint16_t flux, uint16_t raw_env,
                           uint16_t *hist, uint8_t *idx, uint8_t *cooldown)
{
    uint32_t sum = 0u;
    for (uint8_t i = 0u; i < BEAT_HIST_LEN; i++)
    {
        sum += hist[i];
    }
    uint16_t avg = (uint16_t)(sum / BEAT_HIST_LEN);
    hist[*idx] = flux;
    *idx = (uint8_t)((*idx + 1u) % BEAT_HIST_LEN);

    uint16_t delta = (flux > avg) ? (uint16_t)(flux - avg) : 0u;
    if (*cooldown > 0u)
    {
        (*cooldown)--;
        return 0u;
    }
    if (delta >= BEAT_DELTA_THR &&
        (raw_env > NOISE_GATE_THR || delta >= NOISE_GATE_BYPASS_DELTA))
    {
        *cooldown = BEAT_COOLDOWN;
        return (delta > BEAT_DELTA_THR * 3u) ? 2u : 1u;
    }
    return 0u;
}

void Beat_Tasks(void)
{
    BeatDetect_RunFFT();

    if (!BeatDetect_HasFrame())
    {
        return;
    }

    uint16_t flux_full = BeatDetect_GetFluxValue();
    uint16_t flux_bass = BeatDetect_GetBassFluxValue();
    uint16_t raw_env   = BeatDetect_GetRawEnvelope();

    BeatFrame f;
    f.flux_bass     = flux_bass;
    f.flux_full     = flux_full;
    f.raw_env       = raw_env;
    f.bass_dominant = BeatDetect_IsBassDominant();
    f.kick_beat     = BeatDetect_GetKickBeat();
    f.kick_strength = BeatDetect_GetKickStrength();
    f.bass_beat     = detect_band(flux_bass, raw_env, s_bass_hist, &s_bass_idx, &s_bass_cooldown);
    f.full_beat     = detect_band(flux_full, raw_env, s_full_hist, &s_full_idx, &s_full_cooldown);

    s_frame = f;
    s_frame_ready = true;
}

bool Beat_HasFrame(void)
{
    if (s_frame_ready)
    {
        s_frame_ready = false;
        return true;
    }
    return false;
}

void Beat_GetFrame(BeatFrame *out)
{
    if (out != NULL)
    {
        *out = s_frame;
    }
}
