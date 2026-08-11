#ifndef MARVIN_GAME_ENGINE_H
#define MARVIN_GAME_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "queue.h"

/* Game-state observer — see firmware/marvin/docs/spec.md §4.8 (M9).
 *
 * A video-frame consumer (not a detector): it classifies which GH3 screen the
 * captured frame shows and publishes typed game_state_t events on its own bus
 * (xGameStateQueue), kept separate from the detector-state bus per the §4.2
 * multi-detector decision. The recognizer data (region grid, per-class
 * centroids, thresholds) is the host-proven metadata frozen into
 * game/gameplay_metadata.h by `gameplay export-c`.
 *
 * Phase 1 (this module): screen classification only. Selection/song readers
 * (Phase 2) and the navigator/controller (Phase 3, M10) build on this. */

typedef struct
{
    uint32_t frame_epoch;   /* master sync token, = Video_FrameInfo.frame_count */
    uint64_t timestamp_us;  /* monotonic, marvin-local */
    uint8_t  screen;        /* GP_SCREEN_* index, or GP_SCREEN_UNKNOWN */
    uint8_t  reserved[3];
    int32_t  best_dist;     /* L1 to the nearest centroid */
    int32_t  margin;        /* second-best − best (confidence) */
    int16_t  selection;     /* static-list cell, song-template index, or -1 (none) */
    int32_t  score;         /* in-song score read (training font), or -1 (not in_song) */
    uint8_t  multiplier;    /* in-song score multiplier 1..4, or 0 (not in_song) */
    uint16_t streak;        /* in-song note streak (monotonic tracker), 0 = not shown / <~25 */
    int8_t   ready_p1;      /* guitar_select_2p: P1's READY! badge — 1 shown, 0 not, -1 n/a */
    /* in_song_2p only: the two amp scoreboards, -1 when not read. P1 is marvin's side
     * (it drives the left highway only), P2 the human's. Separate from `score` above,
     * which is the 1p scoring block and reads -1 during a 2-player song. */
    int32_t  score_p1;
    int32_t  score_p2;
} game_state_t;

/* Brings up the game-state bus queue and the observer task. The observer
 * subscribes to the video frame queue from inside its task, so call this after
 * Video_Initialize. The task idles (draining frames, ~0 CPU) until a
 * GameEngine_Observe() request arrives. */
void GameEngine_Initialize(void);

/* Game-state event bus. Consumers (operator UI, the controller at M10) read
 * here. Returns NULL until GameEngine_Initialize has run. */
QueueHandle_t GameEngine_BusQueue(void);

/* Synchronous observation: request a classification of a fresh frame and block
 * until the result arrives, or timeout_ms elapses. On success writes the
 * {screen, selection, …} into *out and returns true; returns false on timeout or
 * a NULL out. This is the only way to read screen state — there is no retained
 * "current screen" to poll, so a read never returns stale state.
 *
 * The classified frame is guaranteed to have been captured after the request
 * (the task skips the in-hand frame, which predates it). The sole requester is
 * the game controller; do not call concurrently from multiple tasks.
 *
 * NOTE: GH3 menus take several frames to transition (cursor animation, fades, a
 * `loading` screen), so the controller settles/re-observes rather than trusting
 * one read the instant after actuating. */
bool GameEngine_Observe(game_state_t *out, uint32_t timeout_ms);

/* ── end-of-song watch (frame-rate) ───────────────────────────────────────── */

/* Arm or disarm the end-of-song probe (game/gameplay_endprobe.h).
 *
 * While armed the observer runs the probe on *every* frame it drains — 225 luma
 * samples, integer — instead of discarding the frame. On a confirmed results
 * screen it cuts the timing pipeline itself and signals the semaphore below.
 * That directness is the point: routing the stop through the controller's poll
 * would put the 300 ms sleep back in the path, which is the delay this exists to
 * remove. The controller still decides that the *run* ended, off gp_classify.
 *
 * Arm when the actuation window opens and disarm when it closes; disarming also
 * clears the confirm state, so the next song starts from a clean tracker. */
void GameEngine_ArmEndWatch(bool armed);

/* Wait for the end-of-song probe to fire, up to timeout_ms. Returns true if it
 * fired (actuation has *already* been cut by then), false on timeout. Replaces a
 * blind sleep in the controller's play loop so the loop wakes on the event
 * instead of a poll tick. Only meaningful while armed. */
bool GameEngine_WaitEndOfSong(uint32_t timeout_ms);

/* Did the probe fire during the armed window, and has it stayed fired? Lets the
 * controller distinguish "the song ended" from "Stop was requested" after the
 * play loop breaks, without re-reading a frame. */
bool GameEngine_EndOfSongSeen(void);

#endif
