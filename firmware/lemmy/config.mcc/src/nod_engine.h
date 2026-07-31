#ifndef NOD_ENGINE_H
#define NOD_ENGINE_H

#include <stdint.h>

/* Integer beat-driven nod engine: tracks tempo from per-band beat onsets and
 * produces a neck angle each frame. Self-contained (no hardware coupling) — the
 * caller ticks NodEngine_Frame() once per beat frame and reads the target angle
 * with NodEngine_GetTargetAngle() to drive the servo. Angle is tenths of a
 * degree (170 = 17.0° head-up neutral … 800 = 80.0° comeback slam); raw_env is a
 * 0-10000 loudness domain. */

#define NOD_STATE_STOPPED   0
#define NOD_STATE_SEARCHING 1
#define NOD_STATE_LOCKED    2

#define NOD_BAND_BASS       0
#define NOD_BAND_FULL       1
#define NOD_BAND_UNDECIDED  2

void NodEngine_Init(void);
void NodEngine_Frame(uint8_t bass_beat, uint8_t full_beat, uint16_t raw_env);

uint8_t NodEngine_GetState(void);
uint16_t NodEngine_GetLockedBPM(void);
uint8_t NodEngine_GetConfidence(void);
void NodEngine_SetPotOffset(int8_t offset);
uint16_t NodEngine_GetTargetAngle(void);
uint8_t NodEngine_GetRepetitionCount(void);
int8_t NodEngine_GetPotOffset(void);
uint8_t NodEngine_GetWinningBand(void);    // 0=bass, 1=full, 2=undecided
void NodEngine_SetOscEnabled(uint8_t en);  // 0=beat-only, 1=oscillator+beat

#endif
