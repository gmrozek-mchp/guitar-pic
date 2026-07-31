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

/* Latest decoded frame + a flag set by OnFrame, consumed by Tasks. Both run in
 * main-loop context (the RX callback fires synchronously inside TC6_Service),
 * so no locking is needed. */
static volatile bool    s_new_frame;
static uint8_t          s_seq, s_energy, s_bass, s_treble, s_kick, s_flags;
static uint32_t         s_frame_count;
static uint32_t         s_last_frame_ms;
static bool             s_parked;       /* neck currently held at neutral */
static bool             s_enabled;      /* default off; enabled via CLI / 0x88B9 */
static int8_t           s_neck_pos;     /* last neck position applied */

static int8_t angle_to_pos(uint16_t angle)
{
    if (angle < NOD_ANGLE_MIN) { angle = NOD_ANGLE_MIN; }
    if (angle > NOD_ANGLE_MAX) { angle = NOD_ANGLE_MAX; }
    return (int8_t)-((int32_t)(angle - NOD_ANGLE_MIN) * (int32_t)SERVO_POS_MAX
                     / (int32_t)(NOD_ANGLE_MAX - NOD_ANGLE_MIN));
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
         * manual/marvin command. */
        uint32_t now = SYSTICK_GetTickCounter();
        if (s_enabled && !s_parked && ((now - s_last_frame_ms) >= IDLE_PARK_MS)) {
            s_neck_pos = Servo_SetPosition(SERVO_NECK, SERVO_POS_NEUTRAL);
            s_parked = true;
        }
        return;
    }
    s_new_frame = false;
    s_last_frame_ms = SYSTICK_GetTickCounter();
    s_parked = false;

    /* Beat strengths from the flags: 2 = strong (BIG set), 1 = onset. */
    uint8_t bass_beat = (s_flags & BEAT_FLAG_BASS) ? ((s_flags & BEAT_FLAG_BIG) ? 2u : 1u) : 0u;
    uint8_t full_beat = (s_flags & BEAT_FLAG_MID)  ? ((s_flags & BEAT_FLAG_BIG) ? 2u : 1u) : 0u;

    /* Lift the 0-255 energy field into the engine's 0-10000 loudness domain so
     * its silence/comeback thresholds hold (255 * 39 = 9945). */
    uint16_t raw_env = (uint16_t)s_energy * 39u;

    NodEngine_Frame(bass_beat, full_beat, raw_env);

    int8_t pos = angle_to_pos(NodEngine_GetTargetAngle());
    if (s_enabled) {
        s_neck_pos = Servo_SetPosition(SERVO_NECK, pos);
    } else {
        s_neck_pos = pos;   /* track for diagnostics, but don't drive the servo */
    }
}

void BeatNod_SetEnabled(bool en)
{
    s_enabled = en;
    if (!en) {
        (void)Servo_SetPosition(SERVO_NECK, SERVO_POS_NEUTRAL);
        s_parked = true;
    }
}

bool BeatNod_IsEnabled(void) { return s_enabled; }

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
