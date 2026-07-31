#ifndef BEAT_ENGINE_H
#define BEAT_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

/* Beat decision layer over beat_detect. Beat_Initialize registers an audio
 * sample callback (mono sum of the post-HPF L/R pair feeds beat_detect) and
 * seeds the feature extractor. Beat_Tasks, called from the main loop, runs the
 * pending FFT and, on each new ~23.4 Hz frame, turns the raw spectral-flux
 * features into discrete beat events (a bass-band and a mid/high-band detector,
 * each with running-average delta thresholding, a cooldown, and a noise gate).
 * The result is published as a BeatFrame for consumers to read via Beat_GetFrame.
 * Tempo/BPM and phase are downstream consumers of these events, added later. */

typedef struct
{
    uint8_t  bass_beat;      /* 0=none, 1=beat, 2=strong */
    uint8_t  full_beat;      /* 0=none, 1=beat, 2=strong */
    uint8_t  kick_beat;      /* 0=none, 1=kick, 2=strong kick */
    uint16_t kick_strength;  /* 0-1000 when a kick fires */
    uint16_t flux_bass;      /* 0-1000, auto-ranged */
    uint16_t flux_full;      /* 0-1000, auto-ranged */
    uint16_t raw_env;        /* 0-10000 absolute envelope */
    bool     bass_dominant;
} BeatFrame;

void Beat_Initialize(void);
void Beat_Tasks(void);
bool Beat_HasFrame(void);            /* true once per new frame (consumes) */
void Beat_GetFrame(BeatFrame *out);

#endif /* BEAT_ENGINE_H */
