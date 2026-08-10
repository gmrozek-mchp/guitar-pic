#ifndef MARVIN_GAME_CATALOG_H
#define MARVIN_GAME_CATALOG_H

#include <stdbool.h>
#include <stdint.h>

#include "game/gameplay_select.h"   /* gp_song_t */

/* Song catalog: maps the recognizer's stable (setlist, index) key to human
 * labels (title/artist/album/nod_trim/length/year/genre/difficulty) read from
 * <card>/games/gh3-wii/songs.csv
 * (spec §4.8.3). Labels only — recognition stays compile-time in flash, so a
 * missing/stale catalog degrades to "Unknown song" and never affects play.
 *
 * Flat CSV (header + one row per song), parsed with the shared quote-aware
 * splitter (util/csv.h) into a static cache on first use; no malloc, fits
 * FF_FS_MAX_FILES=1 since the file is closed once loaded. Editable on a PC in
 * pandas/Excel, like results.csv. */

typedef struct
{
    uint8_t  setlist;     /* GP_SETLIST_* */
    uint8_t  index;       /* song ordinal within its setlist */
    char     title[64];
    char     artist[48];
    char     album[64];   /* may be empty (no clean commercial release) */
    /* lemmy's beat-nod trim for this song, signed — the 6th CSV column. 0 = don't
     * nod to this one (he doesn't suit every song), which is also what a blank cell
     * and an unlisted song read as. Not a tempo: it is lemmy's `nod trim` knob
     * verbatim, an offset on the nod oscillator's half-period, useful over roughly
     * ±14 (the node clamps the resulting period to 4..18 frames). */
    int16_t  nod_trim;
    uint16_t length_s;    /* 0 = unknown */
    uint16_t year;        /* original-release year; 0 = unknown */
    char     genre[24];   /* may be empty */
    char     difficulty[16]; /* in-game difficulty label; may be empty */
} game_catalog_entry_t;

void GameCatalog_Initialize(void);   /* state only; no I/O (call before scheduler) */

/* Look up one song's labels. Lazy-loads the catalog from the card on first call
 * (and after a failed load, retries only on an explicit GameCatalog_Reload). Returns
 * true and fills *out on a hit; false on miss / empty catalog (caller shows
 * "Unknown song"). */
bool GameCatalog_Lookup(uint8_t setlist, uint8_t index, game_catalog_entry_t *out);

/* Convenience wrapper keyed by a recognizer result. */
bool GameCatalog_LookupSong(const gp_song_t *song, game_catalog_entry_t *out);

/* Just the nod_trim column — 0 for a song the catalog doesn't know as well as for a
 * blank cell, so 0 always means "no nod". Separate from Lookup so a caller that only
 * needs it doesn't put a whole entry on its stack. */
int16_t GameCatalog_NodTrim(uint8_t setlist, uint8_t index);

/* This song's human title, or NULL on a miss / empty catalog. Returns a pointer
 * into the cache rather than filling a caller struct, for the same reason
 * GameCatalog_NodTrim does: a game_catalog_entry_t is ~230 bytes and the callers
 * that want one field are on small task stacks. Valid until a Reload. */
const char *GameCatalog_Title(uint8_t setlist, uint8_t index);

/* Force a fresh read from the card (e.g. after a card swap). Returns true if the
 * catalog file was read (even with zero rows); false on mount/open failure. */
bool GameCatalog_Reload(void);

int  GameCatalog_Count(void);     /* entries currently cached */
bool GameCatalog_IsLoaded(void);  /* a load has been attempted */

/* Direct access to a cached entry by position [0, GameCatalog_Count()), or NULL.
 * For the `catalog ls` console listing; pointer is valid until the next reload. */
const game_catalog_entry_t *GameCatalog_At(int i);

#endif
