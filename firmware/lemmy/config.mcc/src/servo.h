#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>

/* Raw R/C servo PWM on TCC0 (50 Hz / 20 ms frame). Two channels: neck
 * (TCC0_WO0 / PA16) and jaw (TCC0_WO1 / PA17). This is the raw pulse-width
 * interface; puppet-relative positioning and calibration sit above it.
 *
 * Pulse width is clamped to [SERVO_US_MIN, SERVO_US_MAX] so a command never
 * drives a servo past its mechanical stops. SERVO_US_CENTER is ~neutral. */

typedef enum
{
    SERVO_NECK = 0,   /* TCC0_WO0 / PA16 */
    SERVO_JAW  = 1,   /* TCC0_WO1 / PA17 */
    SERVO_COUNT
} servo_id_t;

#define SERVO_US_MIN     500u
#define SERVO_US_MAX     2500u
#define SERVO_US_CENTER  1500u

/* Start TCC0 PWM and park both servos at center. Call after SYS_Initialize. */
void Servo_Initialize(void);

/* Command a raw pulse width (microseconds), clamped to [MIN, MAX]. Returns the
 * value actually applied after clamping. */
uint16_t Servo_SetPulseUs(servo_id_t servo, uint16_t us);

/* Last pulse width commanded to a servo (microseconds). */
uint16_t Servo_GetPulseUs(servo_id_t servo);

#endif /* SERVO_H */
