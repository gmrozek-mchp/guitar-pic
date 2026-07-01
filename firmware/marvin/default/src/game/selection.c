#include "game/selection.h"

#include <stddef.h>   /* NULL */

static selection_t s_sel;
static void (*s_observer)(const selection_t *);

void Selection_Set(uint8_t setlist, uint8_t index, uint8_t difficulty, uint8_t mode)
{
    s_sel.setlist    = setlist;
    s_sel.index      = index;
    s_sel.difficulty = difficulty;
    s_sel.mode       = mode;
    s_sel.valid      = true;

    if (s_observer != NULL) { s_observer(&s_sel); }
}

const selection_t *Selection_Get(void)
{
    return &s_sel;
}

void Selection_SetObserver(void (*cb)(const selection_t *))
{
    s_observer = cb;
}
