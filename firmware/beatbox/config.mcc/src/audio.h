#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

/* Stereo audio passthrough. ADC4 samples line-in at 48 kHz (PG1-triggered,
 * 256x oversampled) and this module mirrors each L/R pair straight back out on
 * the PWM_HS audio DACs: PWM1H (RB8) = left, PWM2H (RB9) = right, via a DC-block
 * high-pass. Call Audio_Initialize once after SYSTEM_Initialize; the path then
 * runs entirely from the ADC completion interrupt. Audio_GetLevels reports the
 * most recent raw sample pair for bench diagnostics. */

void Audio_Initialize(void);
void Audio_GetLevels(uint16_t *left, uint16_t *right);

/* Min/max raw sample seen per channel since the previous call (the ISR tracks
 * peaks continuously; this reads and resets the window). Peak-to-peak is the
 * honest measure of input swing — instantaneous Audio_GetLevels undersamples it. */
void Audio_GetPeaks(uint16_t *lmin, uint16_t *lmax, uint16_t *rmin, uint16_t *rmax);

/* Same as GetLevels/GetPeaks but on the post-HPF signal, re-expressed in raw
 * count units (mid 32768). This is the DC-blocked audio that actually drives the
 * DACs, so its idle sits near 32768 and its peak-to-peak is the true output swing. */
void Audio_GetFiltered(uint16_t *left, uint16_t *right);
void Audio_GetFilteredPeaks(uint16_t *lmin, uint16_t *lmax, uint16_t *rmin, uint16_t *rmax);

#endif /* AUDIO_H */
