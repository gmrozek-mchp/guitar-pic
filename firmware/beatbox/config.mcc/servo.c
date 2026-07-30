#include <xc.h>
#include "servo.h"

// SCCP4 in edge-aligned PWM mode, output on RA9/RP10 via PPS (OCM4, code 32)
// Peripheral clock = 100 MHz (Fcy/2), prescaler 1:64 -> 1.5625 MHz tick (640ns)
// Period = 31250 ticks = 20ms = 50 Hz

#define SCCP_MODE_PWM   0x05
#define PRESCALE_64     0x03

static uint16_t servo_last_pulse = 0xFFFF;

static void servo_write(uint16_t pulse)
{
    if (pulse == servo_last_pulse) return;
    servo_last_pulse = pulse;
    CCP4RB = pulse;
}

void Servo_Initialize(void)
{
    CCP4CON1 = 0;
    CCP4CON1bits.MOD = SCCP_MODE_PWM;
    CCP4CON1bits.TMRPS = PRESCALE_64;
    CCP4CON1bits.CLKSEL = 0;
    CCP4CON2 = 0;
    CCP4CON2bits.OCAEN = 1;
    CCP4CON3 = 0;
    CCP4PR = SERVO_PWM_PERIOD;
    CCP4RA = 0;
    CCP4RB = SERVO_MIN_PULSE + (uint32_t)SERVO_HEAD_UP_DEG * (SERVO_MAX_PULSE - SERVO_MIN_PULSE) / 180;
    CCP4TMR = 0;
    CCP4CON1bits.ON = 1;
}

void Servo_SetPosition(uint16_t phase)
{
    // Test mode: phase 0-1000 -> full 0-180 degrees
    if (phase > 1000) phase = 1000;
    uint32_t pulse = SERVO_MIN_PULSE +
        ((uint32_t)(SERVO_MAX_PULSE - SERVO_MIN_PULSE) * phase) / 1000;
    servo_write((uint16_t)pulse);
}

void Servo_SetAngle(uint16_t degrees_x10)
{
    // Set servo to exact angle (in tenths of a degree for precision)
    // e.g. 170 = 17.0 degrees, 550 = 55.0 degrees
    if (degrees_x10 > 1800) degrees_x10 = 1800;
    uint32_t pulse = SERVO_MIN_PULSE +
        ((uint32_t)(SERVO_MAX_PULSE - SERVO_MIN_PULSE) * degrees_x10) / 1800;
    servo_write((uint16_t)pulse);
}
