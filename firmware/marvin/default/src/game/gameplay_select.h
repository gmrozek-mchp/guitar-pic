#ifndef MARVIN_GAMEPLAY_SELECT_H
#define MARVIN_GAMEPLAY_SELECT_H

#include <stdint.h>

#include "game/gameplay_metadata.h"

/* Selection readers, ported from the host prototype (tools/gameplay) and driven
 * by game/gameplay_metadata.h. Pure (no FreeRTOS/video), so they compile and
 * cross-check on a host before linking into the observer task. Not reentrant
 * (static scratch); only the single observer task calls them. */

/* The static-list menu layout for a screen (GP_SCREEN_* index), or NULL if that
 * screen isn't a modelled static list. */
const gp_menu_layout_t *gp_menu_for_screen(uint8_t screen);

/* Static-list highlight: which cell/item is selected on `menu` for a canonical
 * BGR frame — the cell that deviates most from its learned unselected baseline.
 * Returns the cell index [0, menu->count), or -1 on a bad frame size. */
int gp_read_selection(const uint8_t *frame, int width, int height,
                      const gp_menu_layout_t *menu);

typedef struct
{
    uint8_t setlist;  /* GP_SETLIST_MAIN | GP_SETLIST_BONUS */
    uint8_t index;    /* song ordinal within its setlist */
    int16_t tmpl;     /* index into gp_song_templates, or -1 */
} gp_song_t;

/* song_select: identify the selected song by nearest-template bitmap match
 * (offset search across both setlists). Fills *out; returns 0 on success, -1 on
 * a bad frame size. */
int gp_read_song(const uint8_t *frame, int width, int height, gp_song_t *out);

#endif
