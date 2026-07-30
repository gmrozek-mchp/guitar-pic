#include <xc.h>
#include <stdint.h>
#include "adc_audio.h"
#include "pwm_audio.h"
#include "beat_detect.h"

// DC-blocking high-pass filter (1st order, ~20 Hz at 48 kHz)
typedef struct {
    float a;
    float x_prev;
    float y_prev;
} HighPass1Pole;

static HighPass1Pole dc_block_left, dc_block_right;

static void hp_init(HighPass1Pole *hp, float cutoff_hz, float sample_rate)
{
    float x = 6.2831853f * cutoff_hz;
    hp->a = sample_rate / (sample_rate + x);
    hp->x_prev = 0.0f;
    hp->y_prev = 0.0f;
}

static inline float hp_process(HighPass1Pole *hp, float x)
{
    float y = hp->a * (hp->y_prev + x - hp->x_prev);
    hp->x_prev = x;
    hp->y_prev = y;
    return y;
}

void ADC_Audio_Initialize(void)
{
    hp_init(&dc_block_left, 20.0f, 48000.0f);
    hp_init(&dc_block_right, 20.0f, 48000.0f);

    // ADC2 CH6: Left audio input (AD2AN3 on RB3)
    AD2CH6CON1bits.PINSEL = 3;
    AD2CH6CON1bits.SAMC = 0;
    AD2CH6CON1bits.TRG1SRC = 8;     // PG3 ADC trigger 1
    AD2CH6CON1bits.TRG2SRC = 2;     // Immediate retrigger for oversampling
    AD2CH6CON1bits.IRQSEL = 1;
    AD2CH6CON1bits.MODE = 3;        // Oversampling mode
    AD2CH6CON1bits.ACCNUM = 3;      // 256x oversampling

    // ADC2 CH7: Right audio input (AD2AN4 on RB4)
    AD2CH7CON1bits.PINSEL = 4;
    AD2CH7CON1bits.SAMC = 0;
    AD2CH7CON1bits.TRG1SRC = 8;     // PG3 ADC trigger 1
    AD2CH7CON1bits.TRG2SRC = 2;     // Immediate retrigger for oversampling
    AD2CH7CON1bits.IRQSEL = 1;
    AD2CH7CON1bits.MODE = 3;        // Oversampling mode
    AD2CH7CON1bits.ACCNUM = 3;      // 256x oversampling

    // High priority for audio ISR
    IPC24bits.AD2CH7IP = 6;

    // Enable CH7 interrupt (fires after both channels complete)
    IEC6bits.AD2CH7IE = 1;

    // Power up ADC2
    AD2CONbits.ON = 1U;
    while (AD2CONbits.ADRDY == 0U) {}
}

// 48 kHz ISR — audio pass-through + beat detection
void __attribute__((interrupt, no_auto_psv)) _AD2CH7Interrupt(void)
{
    float left, right, mono;

    // 256x oversampled: 16-bit unsigned (0-65535), normalize to +/-1.0
    left = ((float)(AD2CH6DATA) - 32768.0f) / 32768.0f;
    right = ((float)(AD2CH7DATA) - 32768.0f) / 32768.0f;

    // DC-blocking high-pass filter
    left = hp_process(&dc_block_left, left);
    right = hp_process(&dc_block_right, right);

    // Audio pass-through to headphones
    PWM_Audio_SetDuty(left, right);

    // Beat detection on summed mono
    mono = (left + right) * 0.5f;
    BeatDetect_Process(mono);

    IFS6bits.AD2CH7IF = 0U;
}
