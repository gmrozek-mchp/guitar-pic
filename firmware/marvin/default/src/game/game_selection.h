#ifndef MARVIN_GAME_SELECTION_H
#define MARVIN_GAME_SELECTION_H

#include <stdbool.h>
#include <stdint.h>

/* Committed gameplay selection: the song + play difficulty + game mode the
 * operator confirmed in the song-select dialog (SELECT). Keyed on the
 * recognizer's stable (setlist, index) so it lines up with GameArt_* / GameCatalog_* and
 * the gameplay engine. Pure static state (no FreeRTOS/UI deps); single observer
 * for the UI to mirror it. Static zero-init leaves valid=false until the first
 * GameSelection_Set, so no explicit initialize call is needed. */

typedef enum
{
    GAME_DIFF_EASY = 0,
    GAME_DIFF_MEDIUM,
    GAME_DIFF_HARD,
    GAME_DIFF_EXPERT
} game_difficulty_t;

typedef enum
{
    GAME_MODE_1P_ROBOT = 0,
    GAME_MODE_1P_HUMAN,
    GAME_MODE_2P
} game_mode_t;

typedef struct
{
    bool    valid;      /* false until the first GameSelection_Set */
    uint8_t setlist;    /* GP_SETLIST_* */
    uint8_t index;      /* song ordinal within its setlist */
    uint8_t difficulty; /* game_difficulty_t */
    uint8_t mode;       /* game_mode_t */
} game_selection_t;

/* Lower-case name for a game_difficulty_t, as the results CSV and any other
 * text consumer wants it ("easy".."expert"); "" for an out-of-range value. */
const char *GameSelection_DifficultyName(uint8_t difficulty);

/* Commit a selection and notify the observer (if any). */
void GameSelection_Set(uint8_t setlist, uint8_t index, uint8_t difficulty, uint8_t mode);

/* The current committed selection; ->valid is false before the first Set. */
const game_selection_t *GameSelection_Get(void);

/* Register the single observer, invoked on every GameSelection_Set with the new
 * selection. NULL clears it. */
void GameSelection_SetObserver(void (*cb)(const game_selection_t *));

#endif /* MARVIN_GAME_SELECTION_H */
