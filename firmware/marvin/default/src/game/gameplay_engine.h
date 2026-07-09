#ifndef GAMEPLAY_ENGINE_H
#define GAMEPLAY_ENGINE_H

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
} game_state_t;

/* Brings up the game-state bus queue and the observer task. The observer
 * subscribes to the video frame queue from inside its task, so call this after
 * Video_Initialize. The task idles (draining frames, ~0 CPU) until a
 * GameplayEngine_Observe() request arrives. */
void GameplayEngine_Initialize(void);

/* Game-state event bus. Consumers (operator UI, the controller at M10) read
 * here. Returns NULL until GameplayEngine_Initialize has run. */
QueueHandle_t GameplayEngine_BusQueue(void);

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
bool GameplayEngine_Observe(game_state_t *out, uint32_t timeout_ms);

#endif
