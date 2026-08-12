#include "beat_nod.h"

#include "definitions.h"   /* SYSTICK_GetTickCounter */
#include "nod_engine.h"
#include "servo.h"

/* Park the neck if no beat frame arrives for this long (music/bus quiet). */
#define IDLE_PARK_MS   (750u)

/* Nod engine angle domain (tenths of a degree): ANGLE_HEAD_UP neutral up to
 * ANGLE_COMEBACK on a full slam. Mapped onto the neck servo's signed position
 * range: 0 at head-up, SERVO_POS_MIN on a comeback (nod drives the head down). */
#define NOD_ANGLE_MIN  (170u)   /* ANGLE_HEAD_UP  */
#define NOD_ANGLE_MAX  (800u)   /* ANGLE_COMEBACK */

/* Auto mode. beatbox's frames arrive at ~23.4 Hz, the engine's design tick rate,
 * so frame counts double as time.
 *
 * A burst's length is a *duration with a floor*, not a beat count. Once he commits,
 * he head-bangs for at least AUTO_MIN_F and nothing cuts it short — the engine's
 * confidence is re-evaluated only every ~2 s and can read 0 for a stretch, which as
 * a mid-burst abort ended bursts almost as soon as they began. The only early exit
 * is the frames stopping altogether (idle park). */
#define AUTO_FPS               (23u)
#define AUTO_CONF_MIN_DEFAULT  (30u)             /* interval spread within ~40%   */
#define AUTO_MIN_F             (10u * AUTO_FPS)  /* every burst runs >= 10 s      */
#define AUTO_EXTRA_SPAN_F      (8u * AUTO_FPS)   /* .. + 0-8 s of jitter          */
#define AUTO_END_GRACE_F       (2u * AUTO_FPS)   /* slack to finish head-up       */
#define AUTO_WAIT_MIN_F        (8u * AUTO_FPS)
#define AUTO_WAIT_SPAN_F       (12u * AUTO_FPS)  /* 8..20 s parked between bursts */
#define AUTO_ARM_PATIENCE_F    (2u * AUTO_FPS)   /* armed this long -> any onset will do */
#define AUTO_PARK_ANGLE        (NOD_ANGLE_MIN + 30u)   /* "head basically up"    */

/* Latest decoded frame + a flag set by OnFrame, consumed by Tasks. Both run in
 * main-loop context (the RX callback fires synchronously inside TC6_Service),
 * so no locking is needed. */
static volatile bool    s_new_frame;
static uint8_t          s_seq, s_energy, s_bass, s_treble, s_kick, s_flags;
static uint32_t         s_frame_count;
static uint32_t         s_last_frame_ms;
static bool             s_parked;       /* neck currently held at neutral */
static beat_nod_mode_t  s_mode;         /* default off; set via CLI / 0x88B9 */
static int8_t           s_neck_pos;     /* last neck position applied */

/* Auto-mode burst state. */
static beat_nod_auto_t  s_auto_state;
static uint16_t         s_auto_beats;       /* beats seen during the burst (info) */
static uint16_t         s_auto_target;      /* frames this burst runs for        */
static uint16_t         s_auto_frames;      /* frames into the current burst     */
static uint16_t         s_auto_wait;        /* frames left before the next burst */
static uint16_t         s_auto_armed;       /* frames spent eligible, awaiting a beat */
static uint32_t         s_auto_bursts;
static uint8_t          s_auto_conf_min = AUTO_CONF_MIN_DEFAULT;

/* Why a burst isn't starting, for the CLI: whether the confidence floor is even
 * reachable on this music (peak), how many frames cleared it (eligible), and how
 * many strong beats the song has offered (bigs). */
static uint32_t         s_auto_eligible;
static uint32_t         s_auto_bigs;
static uint8_t          s_auto_conf_peak;

/* Burst length / spacing jitter. Stirred with the frame's energy byte so the
 * sequence isn't identical every power-up. */
static uint16_t         s_rng = 0xBEEFu;

static uint16_t rng_next(void)
{
    s_rng ^= s_energy;
    if (s_rng == 0u) { s_rng = 0xBEEFu; }   /* an all-zero LFSR never leaves 0 */
    uint16_t bit = ((s_rng >> 0) ^ (s_rng >> 2) ^ (s_rng >> 3) ^ (s_rng >> 5)) & 1u;
    s_rng = (uint16_t)((s_rng >> 1) | (uint16_t)(bit << 15));
    return s_rng;
}

static int8_t angle_to_pos(uint16_t angle)
{
    if (angle < NOD_ANGLE_MIN) { angle = NOD_ANGLE_MIN; }
    if (angle > NOD_ANGLE_MAX) { angle = NOD_ANGLE_MAX; }
    return (int8_t)-((int32_t)(angle - NOD_ANGLE_MIN) * (int32_t)SERVO_POS_MAX
                     / (int32_t)(NOD_ANGLE_MAX - NOD_ANGLE_MIN));
}

/* Whether the neck should be following the engine this frame. */
static bool driving(void)
{
    if (s_mode == BEAT_NOD_ALWAYS) { return true; }
    if (s_mode == BEAT_NOD_AUTO)   { return (s_auto_state != BEAT_NOD_AUTO_WAIT); }
    return false;
}

/* One auto-mode tick, after the engine has consumed the frame. `beat_now` is an
 * onset on the band the engine is tracking; `big` is the frame's BIG_BEAT flag. */
static void auto_tick(uint8_t beat_now, bool big)
{
    uint8_t conf = NodEngine_GetConfidence();
    if (conf > s_auto_conf_peak) { s_auto_conf_peak = conf; }
    if (big) { s_auto_bigs++; }

    if (s_auto_state == BEAT_NOD_AUTO_WAIT) {
        if (s_auto_wait > 0u) { s_auto_wait--; s_auto_armed = 0u; return; }

        /* Confidence is the quality gate: the nod looks worst exactly when the
         * engine hasn't settled on a band and an interval, so he only joins in
         * once it has. A floor of 0 disables the gate. */
        if (conf < s_auto_conf_min) { s_auto_armed = 0u; return; }
        s_auto_eligible++;
        if (s_auto_armed < 0xFFFFu) { s_auto_armed++; }

        /* Armed: come in on a strong beat when the song offers one, but don't hold
         * out forever — a song whose onsets never reach the detector's "strong"
         * margin would otherwise never start a burst at all. After the patience
         * window any onset will do. */
        bool start = big || ((s_auto_armed >= AUTO_ARM_PATIENCE_F) && (beat_now > 0u));
        if (!start) { return; }

        s_auto_armed  = 0u;
        s_auto_state  = BEAT_NOD_AUTO_NODDING;
        s_auto_beats  = 0u;
        s_auto_frames = 0u;
        s_auto_target = AUTO_MIN_F + (uint16_t)(rng_next() % AUTO_EXTRA_SPAN_F);
        s_auto_bursts++;
        return;
    }

    s_auto_frames++;
    if (beat_now > 0u) { s_auto_beats++; }

    if ((s_auto_state == BEAT_NOD_AUTO_NODDING) && (s_auto_frames >= s_auto_target)) {
        s_auto_state = BEAT_NOD_AUTO_FINISHING;
    }

    /* Park between nods rather than mid-slam — but never hold the head down waiting
     * for an angle that has stopped moving. */
    if ((s_auto_state == BEAT_NOD_AUTO_FINISHING)
        && ((NodEngine_GetTargetAngle() <= AUTO_PARK_ANGLE)
            || (s_auto_frames >= (uint16_t)(s_auto_target + AUTO_END_GRACE_F)))) {
        s_auto_state = BEAT_NOD_AUTO_WAIT;
        s_auto_wait  = AUTO_WAIT_MIN_F + (uint16_t)(rng_next() % AUTO_WAIT_SPAN_F);
    }
}

void BeatNod_Initialize(void)
{
    NodEngine_Init();
    s_last_frame_ms = SYSTICK_GetTickCounter();
    s_neck_pos = Servo_SetPosition(SERVO_NECK, SERVO_POS_NEUTRAL);
    s_parked = true;
}

void BeatNod_OnFrame(const uint8_t *payload, uint16_t len)
{
    if ((payload == NULL) || (len < BEAT_NOD_FRAME_LEN)) {
        return;
    }
    s_seq    = payload[0];
    s_energy = payload[1];
    s_bass   = payload[2];
    s_treble = payload[3];
    s_kick   = payload[4];
    s_flags  = payload[5];
    s_frame_count++;
    s_new_frame = true;
}

void BeatNod_Tasks(void)
{
    if (!s_new_frame) {
        /* No frames for a while: park the neck once so the head doesn't freeze
         * mid-nod when the music/bus goes quiet, and free the servo for a
         * manual/marvin command. An auto burst interrupted this way doesn't owe
         * the bus a cooldown — the confidence gate holds him off until the engine
         * has re-learned the tempo anyway. */
        uint32_t now = SYSTICK_GetTickCounter();
        if ((s_mode != BEAT_NOD_OFF) && !s_parked
            && ((now - s_last_frame_ms) >= IDLE_PARK_MS)) {
            s_neck_pos = Servo_SetPosition(SERVO_NECK, SERVO_POS_NEUTRAL);
            s_parked = true;
            s_auto_state = BEAT_NOD_AUTO_WAIT;
            s_auto_wait  = 0u;
        }
        return;
    }
    s_new_frame = false;
    s_last_frame_ms = SYSTICK_GetTickCounter();

    /* Beat strengths from the flags: 2 = strong (BIG set), 1 = onset. */
    uint8_t bass_beat = (s_flags & BEAT_FLAG_BASS) ? ((s_flags & BEAT_FLAG_BIG) ? 2u : 1u) : 0u;
    uint8_t full_beat = (s_flags & BEAT_FLAG_MID)  ? ((s_flags & BEAT_FLAG_BIG) ? 2u : 1u) : 0u;

    /* Lift the 0-255 energy field into the engine's 0-10000 loudness domain so
     * its silence/comeback thresholds hold (255 * 39 = 9945). */
    uint16_t raw_env = (uint16_t)s_energy * 39u;

    /* The engine ticks in every mode, so its tempo tracking is already warm when
     * a burst starts (and the CLI's bpm/conf lines stay live while he sits still). */
    NodEngine_Frame(bass_beat, full_beat, raw_env);

    if (s_mode == BEAT_NOD_AUTO) {
        uint8_t win = NodEngine_GetWinningBand();
        auto_tick((win == NOD_BAND_FULL) ? full_beat : bass_beat,
                  (s_flags & BEAT_FLAG_BIG) != 0u);
    }

    if (driving()) {
        s_neck_pos = Servo_SetPosition(SERVO_NECK,
                                       angle_to_pos(NodEngine_GetTargetAngle()));
        s_parked = false;
    } else if (!s_parked) {
        /* Park once, then leave the servo alone: between bursts a manual `pos` or
         * marvin 0x88B5 position sticks instead of being rewritten every frame. */
        s_neck_pos = Servo_SetPosition(SERVO_NECK, SERVO_POS_NEUTRAL);
        s_parked = true;
    }
}

void BeatNod_SetMode(beat_nod_mode_t mode)
{
    if ((mode != BEAT_NOD_ALWAYS) && (mode != BEAT_NOD_AUTO)) { mode = BEAT_NOD_OFF; }
    s_mode = mode;

    /* Auto starts parked and eligible immediately — no cooldown to serve. */
    s_auto_state = BEAT_NOD_AUTO_WAIT;
    s_auto_wait  = 0u;
    if (mode != BEAT_NOD_ALWAYS) {
        s_neck_pos = Servo_SetPosition(SERVO_NECK, SERVO_POS_NEUTRAL);
        s_parked = true;
    }
}

beat_nod_mode_t BeatNod_GetMode(void) { return s_mode; }

void BeatNod_SetAutoConfMin(uint8_t conf_min)
{
    s_auto_conf_min = (conf_min > 100u) ? 100u : conf_min;
}

void BeatNod_GetAuto(beat_nod_auto_status_t *out)
{
    if (out == NULL) { return; }
    out->state       = s_auto_state;
    out->beats       = s_auto_beats;
    out->run_ms      = (uint16_t)((uint32_t)s_auto_frames * 1000u / AUTO_FPS);
    out->target_ms   = (uint16_t)((uint32_t)s_auto_target * 1000u / AUTO_FPS);
    out->wait_ms     = (uint16_t)((uint32_t)s_auto_wait * 1000u / AUTO_FPS);
    out->conf_min    = s_auto_conf_min;
    out->bursts      = s_auto_bursts;
    out->conf_peak   = s_auto_conf_peak;
    out->eligible    = s_auto_eligible;
    out->bigs        = s_auto_bigs;
}

uint32_t BeatNod_FrameCount(void) { return s_frame_count; }
int8_t   BeatNod_NeckPosition(void) { return s_neck_pos; }

void BeatNod_GetLast(uint8_t *seq, uint8_t *energy, uint8_t *bass,
                     uint8_t *treble, uint8_t *kick, uint8_t *flags)
{
    if (seq    != NULL) { *seq    = s_seq; }
    if (energy != NULL) { *energy = s_energy; }
    if (bass   != NULL) { *bass   = s_bass; }
    if (treble != NULL) { *treble = s_treble; }
    if (kick   != NULL) { *kick   = s_kick; }
    if (flags  != NULL) { *flags  = s_flags; }
}
