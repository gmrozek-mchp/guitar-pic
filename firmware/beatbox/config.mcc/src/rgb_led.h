#ifndef RGB_LED_H
#define RGB_LED_H

#include <stdint.h>

/* Onboard RGB LED, driven by three SCCP channels in edge-aligned PWM. MCC's
 * generated SCCPn_PWM_Initialize sets the mode/output/pins; RGB_LED_Initialize
 * overrides only the prescale + period MCC leaves wrong on this clock tree.
 * Call it once after SYSTEM_Initialize. Channel -> colour -> OCM pin:
 * SCCP1 = red (RD9), SCCP2 = green (RD0), SCCP3 = blue (RD2). Levels are 8-bit
 * (0 = off, 255 = full) and scaled to the PWM period. */

#define RGB_PWM_PERIOD  (6250u)   /* 1 kHz at Fcy/16 (100 MHz / 16 / 6250) */

void RGB_LED_Initialize(void);
void RGB_LED_Set(uint8_t red, uint8_t green, uint8_t blue);
void RGB_LED_Off(void);

#endif /* RGB_LED_H */
