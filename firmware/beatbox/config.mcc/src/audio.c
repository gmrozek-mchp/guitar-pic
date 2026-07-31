#include "audio.h"

#include <stddef.h>

#include "../mcc_generated_files/adc/adc4.h"
#include "../mcc_generated_files/pwm_hs/pwm.h"

/* PWM_HS master period (MPER) is 66651 counts. Idle both channels at mid-scale
 * and swing the normalized sample about it; the gain keeps the swing inside the
 * rails. */
#define AUDIO_PWM_CENTER  (33325u)
#define AUDIO_PWM_GAIN    (16650.0f)

static volatile uint16_t s_last_left  = 0u;
static volatile uint16_t s_last_right = 0u;

/* Peak envelope since the last Audio_GetPeaks read. */
static volatile uint16_t s_min_left  = 0xFFFFu;
static volatile uint16_t s_max_left  = 0u;
static volatile uint16_t s_min_right = 0xFFFFu;
static volatile uint16_t s_max_right = 0u;

/* 256x oversampled result is unsigned 0..65535 centred on 32768. */
static inline float normalize(uint16_t raw)
{
    return ((float)raw - 32768.0f) / 32768.0f;
}

static inline uint32_t to_duty(float sample)
{
    if (sample > 1.0f)
    {
        sample = 1.0f;
    }
    else if (sample < -1.0f)
    {
        sample = -1.0f;
    }
    return (uint32_t)((int32_t)(sample * AUDIO_PWM_GAIN) + (int32_t)AUDIO_PWM_CENTER);
}

/* Registered on ADC4; fires from _AD4CH1Interrupt at the 48 kHz sample rate.
 * Only CH1 (right) raises the interrupt, and it does so once both channels of
 * the pair have converted, so CH0 (left) is already latched. */
static void audio_on_sample(const enum ADC4_CHANNEL channel, uint16_t adc_val)
{
    if (channel != ADC_AUDIO_R)
    {
        return;
    }

    uint16_t raw_left = (uint16_t)ADC4_ConversionResultGet(ADC_AUDIO_L);
    s_last_left  = raw_left;
    s_last_right = adc_val;

    if (raw_left < s_min_left)  { s_min_left  = raw_left; }
    if (raw_left > s_max_left)  { s_max_left  = raw_left; }
    if (adc_val  < s_min_right) { s_min_right = adc_val; }
    if (adc_val  > s_max_right) { s_max_right = adc_val; }

    PWM_DutyCycleSet(PWM_GENERATOR_1, to_duty(normalize(raw_left)));   /* RB8 */
    PWM_DutyCycleSet(PWM_GENERATOR_2, to_duty(normalize(adc_val)));    /* RB9 */
    PWM_SoftwareUpdateRequest(PWM_GENERATOR_1);
    PWM_SoftwareUpdateRequest(PWM_GENERATOR_2);
}

void Audio_Initialize(void)
{
    PWM_DutyCycleSet(PWM_GENERATOR_1, AUDIO_PWM_CENTER);
    PWM_DutyCycleSet(PWM_GENERATOR_2, AUDIO_PWM_CENTER);

    ADC4_ChannelCallbackRegister(&audio_on_sample);

    /* MCC configures PG1/PG2 but leaves them off; enabling PG1 starts the ADC
     * trigger that clocks the sample interrupt. */
    PWM_Enable();
}

void Audio_GetLevels(uint16_t *left, uint16_t *right)
{
    if (left != NULL)
    {
        *left = s_last_left;
    }
    if (right != NULL)
    {
        *right = s_last_right;
    }
}

void Audio_GetPeaks(uint16_t *lmin, uint16_t *lmax, uint16_t *rmin, uint16_t *rmax)
{
    if (lmin != NULL) { *lmin = s_min_left; }
    if (lmax != NULL) { *lmax = s_max_left; }
    if (rmin != NULL) { *rmin = s_min_right; }
    if (rmax != NULL) { *rmax = s_max_right; }

    s_min_left  = 0xFFFFu;
    s_max_left  = 0u;
    s_min_right = 0xFFFFu;
    s_max_right = 0u;
}
