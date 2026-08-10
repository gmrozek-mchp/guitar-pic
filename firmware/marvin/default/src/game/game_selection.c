#include "game/game_selection.h"

#include <stddef.h>   /* NULL */

static game_selection_t s_sel;
static void (*s_observer)(const game_selection_t *);

void GameSelection_Set(uint8_t setlist, uint8_t index, uint8_t difficulty, uint8_t mode)
{
    s_sel.setlist    = setlist;
    s_sel.index      = index;
    s_sel.difficulty = difficulty;
    s_sel.mode       = mode;
    s_sel.valid      = true;

    if (s_observer != NULL) { s_observer(&s_sel); }
}

const game_selection_t *GameSelection_Get(void)
{
    return &s_sel;
}

void GameSelection_SetObserver(void (*cb)(const game_selection_t *))
{
    s_observer = cb;
}

const char *GameSelection_DifficultyName(uint8_t difficulty)
{
    switch ((game_difficulty_t)difficulty)
    {
        case GAME_DIFF_EASY:   return "easy";
        case GAME_DIFF_MEDIUM: return "medium";
        case GAME_DIFF_HARD:   return "hard";
        case GAME_DIFF_EXPERT: return "expert";
        default:               return "";
    }
}
