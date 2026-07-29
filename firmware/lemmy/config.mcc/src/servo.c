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

static uint16_t s_pulse_us[SERVO_COUNT];

static uint16_t us_to_ticks(uint16_t us)
{
    return (uint16_t)(((uint32_t)us * SERVO_TICKS_NUM) / SERVO_TICKS_DEN);
}

void Servo_Initialize(void)
{
    TCC0_PWMStart();
    for (servo_id_t s = 0; s < SERVO_COUNT; s++)
    {
        (void)Servo_SetPulseUs(s, SERVO_US_CENTER);
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
    (void)TCC0_PWM16bitDutySet(s_channel[servo], us_to_ticks(us));
    return us;
}

uint16_t Servo_GetPulseUs(servo_id_t servo)
{
    return (servo < SERVO_COUNT) ? s_pulse_us[servo] : 0u;
}
