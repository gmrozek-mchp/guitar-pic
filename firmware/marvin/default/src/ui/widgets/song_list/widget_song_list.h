#ifndef MARVIN_UI_WIDGET_SONG_LIST_H
#define MARVIN_UI_WIDGET_SONG_LIST_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/legato/legato.h"   /* leWidget, leColor, leFont */

/* Scrollable, touch-driven song list (a custom leWidget subclass) for the
 * SELECT SONG screen — spec §4.8, mockup. Decoupled from the catalog: the owner
 * registers a row-provider that fills each visible row on demand, so the widget
 * never depends on game/catalog.h and the screen owns the catalog -> row mapping
 * (and the eventual tier/setlist grouping).
 *
 * Interaction: drag scrolls (content follows the finger) with kinetic inertia on
 * release; a touch that barely moves is a tap and selects that row, firing the
 * select handler. Painted virtualized — only visible rows are drawn, via the
 * Legato renderer (no per-row child widgets).
 *
 * Allocated from the Legato widget pool like every MGS widget (one
 * self-contained allocation; all state lives in the widget struct). Attach with
 * the usual parent->fn->addChild(parent, SongList_New()); Legato owns its
 * lifetime and frees it on screen teardown. */

typedef struct
{
    const char *badge;       /* short tier tag drawn in badgeColor, e.g. "T1"; "" / NULL = none */
    leColor     badgeColor;  /* RGB_888 (0xRRGGBB) text color for the badge */
    const char *title;
    const char *artist;
    const char *right;       /* right-aligned cell, e.g. "3:42"; NULL / "" = none */
    bool        selected;    /* set by the widget before paint; providers may ignore it */
} songlist_row_t;

/* Fill *out for row `index`; return false if index is out of range. Called
 * during paint for visible rows only — keep it cheap and non-blocking. */
typedef bool (*songlist_row_fn)(void *ctx, int index, songlist_row_t *out);

/* Fired when a tap changes the selection. */
typedef void (*songlist_select_fn)(void *ctx, int index);

leWidget *SongList_New(void);

void SongList_SetModel(leWidget *w, int count, songlist_row_fn rows, void *ctx);
void SongList_SetSelectHandler(leWidget *w, songlist_select_fn fn, void *ctx);
void SongList_SetFonts(leWidget *w, const leFont *title, const leFont *meta, const leFont *badge);
void SongList_SetRowHeight(leWidget *w, int px);

int  SongList_Selected(const leWidget *w);
void SongList_SetSelected(leWidget *w, int index);   /* programmatic; scrolls into view */

/* Diagnostic: the renderer's per-paint counter for this widget (incremented
 * whenever Legato actually repaints it). Used to classify the overlay
 * repaint-on-sibling-damage behavior. */
uint32_t SongList_DrawCount(const leWidget *w);

/* Diagnostic: when on, the widget paints a bare solid magenta rect (no rows) so
 * we can tell a compositing/z-order problem from a row-rendering one. */
void SongList_SetDebugFill(leWidget *w, bool on);

#endif
