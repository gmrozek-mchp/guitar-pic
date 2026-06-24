#ifndef MARVIN_GAME_CATALOG_H
#define MARVIN_GAME_CATALOG_H

#include <stdbool.h>
#include <stdint.h>

#include "game/gameplay_select.h"   /* gp_song_t */

/* Song catalog: maps the recognizer's stable (setlist, index) key to human
 * labels (title/artist/album/bpm/length/year/genre/difficulty) read from
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
    uint16_t bpm;         /* 0 = unknown */
    uint16_t length_s;    /* 0 = unknown */
    uint16_t year;        /* original-release year; 0 = unknown */
    char     genre[24];   /* may be empty */
    char     difficulty[16]; /* in-game difficulty label; may be empty */
} catalog_entry_t;

void Catalog_Initialize(void);   /* state only; no I/O (call before scheduler) */

/* Look up one song's labels. Lazy-loads the catalog from the card on first call
 * (and after a failed load, retries only on an explicit Catalog_Reload). Returns
 * true and fills *out on a hit; false on miss / empty catalog (caller shows
 * "Unknown song"). */
bool Catalog_Lookup(uint8_t setlist, uint8_t index, catalog_entry_t *out);

/* Convenience wrapper keyed by a recognizer result. */
bool Catalog_LookupSong(const gp_song_t *song, catalog_entry_t *out);

/* Force a fresh read from the card (e.g. after a card swap). Returns true if the
 * catalog file was read (even with zero rows); false on mount/open failure. */
bool Catalog_Reload(void);

int  Catalog_Count(void);     /* entries currently cached */
bool Catalog_IsLoaded(void);  /* a load has been attempted */

/* Direct access to a cached entry by position [0, Catalog_Count()), or NULL.
 * For the `catalog ls` console listing; pointer is valid until the next reload. */
const catalog_entry_t *Catalog_At(int i);

#endif
