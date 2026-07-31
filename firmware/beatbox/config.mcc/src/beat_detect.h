#ifndef BEAT_DETECT_H
#define BEAT_DETECT_H

#include <stdint.h>
#include <stdbool.h>

/* Audio feature extraction. BeatDetect_Process is fed one mono sample per ADC
 * frame (48 kHz); it downsamples by 4 into a 512-point buffer. BeatDetect_RunFFT,
 * called from the main loop, runs the FFT when a buffer is ready (~23.4 Hz frame
 * rate) and updates the feature outputs: an amplitude envelope, spectral flux
 * split into a bass band and a mid/high band, a bass-dominance flag, a kick-drum
 * detector, and a 64-bin display spectrum. The higher-level beat decision that
 * consumes these lives in beat_engine. */

#define BEAT_SPEC_BINS  64

void BeatDetect_Init(void);
void BeatDetect_Process(float mono_sample);   /* per-sample, 48 kHz (ISR ctx) */
void BeatDetect_RunFFT(void);                  /* main loop; runs when a buffer is ready */
bool BeatDetect_HasFrame(void);                /* true once per new FFT frame (consumes) */

uint16_t BeatDetect_GetEnvelopeValue(void);    /* 0-1000, auto-ranged */
uint16_t BeatDetect_GetFluxValue(void);        /* mid/high-band flux, 0-1000 auto-ranged */
uint16_t BeatDetect_GetBassFluxValue(void);    /* bass-band flux, 0-1000 auto-ranged */
bool     BeatDetect_IsBassDominant(void);
uint16_t BeatDetect_GetBassPeakBin(void);
uint16_t BeatDetect_GetFullPeakBin(void);
uint16_t BeatDetect_GetRawEnvelope(void);      /* absolute 0-10000 (1.0 = 10000) */
uint8_t  BeatDetect_GetKickBeat(void);         /* 0=none, 1=kick, 2=strong kick */
uint16_t BeatDetect_GetKickStrength(void);     /* 0-1000 when a kick fires */

bool BeatDetect_HasSpectrumFrame(void);        /* true every 3rd frame (consumes) */
void BeatDetect_GetSpectrumData(uint8_t *out); /* copies BEAT_SPEC_BINS bytes */

#endif /* BEAT_DETECT_H */
