#ifndef PUBLISH_H
#define PUBLISH_H

#include <stdint.h>
#include <stdbool.h>

#include "beat_engine.h"

/* Outbound-payload layer: maps the internal BeatFrame features into the compact
 * frames the bus carries to the animation/lighting nodes. The T1S transport is a
 * separate layer added later; this module only produces the payloads.
 *
 * Today it builds the lightshow beat frame. lemmy position commands join here
 * once the choreography layer is ported. */

/* Beat frame sent to lightshow (~23.4 Hz). Fields are normalized to 0-255 so the
 * consumer drives LEDs directly; the source BeatFrame's mixed ranges stay local.
 * tempo/phase are reserved (0) until the tempo/BPM + phase layer lands, so the
 * wire size is stable across that addition. */
typedef struct
{
    uint8_t seq;      /* frame counter; wraps at 256, lets the consumer spot drops */
    uint8_t energy;   /* overall amplitude envelope (from raw_env) */
    uint8_t bass;     /* bass-band energy (from flux_bass) */
    uint8_t treble;   /* mid/high-band energy (from flux_full) */
    uint8_t kick;     /* kick strength this frame, 0 when no kick */
    uint8_t flags;    /* see PUB_FLAG_* */
    uint8_t tempo;    /* reserved: BPM, 0 until the tempo layer lands */
    uint8_t phase;    /* reserved: beat phase 0-255 = one beat, 0 until then */
} LightshowFrame;

#define PUB_FLAG_BASS_BEAT   (1u << 0) /* bass-band onset this frame */
#define PUB_FLAG_MID_BEAT    (1u << 1) /* mid/high-band onset this frame */
#define PUB_FLAG_KICK        (1u << 2) /* kick hit this frame */
#define PUB_FLAG_BIG_BEAT    (1u << 3) /* any of the above fired "strong" */
#define PUB_FLAG_BASS_DOM    (1u << 4) /* bass energy dominates the spectrum */

void Publish_Initialize(void);

/* Build the outbound payloads from one BeatFrame. Call once per new frame. */
void Publish_Update(const BeatFrame *f);

/* Snapshot the latest lightshow frame (CLI now, T1S sender later). */
void Publish_GetLightshowFrame(LightshowFrame *out);

/* True once per newly built frame (consumes); for the future T1S sender. */
bool Publish_HasLightshowFrame(void);

#endif /* PUBLISH_H */
