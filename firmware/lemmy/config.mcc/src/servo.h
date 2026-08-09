#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>
#include <stdbool.h>

/* R/C servo control on TCC0 (50 Hz / 20 ms frame). Two channels: neck
 * (TCC0_WO0 / PA16) and jaw (TCC0_WO1 / PA17).
 *
 * Two layers:
 *   - raw pulse width  -> Servo_SetPulseUs, clamped to [SERVO_US_MIN, MAX].
 *   - puppet position  -> Servo_SetPosition, a signed 8-bit value
 *     (SERVO_POS_MIN..MAX) mapped through per-servo calibration. 0 = neutral,
 *     +MAX = max endpoint, -MIN = min endpoint; travel either side of neutral
 *     may be asymmetric. `invert` flips the sign for a reversed-mounted servo.
 *     The range matches the int8_t position byte carried on the T1S command
 *     plane, so a received command byte applies 1:1 with no scaling.
 *
 * Calibration is a compiled-in default (see servo.c) copied to a RAM working
 * copy at init; the `cal` CLI tunes the working copy live and prints it back in
 * paste-ready form to fold into the default and reflash. The PL10 has no NVM,
 * so calibration is not persisted on-device. */

typedef enum
{
    SERVO_NECK = 0,   /* TCC0_WO0 / PA16 */
    SERVO_JAW  = 1,   /* TCC0_WO1 / PA17 */
    SERVO_COUNT
} servo_id_t;

/* Absolute hardware pulse-width guard rails (µs). */
#define SERVO_US_MIN      500u
#define SERVO_US_MAX      2500u
#define SERVO_US_CENTER   1500u

/* Position range fed to Servo_SetPosition — a signed 8-bit value, matching the
 * T1S command-plane byte (int8_t -127..127; 0 = neutral). */
#define SERVO_POS_MIN     (-127)
#define SERVO_POS_NEUTRAL (0)
#define SERVO_POS_MAX     (127)

typedef struct
{
    uint16_t min_us;      /* pulse at SERVO_POS_MIN */
    uint16_t neutral_us;  /* pulse at SERVO_POS_NEUTRAL */
    uint16_t max_us;      /* pulse at SERVO_POS_MAX */
    bool     invert;      /* flip position sign (servo mounted reversed) */
} servo_cal_t;

/* Start TCC0 PWM, load default calibration, park both servos at neutral. */
void Servo_Initialize(void);

/* Output gate, at the single hardware-write point — so it holds against every
 * motion source (beat nod, the coordinator's and beatbox's T1S positions, the local
 * CLI) rather than just one of them. Requested pulses/positions keep being recorded
 * while gated, and are re-applied on enable. Disabling parks both servos at neutral
 * first. Defaults enabled; the coordinator sets it over 0x88B9 and reconciles
 * against the heartbeat, so a local change is transient while marvin is present. */
void Servo_SetEnabled(bool en);
bool Servo_IsEnabled(void);

/* --- raw pulse layer --- */
uint16_t Servo_SetPulseUs(servo_id_t servo, uint16_t us);  /* clamped; returns applied */
uint16_t Servo_GetPulseUs(servo_id_t servo);

/* --- position layer (mapped through calibration) --- */
int8_t Servo_SetPosition(servo_id_t servo, int8_t pos);  /* clamped; returns applied */
int8_t Servo_GetPosition(servo_id_t servo);

/* --- calibration (RAM working copy) --- */
servo_cal_t Servo_GetCal(servo_id_t servo);
void        Servo_SetCal(servo_id_t servo, servo_cal_t cal);  /* endpoints clamped to guard rails */

#endif /* SERVO_H */
