#include "beat_jaw.h"

#include "servo.h"

/* Poses, in Servo_SetPosition units — negative opens the jaw. */
#define JAW_REST_POS      (-50)
#define JAW_YELL_POS      (-100)
#define JAW_CLOSED_POS    (0)

/* Beat frames arrive at ~23.4 Hz, so frame counts double as time. */
#define JAW_FPS           (23u)

/* Slew rate, counts per frame. The yell snaps open (a mouth flying open on a big
 * hit); everything else moves at the ordinary rate, ~160 counts/s, so the full
 * open-to-shut sweep takes about two thirds of a second. */
#define JAW_STEP          (7)
#define JAW_SNAP_STEP     (40)

#define JAW_YELL_HOLD_MIN_F   ((3u * JAW_FPS) / 2u)   /* 1.5 .. 3.0 s wide open   */
#define JAW_YELL_HOLD_SPAN_F  ((3u * JAW_FPS) / 2u)
#define JAW_YELL_WAIT_MIN_F   (6u * JAW_FPS)          /* 6 .. 14 s between yells  */
#define JAW_YELL_WAIT_SPAN_F  (8u * JAW_FPS)

#define JAW_CLOSE_HOLD_MIN_F  (1u * JAW_FPS)          /* 1.0 .. 2.5 s shut        */
#define JAW_CLOSE_HOLD_SPAN_F ((3u * JAW_FPS) / 2u)
#define JAW_CLOSE_WAIT_MIN_F  (12u * JAW_FPS)         /* 12 .. 28 s between       */
#define JAW_CLOSE_WAIT_SPAN_F (16u * JAW_FPS)

static beat_jaw_state_t s_state;
static uint16_t         s_hold;        /* frames left in the current pose        */
static uint16_t         s_yell_wait;   /* frames until a yell is allowed again   */
static uint16_t         s_close_wait;
static int8_t           s_pos;         /* last position applied                  */
static bool             s_parked;
static uint32_t         s_yells, s_closes;

/* Pose timing jitter, stirred with the frame's energy byte so the sequence isn't
 * identical every power-up. */
static uint16_t         s_rng = 0x5EEDu;
static uint8_t          s_energy;

static uint16_t rng_next(void)
{
    s_rng ^= s_energy;
    if (s_rng == 0u) { s_rng = 0x5EEDu; }   /* an all-zero LFSR never leaves 0 */
    uint16_t bit = ((s_rng >> 0) ^ (s_rng >> 2) ^ (s_rng >> 3) ^ (s_rng >> 5)) & 1u;
    s_rng = (uint16_t)((s_rng >> 1) | (uint16_t)(bit << 15));
    return s_rng;
}

static int8_t slew_to(int8_t cur, int8_t target, int step)
{
    int d = (int)target - (int)cur;
    if (d >  step) { d =  step; }
    if (d < -step) { d = -step; }
    return (int8_t)((int)cur + d);
}

void BeatJaw_Initialize(void)
{
    s_state      = BEAT_JAW_REST;
    s_hold       = 0u;
    /* Both start their wait served-but-not-expired, so he doesn't open his mouth
     * in the first frame after the music starts. */
    s_yell_wait  = JAW_YELL_WAIT_MIN_F;
    s_close_wait = JAW_CLOSE_WAIT_MIN_F;
    s_pos        = Servo_SetPosition(SERVO_JAW, JAW_REST_POS);
    s_parked     = true;
}

void BeatJaw_Frame(bool nodding, bool big, uint8_t energy)
{
    s_energy = energy;
    s_parked = false;

    if (s_yell_wait  > 0u) { s_yell_wait--; }
    if (s_close_wait > 0u) { s_close_wait--; }

    if (s_hold > 0u) {
        s_hold--;
    } else if (s_state != BEAT_JAW_REST) {
        s_state = BEAT_JAW_REST;   /* pose served its time */
    } else if (nodding && big && (s_yell_wait == 0u)) {
        /* A yell wants a strong beat to hang on, and only makes sense while the head
         * is actually banging. */
        s_state     = BEAT_JAW_YELL;
        s_hold      = JAW_YELL_HOLD_MIN_F + (uint16_t)(rng_next() % JAW_YELL_HOLD_SPAN_F);
        s_yell_wait = JAW_YELL_WAIT_MIN_F + (uint16_t)(rng_next() % JAW_YELL_WAIT_SPAN_F);
        s_yells++;
    } else if (s_close_wait == 0u) {
        /* Shutting his mouth needs no excuse and happens between bursts too. */
        s_state      = BEAT_JAW_CLOSED;
        s_hold       = JAW_CLOSE_HOLD_MIN_F + (uint16_t)(rng_next() % JAW_CLOSE_HOLD_SPAN_F);
        s_close_wait = JAW_CLOSE_WAIT_MIN_F + (uint16_t)(rng_next() % JAW_CLOSE_WAIT_SPAN_F);
        s_closes++;
    }

    int8_t target = (s_state == BEAT_JAW_YELL)   ? (int8_t)JAW_YELL_POS
                  : (s_state == BEAT_JAW_CLOSED) ? (int8_t)JAW_CLOSED_POS
                                                 : (int8_t)JAW_REST_POS;
    int step = (s_state == BEAT_JAW_YELL) ? JAW_SNAP_STEP : JAW_STEP;

    s_pos = Servo_SetPosition(SERVO_JAW, slew_to(s_pos, target, step));
}

void BeatJaw_Park(void)
{
    if (s_parked) { return; }
    s_state  = BEAT_JAW_REST;
    s_hold   = 0u;
    s_pos    = Servo_SetPosition(SERVO_JAW, JAW_REST_POS);
    s_parked = true;
}

beat_jaw_state_t BeatJaw_State(void)  { return s_state; }
int8_t           BeatJaw_Position(void) { return s_pos; }
uint16_t         BeatJaw_HoldMs(void) { return (uint16_t)((uint32_t)s_hold * 1000u / JAW_FPS); }
uint32_t         BeatJaw_Yells(void)  { return s_yells; }
uint32_t         BeatJaw_Closes(void) { return s_closes; }
