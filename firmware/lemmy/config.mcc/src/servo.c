#include "servo.h"

#include "definitions.h"   /* TCC0_* */

/* TCC0 runs at 1.5 MHz (24 MHz / DIV16), PER = 29999 → 20 ms frame.
 * 1 us = 1.5 ticks, i.e. ticks = us * 3 / 2. */
#define SERVO_TICKS_NUM  3u
#define SERVO_TICKS_DEN  2u

static const TCC0_CHANNEL_NUM s_channel[SERVO_COUNT] =
{
    [SERVO_NECK] = TCC0_CHANNEL0,
    [SERVO_JAW]  = TCC0_CHANNEL1,
};

/* Compiled-in calibration defaults. Tune live with the `cal` CLI, then paste
 * the printed values back here and reflash. */
static const servo_cal_t s_cal_default[SERVO_COUNT] =
{
    [SERVO_NECK] = { .min_us = 1000u, .neutral_us = 1450u, .max_us = 2000u, .invert = true },
    [SERVO_JAW]  = { .min_us = 1000u, .neutral_us = 1600u, .max_us = 1600u, .invert = false },
};

static servo_cal_t s_cal[SERVO_COUNT];       /* RAM working copy */
static uint16_t    s_pulse_us[SERVO_COUNT];
static int8_t      s_position[SERVO_COUNT];

/* Output gate. While clear, requested pulses/positions are still recorded but the
 * TCC duty is left alone, so nothing moves whatever asked — the beat nod, the
 * coordinator's 0x88B5 positions, beatbox's positions, or the local CLI. Default on
 * so the puppet works with no coordinator; marvin pushes its own state over
 * 0x88B9. */
static bool        s_output_en = true;

static uint16_t us_to_ticks(uint16_t us)
{
    return (uint16_t)(((uint32_t)us * SERVO_TICKS_NUM) / SERVO_TICKS_DEN);
}

void Servo_Initialize(void)
{
    TCC0_PWMStart();
    for (servo_id_t s = 0; s < SERVO_COUNT; s++)
    {
        s_cal[s] = s_cal_default[s];
        (void)Servo_SetPosition(s, SERVO_POS_NEUTRAL);
    }
}

uint16_t Servo_SetPulseUs(servo_id_t servo, uint16_t us)
{
    if (servo >= SERVO_COUNT)
    {
        return 0u;
    }
    if (us < SERVO_US_MIN) { us = SERVO_US_MIN; }
    if (us > SERVO_US_MAX) { us = SERVO_US_MAX; }

    s_pulse_us[servo] = us;
    if (s_output_en)
    {
        (void)TCC0_PWM16bitDutySet(s_channel[servo], us_to_ticks(us));
    }
    return us;
}

void Servo_SetEnabled(bool en)
{
    if (en == s_output_en) { return; }

    if (en)
    {
        /* Re-apply what was requested while gated, so the puppet resumes where the
         * motion source already thinks it is. */
        s_output_en = true;
        for (servo_id_t s = 0; s < SERVO_COUNT; s++)
        {
            (void)TCC0_PWM16bitDutySet(s_channel[s], us_to_ticks(s_pulse_us[s]));
        }
    }
    else
    {
        /* Park at neutral before gating, so the head settles rather than freezing
         * mid-nod (the same courtesy BeatNod_SetEnabled does for the neck). */
        for (servo_id_t s = 0; s < SERVO_COUNT; s++)
        {
            (void)TCC0_PWM16bitDutySet(s_channel[s], us_to_ticks(s_cal[s].neutral_us));
        }
        s_output_en = false;
    }
}

bool Servo_IsEnabled(void)
{
    return s_output_en;
}

uint16_t Servo_GetPulseUs(servo_id_t servo)
{
    return (servo < SERVO_COUNT) ? s_pulse_us[servo] : 0u;
}

int8_t Servo_SetPosition(servo_id_t servo, int8_t pos)
{
    if (servo >= SERVO_COUNT)
    {
        return 0;
    }
    if (pos > SERVO_POS_MAX) { pos = SERVO_POS_MAX; }
    if (pos < SERVO_POS_MIN) { pos = SERVO_POS_MIN; }
    s_position[servo] = pos;

    const servo_cal_t *c = &s_cal[servo];
    int32_t mapped = c->invert ? -(int32_t)pos : (int32_t)pos;

    int32_t us;
    if (mapped >= 0)
    {
        us = (int32_t)c->neutral_us
           + (((int32_t)c->max_us - (int32_t)c->neutral_us) * mapped) / SERVO_POS_MAX;
    }
    else
    {
        us = (int32_t)c->neutral_us
           - (((int32_t)c->neutral_us - (int32_t)c->min_us) * (-mapped)) / (-SERVO_POS_MIN);
    }

    (void)Servo_SetPulseUs(servo, (uint16_t)us);
    return pos;
}

int8_t Servo_GetPosition(servo_id_t servo)
{
    return (servo < SERVO_COUNT) ? s_position[servo] : 0;
}

servo_cal_t Servo_GetCal(servo_id_t servo)
{
    if (servo >= SERVO_COUNT)
    {
        servo_cal_t empty = { 0u, 0u, 0u, false };
        return empty;
    }
    return s_cal[servo];
}

void Servo_SetCal(servo_id_t servo, servo_cal_t cal)
{
    if (servo >= SERVO_COUNT)
    {
        return;
    }
    /* Keep every endpoint inside the hardware guard rails. */
    if (cal.min_us     < SERVO_US_MIN) { cal.min_us     = SERVO_US_MIN; }
    if (cal.min_us     > SERVO_US_MAX) { cal.min_us     = SERVO_US_MAX; }
    if (cal.max_us     < SERVO_US_MIN) { cal.max_us     = SERVO_US_MIN; }
    if (cal.max_us     > SERVO_US_MAX) { cal.max_us     = SERVO_US_MAX; }
    if (cal.neutral_us < SERVO_US_MIN) { cal.neutral_us = SERVO_US_MIN; }
    if (cal.neutral_us > SERVO_US_MAX) { cal.neutral_us = SERVO_US_MAX; }

    s_cal[servo] = cal;
    (void)Servo_SetPosition(servo, s_position[servo]);   /* re-apply under new cal */
}
