#ifndef MARVIN_RESULTS_H
#define MARVIN_RESULTS_H

#include <stdbool.h>
#include <stdint.h>

/* Per-player gameplay results -> <card>/players/results.csv (spec §4.8.6).
 *
 * Flat, append-only CSV: one row per completed run, header row on first write.
 * Marvin reads it back on-device for high scores; the card is also taken to a
 * PC for analysis (pandas/Excel). CSV is the format that serves both — on-device
 * it parses with comma-splitting + atol into static buffers (no malloc, no JSON
 * tokenizer), which fits FF_FS_MAX_FILES=1 and the static-allocation rule.
 *
 * Builds entirely on the SD/FatFs/RTC layer (storage.c); no gameplay dependency,
 * so the write + read + display path is testable now with synthetic rows. The
 * gameplay engine wires in later by filling a record and calling Results_Append
 * once the M9 Phase 3 score readers exist. */

void Results_Initialize(void);

/* Current player name (operator-entered; commas/quotes/newlines stripped to keep
 * the CSV clean). Defaults to "p1". Used for the player column on append. */
void        Results_SetPlayer(const char *name);
const char *Results_GetPlayer(void);

typedef struct
{
    const char *game;         /* e.g. "gh3-wii" */
    const char *setlist;      /* "main" | "bonus" */
    uint8_t     index;        /* song ordinal within its setlist */
    const char *song;         /* human title; CSV-quoted on write (may contain ,) */
    const char *difficulty;   /* "easy".."expert" */
    const char *part;         /* "lead" | "bass" | ... */
    uint32_t    score;
    uint16_t    accuracy_x10; /* accuracy percent * 10 (924 => 92.4%) */
    uint16_t    notes_hit;
    uint16_t    notes_total;
} results_record_t;

/* Append one completed run as a CSV row: mounts on demand, writes the header if
 * the file is new, stamps the timestamp as UTC from the RTC, and uses
 * Results_GetPlayer() for the player column. Returns true on success. */
bool Results_Append(const results_record_t *rec);

/* One high-score line for display. */
typedef struct
{
    char     player[24];
    char     timestamp[24];
    uint32_t score;
} results_score_t;

/* Fill out[0..max) with the highest scores for a song (filtered by difficulty
 * when non-NULL/non-empty), highest first. Returns the count filled. */
int Results_TopN(const char *setlist, uint8_t index, const char *difficulty,
                 results_score_t *out, int max);

#endif
