#ifndef MARVIN_GAME_ART_H
#define MARVIN_GAME_ART_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/legato/image/legato_image.h"   /* leImage */

/* Album-artwork cache: decodes every cover under
 * <card>/games/gh3-wii/art/{small,large}/ once at boot into static, fixed-size
 * DDR slots (spec §4.8.7). Runtime access is then an O(1) leImage* pointer — no
 * card I/O, no decode, no allocation at use time. Each tier's slot format matches
 * the hardware layer it's shown on, so the blit is a native copy.
 *
 * Two size tiers, each a fixed slot dimension:
 *   small  144x144  RGB565    (dashboard now-playing thumbnail, on the RGB565 BASE) JPEG on card
 *   large  508x208  RGBA8888  (song-select detail strip, difficulty fade baked offline) PNG on card
 *
 * Covers are keyed by the recognizer's stable (setlist, index). A missing,
 * oversized, wrong-size, or corrupt file simply leaves its slot empty; the
 * lookup returns NULL and the UI shows a blank — the loader never fails boot. */

void GameArt_Initialize(void);   /* state only; no I/O (call before scheduler) */

/* Decode progress, for a caller that wants to show it (the boot splash). Reports the
 * tier's own name ("small"/"large") and its file count as the tier starts, then after
 * every cover; wording is the caller's business, not this module's. */
typedef void (*game_art_progress_fn)(const char *tier, uint32_t done, uint32_t total);
void GameArt_SetProgressCallback(game_art_progress_fn fn);

/* Mount the card and decode all covers in both tiers into the caches. Idempotent
 * after the first successful pass. Returns the total number of covers decoded.
 * Must run from a task (blocks on SD I/O) and after Legato's image decoders are
 * up — i.e. during the boot/splash sequence. */
int  GameArt_LoadAll(void);
bool GameArt_IsLoaded(void);

/* Decoded cover for (setlist, index), or NULL if absent/failed. The pointer is
 * stable for the life of the cache (no reload today). */
const leImage *GameArt_Large(uint8_t setlist, uint8_t index);
const leImage *GameArt_Small(uint8_t setlist, uint8_t index);

int  GameArt_CountSmall(void);
int  GameArt_CountLarge(void);

#endif
