#include "audio.h"

#include <stddef.h>

#include "../mcc_generated_files/adc/adc4.h"
#include "../mcc_generated_files/pwm_hs/pwm.h"

/* PWM_HS master period (MPER) is 66651 counts. Idle both channels at mid-scale
 * and swing the normalized sample about it; the gain keeps the swing inside the
 * rails. */
#define AUDIO_PWM_CENTER  (33325u)
#define AUDIO_PWM_GAIN    (16650.0f)

/* First-order DC-blocking high-pass (~20 Hz @ 48 kHz). The line-in idles a few
 * thousand counts off the 32768 mid-rail (front-end bias, not 0-centred), so
 * this removes the DC term adaptively — the output idles at true mid-scale
 * whatever the input bias is — and strips sub-audible rumble.
 * y = a * (y_prev + x - x_prev), a = fs / (fs + 2*pi*fc). */
#define AUDIO_HPF_CUTOFF_HZ   (20.0f)
#define AUDIO_SAMPLE_RATE_HZ  (48000.0f)

typedef struct
{
    float a;
    float x_prev;
    float y_prev;
} dc_block_t;

static dc_block_t s_dc_left;
static dc_block_t s_dc_right;

static void dc_block_init(dc_block_t *hp)
{
    hp->a = AUDIO_SAMPLE_RATE_HZ
          / (AUDIO_SAMPLE_RATE_HZ + (6.2831853f * AUDIO_HPF_CUTOFF_HZ));
    hp->x_prev = 0.0f;
    hp->y_prev = 0.0f;
}

static inline float dc_block_process(dc_block_t *hp, float x)
{
    float y = hp->a * (hp->y_prev + x - hp->x_prev);
    hp->x_prev = x;
    hp->y_prev = y;
    return y;
}

static volatile uint16_t s_last_left  = 0u;
static volatile uint16_t s_last_right = 0u;

/* Peak envelope since the last Audio_GetPeaks read (raw ADC counts). */
static volatile uint16_t s_min_left  = 0xFFFFu;
static volatile uint16_t s_max_left  = 0u;
static volatile uint16_t s_min_right = 0xFFFFu;
static volatile uint16_t s_max_right = 0u;

/* Same, on the post-HPF signal re-expressed in count units (mid 32768). */
static volatile uint16_t s_flast_left  = 32768u;
static volatile uint16_t s_flast_right = 32768u;
static volatile uint16_t s_fmin_left   = 0xFFFFu;
static volatile uint16_t s_fmax_left   = 0u;
static volatile uint16_t s_fmin_right  = 0xFFFFu;
static volatile uint16_t s_fmax_right  = 0u;

/* Optional analysis consumer, fed the post-HPF L/R pair from the ISR. */
static void (*s_sample_cb)(float left, float right) = NULL;

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

/* Re-express a normalized (+/-1) sample as a 0..65535 count centred on 32768,
 * for diagnostics that compare the filtered signal against the raw ADC scale. */
static inline uint16_t to_counts(float sample)
{
    int32_t c = (int32_t)(sample * 32768.0f) + 32768;
    if (c < 0)          { c = 0; }
    else if (c > 65535) { c = 65535; }
    return (uint16_t)c;
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

    float left  = dc_block_process(&s_dc_left,  normalize(raw_left));
    float right = dc_block_process(&s_dc_right, normalize(adc_val));

    uint16_t filt_left  = to_counts(left);
    uint16_t filt_right = to_counts(right);
    s_flast_left  = filt_left;
    s_flast_right = filt_right;
    if (filt_left  < s_fmin_left)  { s_fmin_left  = filt_left; }
    if (filt_left  > s_fmax_left)  { s_fmax_left  = filt_left; }
    if (filt_right < s_fmin_right) { s_fmin_right = filt_right; }
    if (filt_right > s_fmax_right) { s_fmax_right = filt_right; }

    PWM_DutyCycleSet(PWM_GENERATOR_1, to_duty(left));    /* RB8 */
    PWM_DutyCycleSet(PWM_GENERATOR_2, to_duty(right));   /* RB9 */
    PWM_SoftwareUpdateRequest(PWM_GENERATOR_1);
    PWM_SoftwareUpdateRequest(PWM_GENERATOR_2);

    if (s_sample_cb != NULL)
    {
        s_sample_cb(left, right);
    }
}

void Audio_Initialize(void)
{
    dc_block_init(&s_dc_left);
    dc_block_init(&s_dc_right);

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

void Audio_GetFiltered(uint16_t *left, uint16_t *right)
{
    if (left != NULL)  { *left  = s_flast_left; }
    if (right != NULL) { *right = s_flast_right; }
}

void Audio_GetFilteredPeaks(uint16_t *lmin, uint16_t *lmax, uint16_t *rmin, uint16_t *rmax)
{
    if (lmin != NULL) { *lmin = s_fmin_left; }
    if (lmax != NULL) { *lmax = s_fmax_left; }
    if (rmin != NULL) { *rmin = s_fmin_right; }
    if (rmax != NULL) { *rmax = s_fmax_right; }

    s_fmin_left  = 0xFFFFu;
    s_fmax_left  = 0u;
    s_fmin_right = 0xFFFFu;
    s_fmax_right = 0u;
}

void Audio_SampleCallbackRegister(void (*callback)(float left, float right))
{
    s_sample_cb = callback;
}
