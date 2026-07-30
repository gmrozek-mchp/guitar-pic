#include <xc.h>
#include "rgb_led.h"

// RGB LED on dsPIC33AK256MPS306 Curiosity Board:
//   Green = SCCP1/OCM1 -> RA10/RP11 (PPS code 29)
//   Red   = SCCP2/OCM2 -> RA2/RP3   (PPS code 30)
//   Blue  = SCCP3/OCM3 -> RC0/RP33  (PPS code 31)

#define SCCP_MODE_PWM   0x05    // Edge-aligned PWM
#define PRESCALE_64     0x03    // 1:64

void RGB_LED_Initialize(void)
{
    // SCCP1 — Green (RA10)
    CCP1CON1 = 0;
    CCP1CON1bits.MOD = SCCP_MODE_PWM;
    CCP1CON1bits.TMRPS = PRESCALE_64;
    CCP1CON1bits.CLKSEL = 0;       // Fcy
    CCP1CON2 = 0;
    CCP1CON2bits.OCAEN = 1;
    CCP1CON3 = 0;
    CCP1PR = RGB_PWM_PERIOD;
    CCP1RA = 0;
    CCP1RB = 0;
    CCP1TMR = 0;
    CCP1CON1bits.ON = 1;

    // SCCP2 — Red (RA2)
    CCP2CON1 = 0;
    CCP2CON1bits.MOD = SCCP_MODE_PWM;
    CCP2CON1bits.TMRPS = PRESCALE_64;
    CCP2CON1bits.CLKSEL = 0;
    CCP2CON2 = 0;
    CCP2CON2bits.OCAEN = 1;
    CCP2CON3 = 0;
    CCP2PR = RGB_PWM_PERIOD;
    CCP2RA = 0;
    CCP2RB = 0;
    CCP2TMR = 0;
    CCP2CON1bits.ON = 1;

    // SCCP3 — Blue (RC0)
    CCP3CON1 = 0;
    CCP3CON1bits.MOD = SCCP_MODE_PWM;
    CCP3CON1bits.TMRPS = PRESCALE_64;
    CCP3CON1bits.CLKSEL = 0;
    CCP3CON2 = 0;
    CCP3CON2bits.OCAEN = 1;
    CCP3CON3 = 0;
    CCP3PR = RGB_PWM_PERIOD;
    CCP3RA = 0;
    CCP3RB = 0;
    CCP3TMR = 0;
    CCP3CON1bits.ON = 1;
}

void RGB_LED_Set(uint16_t red, uint16_t green, uint16_t blue)
{
    CCP2RB = red;
    CCP1RB = green;
    CCP3RB = blue;
}

void RGB_LED_BootTest(void)
{
    volatile uint32_t i;
    uint16_t brightness = RGB_PWM_PERIOD / 4;  // 25% brightness

    // Red — 1 second
    RGB_LED_Set(brightness, 0, 0);
    for (i = 0; i < 8000000; i++) {}

    // Green — 1 second
    RGB_LED_Set(0, brightness, 0);
    for (i = 0; i < 8000000; i++) {}

    // Off
    RGB_LED_Set(0, 0, 0);
}
