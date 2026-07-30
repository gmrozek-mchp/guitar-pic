#ifndef SERVO_H
#define SERVO_H

#include <xc.h>
#include <stdint.h>

// RC servo PWM: 50 Hz (20ms period), pulse 500-2500us
// SCCP4 peripheral clock = 100 MHz (Fcy/2 standard peripheral bus)
// Prescaler 1:64 -> tick = 640ns
// Period = 20ms / 640ns = 31250 ticks
// 500us = 781 ticks, 2500us = 3906 ticks
#define SERVO_PWM_PERIOD    31250U
#define SERVO_MIN_PULSE     781U    // 500us (0 degrees)
#define SERVO_MAX_PULSE     3906U   // 2500us (180 degrees)

// Beat-sync motion limits (degrees)
#define SERVO_HEAD_UP_DEG       17      // neutral / head up position
#define SERVO_NORMAL_NOD_DEG    40      // normal beat max tilt
#define SERVO_BIG_NOD_DEG       55      // big beat max tilt (hard limit)

void Servo_Initialize(void);
void Servo_SetPosition(uint16_t phase);         // phase 0-1000 -> full 0-180 (test mode)
void Servo_SetAngle(uint16_t degrees_x10);      // angle in tenths of degree (170 = 17.0°)

#endif
