#include <xc.h>
#include "pwm_audio.h"

void PWM_Audio_Initialize(void)
{
    // Select CLK Gen 5 (800 MHz) for PWM module
    PCLKCONbits.MCLKSEL = 1;

    // Master period: 800 MHz effective (with 16x HREN) / 192 kHz = 66649 counts
    MPER = PWM_MPER;
    MPHASE = 0;

    // PG3: Right channel output (PWM3H on RB9 via PPS)
    // Independent Edge, Master clock, HREN enabled, MPERSEL enabled
    PG3CON = 0x40000088UL;
    PG3DC = PWM_OFFS;
    // PPSEN=1, PENH=1 (high-side output only for audio)
    PG3IOCON1 = 0x218UL;
    PG3IOCON2 = 0x800UL;

    // PG4: Left channel output (PWM4H on RB8 via PPS)
    PG4CON = 0x40000088UL;
    PG4CONbits.SOCS = 3;       // Triggered from PG3 SOC
    PG4DC = PWM_OFFS;
    PG4IOCON1 = 0x218UL;
    PG4IOCON2 = 0x800UL;

    // ADC trigger from PG3
    PG3EVT1 = 0;
    PG3EVT2 = 0;
    PG3TRIGA = 0;
    PG3EVT1bits.PGTRGSEL = 1;  // PG3TRIGA is ADC trigger source
    PG3EVT1bits.ADTR1PS = 3;   // Postscale 4:1 (192 kHz / 4 = 48 kHz ADC rate)
    PG3EVT1bits.ADTR1EN1 = 1;  // Enable ADC trigger output

    // PG3 self-triggered (free-running)
    PG3CONbits.SOCS = 0;

    // Clear PWM interrupt flags
    IFS1bits.PWM3IF = 0;
    IFS1bits.PWM4IF = 0;

    // Enable PWM generators
    PG3CONbits.ON = 1;
    PG4CONbits.ON = 1;
}

void PWM_Audio_SetDuty(float left, float right)
{
    int32_t l_duty, r_duty;

    if (left > 0.9f) left = 0.9f;
    else if (left < -0.9f) left = -0.9f;
    if (right > 0.9f) right = 0.9f;
    else if (right < -0.9f) right = -0.9f;

    l_duty = (int32_t)(left * PWM_GAIN) + PWM_OFFS;
    r_duty = (int32_t)(right * PWM_GAIN) + PWM_OFFS;

    PG4DC = (uint32_t)l_duty & 0x000FFFFFUL;
    PG3DC = (uint32_t)r_duty & 0x000FFFFFUL;
    PG4STATbits.UPDREQ = 1;
    PG3STATbits.UPDREQ = 1;
}
