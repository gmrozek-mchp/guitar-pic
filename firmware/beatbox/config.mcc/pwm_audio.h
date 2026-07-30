#ifndef PWM_AUDIO_H
#define PWM_AUDIO_H

#include <xc.h>
#include <stdint.h>

#define PWM_GAIN    16650.0f
#define PWM_OFFS    33325
#define PWM_MPER    66649

void PWM_Audio_Initialize(void);
void PWM_Audio_SetDuty(float left, float right);

#endif
