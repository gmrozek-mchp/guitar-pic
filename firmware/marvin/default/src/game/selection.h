#ifndef MARVIN_GAME_SELECTION_H
#define MARVIN_GAME_SELECTION_H

#include <stdbool.h>
#include <stdint.h>

/* Committed gameplay selection: the song + play difficulty + game mode the
 * operator confirmed in the song-select dialog (SELECT). Keyed on the
 * recognizer's stable (setlist, index) so it lines up with Art_* / Catalog_* and
 * the gameplay engine. Pure static state (no FreeRTOS/UI deps); single observer
 * for the UI to mirror it. Static zero-init leaves valid=false until the first
 * Selection_Set, so no explicit initialize call is needed. */

typedef enum
{
    SEL_DIFF_EASY = 0,
    SEL_DIFF_MEDIUM,
    SEL_DIFF_HARD,
    SEL_DIFF_EXPERT
} sel_difficulty_t;

typedef enum
{
    SEL_MODE_1P_ROBOT = 0,
    SEL_MODE_1P_HUMAN,
    SEL_MODE_2P
} sel_mode_t;

typedef struct
{
    bool    valid;      /* false until the first Selection_Set */
    uint8_t setlist;    /* GP_SETLIST_* */
    uint8_t index;      /* song ordinal within its setlist */
    uint8_t difficulty; /* sel_difficulty_t */
    uint8_t mode;       /* sel_mode_t */
} selection_t;

/* Commit a selection and notify the observer (if any). */
void Selection_Set(uint8_t setlist, uint8_t index, uint8_t difficulty, uint8_t mode);

/* The current committed selection; ->valid is false before the first Set. */
const selection_t *Selection_Get(void);

/* Register the single observer, invoked on every Selection_Set with the new
 * selection. NULL clears it. */
void Selection_SetObserver(void (*cb)(const selection_t *));

#endif /* MARVIN_GAME_SELECTION_H */
