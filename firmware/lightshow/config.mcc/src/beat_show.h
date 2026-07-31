#ifndef BEAT_SHOW_H
#define BEAT_SHOW_H

#include <stdbool.h>
#include <stdint.h>

/* Beat-frame consumer: turns beatbox's bus payload into a WS2812 light show.
 *
 * beatbox (beat-source node, id 5) broadcasts a compact 8-byte LightshowFrame
 * under ethertype 0x88B8 at ~23.4 Hz. t1s_follower hands each frame here via
 * BeatShow_OnFrame(); BeatShow_Tasks() renders the current effect to the two
 * NeoPixel strands from the main loop. The strip clears itself if the frames
 * stop (music/bus quiet).
 *
 * Wire payload (all fields 0-255, mirrors beatbox publish.{c,h}):
 *   [0] seq  [1] energy  [2] bass  [3] treble  [4] kick  [5] flags
 *   [6] tempo (reserved)  [7] phase (reserved) */

#define BEAT_SHOW_FRAME_LEN  (8u)

/* Flag bits in payload[5] (mirror beatbox PUB_FLAG_*). */
#define BEAT_FLAG_BASS     (1u << 0)   /* bass-band onset */
#define BEAT_FLAG_MID      (1u << 1)   /* mid/high-band onset */
#define BEAT_FLAG_KICK     (1u << 2)   /* kick detector fired */
#define BEAT_FLAG_BIG      (1u << 3)   /* strong beat (either band) */
#define BEAT_FLAG_BASS_DOM (1u << 4)   /* bass energy dominates */

void BeatShow_Initialize(void);

/* Stash one beat frame (called from the T1S RX path). len must be >=
 * BEAT_SHOW_FRAME_LEN; extra bytes (min-frame padding) are ignored. Cheap:
 * copies + flags, the render happens in BeatShow_Tasks(). */
void BeatShow_OnFrame(const uint8_t *payload, uint16_t len);

/* Render the current effect if a new frame arrived, and idle-clear the strip
 * when frames stop. Call from the main loop. */
void BeatShow_Tasks(void);

/* Effect selection for the CLI. 0 = beat flash, 1 = dual comet, 2 = split
 * energy. BeatShow_SetEffect locks an effect; BeatShow_SetAuto resumes the
 * ~20 s auto-cycle. */
#define BEAT_SHOW_EFFECTS  (3u)
void    BeatShow_SetEffect(uint8_t idx);
void    BeatShow_SetAuto(void);
uint8_t BeatShow_Effect(void);
bool    BeatShow_IsAuto(void);

/* Diagnostics for the CLI. */
uint32_t BeatShow_FrameCount(void);
void     BeatShow_GetLast(uint8_t *seq, uint8_t *energy, uint8_t *bass,
                          uint8_t *treble, uint8_t *kick, uint8_t *flags);

#endif /* BEAT_SHOW_H */
