#ifndef BEAT_NOD_H
#define BEAT_NOD_H

#include <stdbool.h>
#include <stdint.h>

/* Beat-frame consumer: turns beatbox's bus payload into a head nod on the neck
 * servo.
 *
 * beatbox (beat-source node, id 5) broadcasts a compact 8-byte LightshowFrame
 * under ethertype 0x88B8 at ~23.4 Hz. t1s_follower hands each frame here via
 * BeatNod_OnFrame(); BeatNod_Tasks() ticks the nod engine and drives the neck
 * servo from the main loop. The head parks at neutral if the frames stop
 * (music/bus quiet). beatbox's frame rate equals the engine's design tick rate,
 * so one NodEngine_Frame() per received frame keeps its tempo tracking valid.
 *
 * Wire payload (all fields 0-255, mirrors beatbox publish.{c,h}):
 *   [0] seq  [1] energy  [2] bass  [3] treble  [4] kick  [5] flags
 *   [6] tempo (reserved)  [7] phase (reserved) */

#define BEAT_NOD_FRAME_LEN  (8u)

/* Flag bits in payload[5] (mirror beatbox PUB_FLAG_* / lightshow BEAT_FLAG_*). */
#define BEAT_FLAG_BASS     (1u << 0)   /* bass-band onset */
#define BEAT_FLAG_MID      (1u << 1)   /* mid/high-band onset */
#define BEAT_FLAG_KICK     (1u << 2)   /* kick detector fired */
#define BEAT_FLAG_BIG      (1u << 3)   /* strong beat (either band) */
#define BEAT_FLAG_BASS_DOM (1u << 4)   /* bass energy dominates */

void BeatNod_Initialize(void);

/* Stash one beat frame (called from the T1S RX path). len must be >=
 * BEAT_NOD_FRAME_LEN; extra bytes (min-frame padding) are ignored. Cheap:
 * copies + flags, the nod engine runs in BeatNod_Tasks(). */
void BeatNod_OnFrame(const uint8_t *payload, uint16_t len);

/* Tick the nod engine on a new frame and drive the neck servo; park at neutral
 * when frames stop. Call from the main loop. */
void BeatNod_Tasks(void);

/* Enable/disable driving the neck servo from the nod. Disabling frees the neck
 * for manual `pos`/`servo` testing or a marvin 0x88B5 command. */
void BeatNod_SetEnabled(bool en);
bool BeatNod_IsEnabled(void);

/* Diagnostics for the CLI. */
uint32_t BeatNod_FrameCount(void);
int8_t   BeatNod_NeckPosition(void);   /* last neck position applied */
void     BeatNod_GetLast(uint8_t *seq, uint8_t *energy, uint8_t *bass,
                         uint8_t *treble, uint8_t *kick, uint8_t *flags);

#endif /* BEAT_NOD_H */
