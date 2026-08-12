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

/* How the nod drives the neck. Wire value: 0x88B9 op 0x01 arg.
 *
 * `auto` is the normal performance setting: the nod runs in occasional bursts of
 * 10-18 s, entered only while the engine is confident of the tempo, so the head
 * joins in for a stretch and then sits still instead of head-banging through a
 * whole song it may be tracking badly. */
typedef enum
{
    BEAT_NOD_OFF    = 0,   /* neck free for manual `pos` / marvin's 0x88B5 */
    BEAT_NOD_ALWAYS = 1,   /* nod every frame */
    BEAT_NOD_AUTO   = 2,   /* occasional bursts on a confident tempo */
} beat_nod_mode_t;

/* Where auto mode is in its cycle. WAIT parks the neck (and leaves it alone, so a
 * manual position sticks between bursts); FINISHING is a burst that has served its
 * time and is waiting for the head to come back up before parking. */
typedef enum
{
    BEAT_NOD_AUTO_WAIT      = 0,
    BEAT_NOD_AUTO_NODDING   = 1,
    BEAT_NOD_AUTO_FINISHING = 2,
} beat_nod_auto_t;

typedef struct
{
    beat_nod_auto_t state;
    uint16_t        beats;        /* beats seen during the current burst */
    uint16_t        run_ms;       /* elapsed in the current burst */
    uint16_t        target_ms;    /* minimum this burst runs for */
    uint16_t        wait_ms;      /* left before another burst may start */
    uint8_t         conf_min;     /* tempo confidence a burst needs to start */
    uint32_t        bursts;       /* bursts run since boot */
    /* Why a burst isn't starting: conf_peak below conf_min means the floor is
     * unreachable on this music (lower it); eligible frames with no bursts means
     * the beat side of the trigger; bigs counts strong beats offered. */
    uint8_t         conf_peak;
    uint32_t        eligible;
    uint32_t        bigs;
} beat_nod_auto_status_t;

void BeatNod_Initialize(void);

/* Stash one beat frame (called from the T1S RX path). len must be >=
 * BEAT_NOD_FRAME_LEN; extra bytes (min-frame padding) are ignored. Cheap:
 * copies + flags, the nod engine runs in BeatNod_Tasks(). */
void BeatNod_OnFrame(const uint8_t *payload, uint16_t len);

/* Tick the nod engine on a new frame and drive the neck servo; park at neutral
 * when frames stop. Call from the main loop. */
void BeatNod_Tasks(void);

/* Select how the nod drives the neck. Anything but ALWAYS parks the neck at
 * neutral, freeing it for manual `pos`/`servo` testing or a marvin 0x88B5
 * command; an unknown value reads as OFF. */
void            BeatNod_SetMode(beat_nod_mode_t mode);
beat_nod_mode_t BeatNod_GetMode(void);

/* Tempo confidence (NodEngine_GetConfidence(), 0-100) an auto burst needs before
 * it starts — the knob for how picky he is. Clamped to 100. */
void    BeatNod_SetAutoConfMin(uint8_t conf_min);

/* Diagnostics for the CLI. */
uint32_t BeatNod_FrameCount(void);
int8_t   BeatNod_NeckPosition(void);   /* last neck position applied */
void     BeatNod_GetAuto(beat_nod_auto_status_t *out);
void     BeatNod_GetLast(uint8_t *seq, uint8_t *energy, uint8_t *bass,
                         uint8_t *treble, uint8_t *kick, uint8_t *flags);

#endif /* BEAT_NOD_H */
