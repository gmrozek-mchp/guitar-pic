#include "rgb_led.h"

#include <xc.h>

#include "../mcc_generated_files/pwm/sccp1.h"
#include "../mcc_generated_files/pwm/sccp2.h"
#include "../mcc_generated_files/pwm/sccp3.h"

/* MCC's SCCPn_PWM_Initialize (run from SYSTEM_Initialize) sets the PWM mode,
 * enables the OCM output, and routes the pins, but leaves a 1:1 prescale +
 * 0xFFFF period that don't suit the LED on this 100 MHz clock tree.
 * RGB_LED_Initialize overrides just the prescale and period (module off during
 * the change) for 1 kHz at Fcy/16. Duty (CCPxRB) is buffered — it takes effect
 * at the next period. Channel -> colour: SCCP1 = red, SCCP2 = green,
 * SCCP3 = blue (per EV74H48A wiring). */

#define SCCP_PRESCALE_16  (0x02u)   /* TMRPS<1:0>: 1:16 */

static uint16_t scale(uint8_t level)
{
    return (uint16_t)(((uint32_t)level * RGB_PWM_PERIOD) / 255u);
}

void RGB_LED_Initialize(void)
{
    SCCP1_PWM_Disable();
    SCCP2_PWM_Disable();
    SCCP3_PWM_Disable();

    CCP1CON1bits.TMRPS = SCCP_PRESCALE_16;
    CCP2CON1bits.TMRPS = SCCP_PRESCALE_16;
    CCP3CON1bits.TMRPS = SCCP_PRESCALE_16;

    SCCP1_PWM_PeriodSet(RGB_PWM_PERIOD);
    SCCP2_PWM_PeriodSet(RGB_PWM_PERIOD);
    SCCP3_PWM_PeriodSet(RGB_PWM_PERIOD);

    RGB_LED_Off();

    SCCP1_PWM_Enable();
    SCCP2_PWM_Enable();
    SCCP3_PWM_Enable();
}

void RGB_LED_Set(uint8_t red, uint8_t green, uint8_t blue)
{
    SCCP1_PWM_DutyCycleSet(scale(red));     /* SCCP1 = red   */
    SCCP2_PWM_DutyCycleSet(scale(green));   /* SCCP2 = green */
    SCCP3_PWM_DutyCycleSet(scale(blue));    /* SCCP3 = blue  */
}

void RGB_LED_Off(void)
{
    SCCP1_PWM_DutyCycleSet(0u);
    SCCP2_PWM_DutyCycleSet(0u);
    SCCP3_PWM_DutyCycleSet(0u);
}
