#ifndef MARVIN_GAME_ART_H
#define MARVIN_GAME_ART_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/legato/image/legato_image.h"   /* leImage */

/* Album-artwork cache: decodes every cover under
 * <card>/games/gh3-wii/art/{small,large}/ once at boot into static, fixed-size
 * RGB888 slots in DDR (spec §4.8.7). Runtime access is then an O(1) leImage*
 * pointer — no card I/O, no decode, no allocation at use time.
 *
 * Two size tiers, each a fixed slot dimension:
 *   small  144x144  (dashboard now-playing thumbnail)         JPEG on card
 *   large  508x208  (song-select detail strip, difficulty fade baked offline) PNG on card
 *
 * Covers are keyed by the recognizer's stable (setlist, index). A missing,
 * oversized, wrong-size, or corrupt file simply leaves its slot empty; the
 * lookup returns NULL and the UI shows a blank — the loader never fails boot. */

void Art_Initialize(void);   /* state only; no I/O (call before scheduler) */

/* Mount the card and decode all covers in both tiers into the caches. Idempotent
 * after the first successful pass. Returns the total number of covers decoded.
 * Must run from a task (blocks on SD I/O) and after Legato's image decoders are
 * up — i.e. during the boot/splash sequence. */
int  Art_LoadAll(void);
bool Art_IsLoaded(void);

/* Decoded cover for (setlist, index), or NULL if absent/failed. The pointer is
 * stable for the life of the cache (no reload today). */
const leImage *Art_Large(uint8_t setlist, uint8_t index);
const leImage *Art_Small(uint8_t setlist, uint8_t index);

int  Art_CountSmall(void);
int  Art_CountLarge(void);

#endif
