#ifndef BEAT_JAW_H
#define BEAT_JAW_H

#include <stdbool.h>
#include <stdint.h>

/* Jaw animation — lemmy's own business. Nothing on the bus commands the jaw: it
 * repositions itself off the same beat frames the nod runs on, so the puppet's
 * mouth has a life of its own whether or not marvin is paying attention.
 *
 * Three poses, held for seconds at a time rather than chewed once per beat: relaxed
 * open at rest, wide open for a yell while the head is actually banging, and shut
 * now and then. Moves are slew-limited so they read as a jaw moving rather than a
 * servo twitching.
 *
 * beat_nod owns the frame tick, so it owns this module's clock too. */

typedef enum
{
    BEAT_JAW_REST   = 0,   /* relaxed open — where the jaw lives */
    BEAT_JAW_YELL   = 1,   /* wide open, during a head-banging burst */
    BEAT_JAW_CLOSED = 2,   /* shut */
} beat_jaw_state_t;

void BeatJaw_Initialize(void);

/* One beat frame. `nodding` is whether the neck is being driven this frame (a burst
 * in auto mode, every frame in always mode) — the yell only happens while it is.
 * `big` is the frame's BIG_BEAT flag, `energy` its envelope byte. */
void BeatJaw_Frame(bool nodding, bool big, uint8_t energy);

/* Park at rest and stop driving, so a manual `pos`/0x88B5 jaw position sticks.
 * Called when the beat frames stop, or the nod mode goes off. */
void BeatJaw_Park(void);

/* Diagnostics for the CLI. */
beat_jaw_state_t BeatJaw_State(void);
int8_t           BeatJaw_Position(void);   /* last position applied */
uint16_t         BeatJaw_HoldMs(void);     /* left in the current pose */
uint32_t         BeatJaw_Yells(void);
uint32_t         BeatJaw_Closes(void);

#endif /* BEAT_JAW_H */
