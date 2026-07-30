#ifndef BEAT_DETECT_H
#define BEAT_DETECT_H

#include <stdint.h>
#include <stdbool.h>

#define BEAT_NUM_BANDS      6
#define BEAT_SPECTRUM_BINS   128
#define BEAT_DRUM_HIST_LEN  128

void BeatDetect_Init(void);
void BeatDetect_Process(float mono_sample);
void BeatDetect_RunFFT(void);

#define BEAT_SPEC_BINS      64

bool BeatDetect_HasFrame(void);
uint16_t BeatDetect_GetEnvelopeValue(void);
uint16_t BeatDetect_GetFluxValue(void);
uint16_t BeatDetect_GetBassFluxValue(void);
bool BeatDetect_IsBassDominant(void);
uint16_t BeatDetect_GetBassPeakBin(void);
uint16_t BeatDetect_GetFullPeakBin(void);
uint16_t BeatDetect_GetRawEnvelope(void);  // absolute 0-10000 (1.0 = 10000)
uint16_t BeatDetect_GetFrameTime(void);   // frame period in 10us units (expect ~4270 = 42.7ms)
uint8_t BeatDetect_GetKickBeat(void);    // 0=none, 1=kick detected, 2=strong kick
uint16_t BeatDetect_GetKickStrength(void); // 0-1000 proportional intensity when kick detected
bool BeatDetect_HasSpectrumFrame(void);
void BeatDetect_GetSpectrumData(uint8_t *out);

// Stubs
bool BeatDetect_HasBeat(void);
float BeatDetect_GetStrength(void);
float BeatDetect_GetFlux(void);
float BeatDetect_GetFluxAvg(void);
uint8_t BeatDetect_GetActiveBand(void);
float BeatDetect_GetBPM(void);
float BeatDetect_GetConfidence(void);
bool BeatDetect_IsMusicPresent(void);
bool BeatDetect_HasSpectrum(void);
void BeatDetect_GetSpectrum(uint8_t *out);
void BeatDetect_GetDrumSignal(uint8_t *out);
void BeatDetect_SetThreshold(float value);
float BeatDetect_GetThreshold(void);
void BeatDetect_SetBandMask(uint8_t mask);
uint8_t BeatDetect_GetBandMask(void);
void BeatDetect_SetMode(uint8_t mode);
uint8_t BeatDetect_GetMode(void);

#endif
