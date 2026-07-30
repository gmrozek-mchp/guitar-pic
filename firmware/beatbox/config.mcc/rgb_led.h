#ifndef RGB_LED_H
#define RGB_LED_H

#include <xc.h>
#include <stdint.h>

#define RGB_PWM_PERIOD      3125U   // 1 kHz at Fcy/64
#define RGB_MAX_DUTY        3125U   // full brightness

void RGB_LED_Initialize(void);
void RGB_LED_Set(uint16_t red, uint16_t green, uint16_t blue);
void RGB_LED_BootTest(void);

#endif
