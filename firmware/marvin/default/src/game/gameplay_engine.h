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
} game_state_t;

/* Brings up the game-state bus queue and the observer task. The observer
 * subscribes to the video frame queue from inside its task, so call this after
 * Video_Initialize. Observation starts disabled; call SetObserveEnabled(true)
 * to begin publishing (spec §6 game_observe_enable). */
void GameplayEngine_Initialize(void);

/* Game-state event bus. Consumers (operator UI, the controller at M10) read
 * here. Returns NULL until GameplayEngine_Initialize has run. */
QueueHandle_t GameplayEngine_BusQueue(void);

/* Observation toggle. When off the task still drains frames (so the video
 * queue doesn't back up) but classifies nothing and emits no events. */
void GameplayEngine_SetObserveEnabled(bool on);
bool GameplayEngine_ObserveEnabled(void);

/* Force the observer to run on the next captured frame, bypassing the ~5 Hz
 * background rate limit. Intended for closed-loop control (M10): read the result
 * of an actuator command promptly instead of waiting for the next background
 * tick. One-shot (cleared once consumed); thread-safe (single volatile flag).
 *
 * NOTE: don't trigger this the instant after sending the command — GH3 menus
 * take several frames to transition (cursor animation, screen fades, a `loading`
 * screen). The caller should let the screen settle first (a delay, and/or poll
 * until the expected screen appears with a timeout) rather than trust a single
 * immediate read. That settle policy lives in the controller (M10). */
void GameplayEngine_RequestObservation(void);

/* Most recently classified screen (GP_SCREEN_* index, or GP_SCREEN_UNKNOWN);
 * GP_SCREEN_UNKNOWN before the first classified frame. */
uint8_t GameplayEngine_CurrentScreen(void);

/* Snapshot the most recent classification (screen + selection + frame_epoch),
 * retained every classify. Returns false until the first classified frame. The
 * game-state controller polls this (paired with RequestObservation) for a fresh
 * {screen, selection} after each actuator command. */
bool GameplayEngine_GetLatest(game_state_t *out);

#endif
