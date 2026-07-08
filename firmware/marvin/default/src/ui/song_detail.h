#ifndef UI_SONG_DETAIL_H
#define UI_SONG_DETAIL_H

#include <stddef.h>
#include <stdint.h>

#include "gfx/legato/generated/le_gen_scheme.h"   /* leScheme */

/* Shared song-detail formatting used by both the song-select dialog and the
 * dashboard SONG card: the career tier (from the catalog difficulty column) and
 * the m:ss duration. Keeps the star/tier palette and duration format in one place. */

/* Catalog difficulty string ("1".."8" or "bonus"/empty) → career tier 1-8, or 0
 * (bonus / unknown). */
int SongDetail_Tier(const char *difficulty);

/* UTF-8 tier text: `tier` black stars (U+2605), or "BONUS" for 0. */
void SongDetail_TierText(int tier, char *buf, size_t n);

/* Text color scheme for a tier: SCHEME_TEXT_TIER_1..8, or light gray for bonus. */
const leScheme *SongDetail_TierScheme(int tier);

/* Duration as "m:ss", or "-" when length_s is 0 (unknown). */
void SongDetail_Duration(uint16_t length_s, char *buf, size_t n);

#endif /* UI_SONG_DETAIL_H */
