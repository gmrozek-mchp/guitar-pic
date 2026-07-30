#include "nod_engine.h"
#include "servo.h"

// Servo angle limits (tenths of a degree)
#define ANGLE_HEAD_UP       170     // 17.0° — neutral / oscillator bottom
#define ANGLE_OSC_MAX       400     // 40.0° — oscillator peak
#define ANGLE_BEAT_MAX      650     // 65.0° — beat snap (above oscillator)

// Oscillator: adaptive period from beat intervals
// osc_period is a HALF-CYCLE (up or down), so full nod = 2 * osc_period
#define OSC_DEFAULT_PERIOD  12      // ~1.0 Hz nod fallback
#define OSC_MIN_PERIOD      4       // fastest half-cycle
#define OSC_MAX_PERIOD      18      // slowest half-cycle

// Beat interval range (full beat-to-beat, before /2 for half-cycle)
#define INTERVAL_MIN        8       // ~176 BPM
#define INTERVAL_MAX        35      // ~40 BPM

// Cooldown: minimum floor, actual cooldown adapts to tempo
#define NOD_COOLDOWN_MIN    6       // ~256ms absolute minimum

// Silence tracking
#define SILENCE_GATE        30      // raw_env below this = no audio
#define SILENCE_FRAMES_MAX  35      // ~1.5s of silence before stopping

// Comeback bang: big snap after a brief energy dip
#define DROP_RATIO          3       // armed when raw_env < energy_smooth / DROP_RATIO
#define DROP_ARM_FRAMES     4       // ~170ms below threshold to arm (~4 frames)
#define ANGLE_COMEBACK      800     // 80.0° — one-shot bang on return

// Beat-absence drift: slow oscillator to idle when beats stop
#define IDLE_DRIFT_TARGET   12      // 1 Hz nod (half-cycle at 23.4 fps)
#define BEAT_MISS_THRESH    2       // start drifting after missing 2 expected beats
#define IDLE_RANGE_TARGET   250     // 25.0° — shallow idle nod

// Band consistency tracker
#define BAND_HIST_LEN       6       // intervals per band
#define BAND_EVAL_INTERVAL  46      // re-evaluate every ~2 seconds
#define BAND_HYSTERESIS     5       // winner must beat other by this margin
#define BAND_MAX_SCORE      60      // above this = unreliable, go undecided

// Per-band interval tracking
typedef struct {
    uint16_t intervals[BAND_HIST_LEN];
    uint8_t  idx;
    uint8_t  count;
    uint16_t frames_since;
    uint8_t  score;         // 0=perfect, 255=garbage
} BandTracker;

// State
static uint16_t osc_phase;         // 0-1000 triangle wave position
static int8_t osc_direction;       // +1 rising, -1 falling
static uint8_t cooldown;           // retrigger block countdown
static uint8_t beat_snap;          // frames to hold at BEAT_MAX before releasing
static uint16_t beat_snap_angle;   // randomized snap angle for this beat
static uint16_t nod_current_angle; // actual angle this frame (for debug)
static uint16_t silence_frames;
static uint16_t lfsr = 0xACE1;     // PRNG for nod variation
static uint16_t energy_smooth;     // smoothed audio level for oscillator scaling
static uint8_t beat_toggle;        // alternates 0/1 each beat
static uint16_t osc_period;        // current oscillator half-period (adaptive)
static int8_t pot_period_offset;   // pot fine-tune adjustment

// Band selection state
static BandTracker band[2];        // [0]=bass, [1]=full
static uint8_t winning_band;       // 0=bass, 1=full, 2=undecided
static uint8_t band_eval_counter;  // counts down to next re-evaluation
static uint8_t osc_enabled = 1;    // 1=oscillator+beat, 0=beat-only

// Comeback bang state
static uint8_t drop_frames;        // frames below drop threshold
static uint8_t comeback_armed;     // 1 = next beat gets the big bang
static uint16_t pre_drop_period;   // saved osc_period before slowdown
static uint16_t frames_since_any_beat;  // frames since last onset from either band
static uint16_t osc_range_max;     // current max oscillator angle (decays during drift)

static uint8_t score_band(uint8_t b)
{
    BandTracker *bt = &band[b];
    if (bt->count < 4) return 255;

    uint16_t min_val = 0xFFFF;
    uint16_t max_val = 0;
    uint32_t sum = 0;
    uint8_t valid = 0;

    for (uint8_t i = 0; i < bt->count; i++) {
        uint16_t iv = bt->intervals[i];
        if (iv >= INTERVAL_MIN && iv <= INTERVAL_MAX) {
            valid++;
            sum += iv;
            if (iv < min_val) min_val = iv;
            if (iv > max_val) max_val = iv;
        }
    }

    if (valid < 3) return 255;

    uint16_t mean = (uint16_t)(sum / valid);
    if (mean == 0) return 255;

    // Score = (max - min) * 100 / mean — percentage spread
    uint16_t spread = (uint16_t)((uint32_t)(max_val - min_val) * 100 / mean);
    if (spread > 255) spread = 255;

    return (uint8_t)spread;
}

static void evaluate_bands(void)
{
    band[0].score = score_band(0);
    band[1].score = score_band(1);

    if (winning_band == NOD_BAND_BASS) {
        if (band[1].score + BAND_HYSTERESIS < band[0].score)
            winning_band = NOD_BAND_FULL;
    } else if (winning_band == NOD_BAND_FULL) {
        if (band[0].score + BAND_HYSTERESIS < band[1].score)
            winning_band = NOD_BAND_BASS;
    } else {
        // Undecided: pick whichever is good enough
        if (band[0].score <= band[1].score && band[0].score < BAND_MAX_SCORE)
            winning_band = NOD_BAND_BASS;
        else if (band[1].score < band[0].score && band[1].score < BAND_MAX_SCORE)
            winning_band = NOD_BAND_FULL;
    }

    // If winner degraded, go undecided
    if (winning_band < 2 && band[winning_band].score > BAND_MAX_SCORE)
        winning_band = NOD_BAND_UNDECIDED;
}

void NodEngine_Init(void)
{
    osc_phase = 0;
    osc_direction = 1;
    cooldown = 0;
    beat_snap = 0;
    silence_frames = SILENCE_FRAMES_MAX;
    osc_period = OSC_DEFAULT_PERIOD;
    nod_current_angle = ANGLE_HEAD_UP;
    pot_period_offset = 0;
    winning_band = NOD_BAND_UNDECIDED;
    band_eval_counter = BAND_EVAL_INTERVAL;
    drop_frames = 0;
    comeback_armed = 0;
    pre_drop_period = OSC_DEFAULT_PERIOD;
    frames_since_any_beat = 0;
    osc_range_max = ANGLE_OSC_MAX;

    for (uint8_t b = 0; b < 2; b++) {
        band[b].idx = 0;
        band[b].count = 0;
        band[b].frames_since = 0;
        band[b].score = 255;
        for (uint8_t i = 0; i < BAND_HIST_LEN; i++)
            band[b].intervals[i] = 0;
    }

    Servo_SetAngle(ANGLE_HEAD_UP);
}

void NodEngine_Frame(uint8_t bass_beat, uint8_t full_beat, uint16_t raw_env)
{
    // --- Track intervals for BOTH bands independently ---
    band[0].frames_since++;
    band[1].frames_since++;

    if (bass_beat > 0) {
        uint16_t iv = band[0].frames_since;
        if (iv >= INTERVAL_MIN && iv <= INTERVAL_MAX) {
            band[0].intervals[band[0].idx] = iv;
            band[0].idx = (band[0].idx + 1) % BAND_HIST_LEN;
            if (band[0].count < BAND_HIST_LEN) band[0].count++;
        }
        band[0].frames_since = 0;
    }

    if (full_beat > 0) {
        uint16_t iv = band[1].frames_since;
        if (iv >= INTERVAL_MIN && iv <= INTERVAL_MAX) {
            band[1].intervals[band[1].idx] = iv;
            band[1].idx = (band[1].idx + 1) % BAND_HIST_LEN;
            if (band[1].count < BAND_HIST_LEN) band[1].count++;
        }
        band[1].frames_since = 0;
    }

    // --- Periodically evaluate which band is more consistent ---
    if (--band_eval_counter == 0) {
        band_eval_counter = BAND_EVAL_INTERVAL;
        evaluate_bands();
    }

    // --- Tempo adaptation from winning band ---
    // Use the winning band's latest interval to adapt osc_period
    // KEY FIX: divide interval by 2 because osc_period is a HALF-CYCLE
    uint8_t tempo_beat;
    if (winning_band == NOD_BAND_BASS)
        tempo_beat = bass_beat;
    else if (winning_band == NOD_BAND_FULL)
        tempo_beat = full_beat;
    else
        tempo_beat = (bass_beat > 0) ? bass_beat : full_beat;  // prefer bass when undecided

    if (tempo_beat > 0) {
        uint16_t iv = band[winning_band < 2 ? winning_band : 0].frames_since;
        // Use the interval that was just recorded (frames_since was reset above)
        // Actually grab from the ring buffer — the latest recorded interval
        if (band[winning_band < 2 ? winning_band : 0].count > 0) {
            uint8_t bi = winning_band < 2 ? winning_band : 0;
            uint8_t last_idx = (band[bi].idx == 0) ? BAND_HIST_LEN - 1 : band[bi].idx - 1;
            iv = band[bi].intervals[last_idx];
            if (iv >= INTERVAL_MIN && iv <= INTERVAL_MAX) {
                // Half-period: one full nod per beat
                uint16_t target = iv / 2;
                if (target < OSC_MIN_PERIOD) target = OSC_MIN_PERIOD;
                if (target > OSC_MAX_PERIOD) target = OSC_MAX_PERIOD;
                // Smooth 25% toward target
                osc_period += ((int16_t)target - (int16_t)osc_period) / 4;
                if (osc_period < OSC_MIN_PERIOD) osc_period = OSC_MIN_PERIOD;
                if (osc_period > OSC_MAX_PERIOD) osc_period = OSC_MAX_PERIOD;
            }
        }
    }

    // --- Combined onset for snap triggering (max of both bands) ---
    uint8_t onset_strength = (bass_beat > full_beat) ? bass_beat : full_beat;

    // Beat-absence drift: slow to 1 Hz idle when beats stop arriving
    if (onset_strength > 0) {
        frames_since_any_beat = 0;
        osc_range_max = ANGLE_OSC_MAX;
    } else {
        if (frames_since_any_beat < 1000) frames_since_any_beat++;
        uint16_t miss_limit = (uint16_t)osc_period * 2 * BEAT_MISS_THRESH;
        if (frames_since_any_beat > miss_limit) {
            // Save tempo on first drift frame
            if (!comeback_armed) {
                pre_drop_period = osc_period;
                comeback_armed = 1;
            }
            // Fast drift toward idle (1 Hz) — jump 25% of remaining gap each frame
            if (osc_period < IDLE_DRIFT_TARGET) {
                uint16_t gap = IDLE_DRIFT_TARGET - osc_period;
                osc_period += (gap / 4) + 1;
            } else if (osc_period > IDLE_DRIFT_TARGET) {
                uint16_t gap = osc_period - IDLE_DRIFT_TARGET;
                osc_period -= (gap / 4) + 1;
            }
            // Decay nod range in parallel
            if (osc_range_max > IDLE_RANGE_TARGET) {
                uint16_t rgap = osc_range_max - IDLE_RANGE_TARGET;
                osc_range_max -= (rgap / 4) + 1;
            }
        }
    }

    // Track silence and smoothed energy
    if (raw_env < SILENCE_GATE) {
        if (silence_frames < SILENCE_FRAMES_MAX) silence_frames++;
    } else {
        silence_frames = 0;
    }
    if (raw_env > energy_smooth) {
        energy_smooth += (raw_env - energy_smooth) / 4;
    } else {
        energy_smooth -= (energy_smooth - raw_env) / 8;
    }

    // Comeback bang: arm when energy dips relative to recent level
    uint16_t drop_thresh = energy_smooth / DROP_RATIO;
    if (raw_env < drop_thresh && energy_smooth > 150) {
        if (drop_frames < 255) drop_frames++;
        if (drop_frames == DROP_ARM_FRAMES) {
            // First moment of arming: save current tempo
            comeback_armed = 1;
            pre_drop_period = osc_period;
        }
        if (comeback_armed) {
            // Gradually slow oscillator during the dip
            if (osc_period < OSC_MAX_PERIOD) osc_period++;
        }
    } else {
        drop_frames = 0;
    }

    // Silent: hold still at head-up, no oscillation
    if (silence_frames >= SILENCE_FRAMES_MAX) {
        osc_phase = 0;
        osc_direction = 1;
        Servo_SetAngle(ANGLE_HEAD_UP);
        nod_current_angle = ANGLE_HEAD_UP;
        return;
    }

    // Tick cooldown
    if (cooldown > 0) cooldown--;

    // Beat detected: every other beat gets the hard snap
    if (onset_strength > 0 && cooldown == 0) {
        beat_toggle ^= 1;
        uint8_t adaptive_cd = (uint8_t)(osc_period * 3 / 4);
        if (adaptive_cd < NOD_COOLDOWN_MIN) adaptive_cd = NOD_COOLDOWN_MIN;
        cooldown = adaptive_cd;

        if (comeback_armed) {
            // Comeback bang: one-shot 80° slam, restore tempo
            comeback_armed = 0;
            osc_period = pre_drop_period;
            beat_snap_angle = ANGLE_COMEBACK;
            beat_snap = 3;
            osc_phase = 1000;
            osc_direction = -1;
        } else if (beat_toggle == 0) {
            // Hard snap: angle scales with volume
            uint16_t base_angle = ANGLE_OSC_MAX +
                (uint32_t)(ANGLE_BEAT_MAX - ANGLE_OSC_MAX) * raw_env / 10000;
            if (base_angle < ANGLE_OSC_MAX) base_angle = ANGLE_OSC_MAX;
            if (base_angle > ANGLE_BEAT_MAX) base_angle = ANGLE_BEAT_MAX;

            // Random ±5° variation
            uint16_t bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1u;
            lfsr = (lfsr >> 1) | (bit << 15);
            int16_t offset = (int16_t)(lfsr % 101) - 50;
            beat_snap_angle = (uint16_t)((int16_t)base_angle + offset);
            if (beat_snap_angle < ANGLE_OSC_MAX) beat_snap_angle = ANGLE_OSC_MAX;

            beat_snap = 3;
            osc_phase = 1000;
            osc_direction = -1;
        } else {
            // Soft beat: reset oscillator to top
            osc_phase = 1000;
            osc_direction = -1;
        }
    }

    // Compute angle
    uint16_t angle;

    if (beat_snap > 0) {
        angle = beat_snap_angle;
        beat_snap--;
    } else if (!osc_enabled) {
        // Beat-only mode: hold at neutral, no oscillation
        angle = ANGLE_HEAD_UP;
    } else {
        // Advance oscillator (adaptive step from tempo + pot offset)
        int16_t effective_period = (int16_t)osc_period + pot_period_offset;
        if (effective_period < OSC_MIN_PERIOD) effective_period = OSC_MIN_PERIOD;
        if (effective_period > OSC_MAX_PERIOD) effective_period = OSC_MAX_PERIOD;
        uint16_t step = 2000 / (uint16_t)effective_period;
        if (osc_direction > 0) {
            osc_phase += step;
            if (osc_phase >= 1000) {
                osc_phase = 1000;
                osc_direction = -1;
            }
        } else {
            if (osc_phase >= step) {
                osc_phase -= step;
            } else {
                osc_phase = 0;
                osc_direction = 1;
            }
        }

        // Map phase 0-1000 to angle HEAD_UP-osc_range_max, scaled by audio energy
        uint16_t osc_range = (uint32_t)(osc_range_max - ANGLE_HEAD_UP) * osc_phase / 1000;
        if (energy_smooth < 500) {
            osc_range = (uint32_t)osc_range * energy_smooth / 500;
        }
        angle = ANGLE_HEAD_UP + osc_range;
    }

    Servo_SetAngle(angle);
    nod_current_angle = angle;
}

uint8_t NodEngine_GetState(void)
{
    return (beat_snap > 0 || cooldown > 0) ? 1 : 0;
}

uint16_t NodEngine_GetLockedBPM(void)
{
    if (osc_period == 0) return 0;
    // osc_period is half-cycle, so full beat interval = 2 * osc_period frames
    // BPM = frame_rate * 60 / (2 * osc_period) = 23.4 * 60 / (2 * osc_period)
    // = 1406 / (2 * osc_period) = 703 / osc_period
    return (uint16_t)(703u / osc_period);
}

uint8_t NodEngine_GetConfidence(void)
{
    if (winning_band >= 2) return 0;
    // Invert score: 0(best)→100, 60(worst)→0
    if (band[winning_band].score >= BAND_MAX_SCORE) return 0;
    return (uint8_t)(100 - (uint16_t)band[winning_band].score * 100 / BAND_MAX_SCORE);
}

void NodEngine_SetPotOffset(int8_t offset)
{
    pot_period_offset = offset;
}

uint16_t NodEngine_GetTargetAngle(void)
{
    return nod_current_angle;
}

uint8_t NodEngine_GetRepetitionCount(void)
{
    return 0;
}

int8_t NodEngine_GetPotOffset(void)
{
    return pot_period_offset;
}

uint8_t NodEngine_GetWinningBand(void)
{
    return winning_band;
}

void NodEngine_SetOscEnabled(uint8_t en)
{
    osc_enabled = en;
}
