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
 * This records **human** performance, which is why a row is only ever a
 * 2-player run: 1P ROBOT is marvin playing alone, and 1P HUMAN is retired. The
 * `score` is therefore the human's amp scoreboard, not marvin's — marvin's total
 * lives on the dashboard and in the run log, deliberately unpersisted.
 *
 * Builds entirely on the SD/FatFs/RTC layer (storage.c); no gameplay dependency,
 * so the write + read + display path is exercisable from the console (`results
 * add` / `results top`) independently of a real run. */

void Results_Initialize(void);

/* Current player name (operator-entered; commas/quotes/newlines stripped to keep
 * the CSV clean). Defaults to "p1". Used for the player column on append. */
void        Results_SetPlayer(const char *name);
const char *Results_GetPlayer(void);

/* Which leaderboard the run belongs to, chosen on the role dialog straight after the
 * name. Session state like the player name, not a field of the record, because both are
 * answered once per player and then hold for every run they play.
 *
 * Defaults to EMPLOYEE deliberately: the dashboard's board shows clients only, so a row
 * written without anyone having answered the dialog (a console `results add`, a flow that
 * grows a new entry point later) stays off the customer-facing board rather than landing
 * on it unattributed. Erring the other way would put unknown rows in front of visitors. */
typedef enum
{
    RESULTS_AFFIL_CLIENT = 0,   /* client / partner   */
    RESULTS_AFFIL_EMPLOYEE,     /* microchip employee */
    RESULTS_AFFIL_COUNT
} results_affil_t;

void            Results_SetAffiliation(results_affil_t affil);
results_affil_t Results_GetAffiliation(void);

/* The CSV spelling: "client" | "employee". Stable lowercase keys — they are written to the
 * file and matched against it on read, so they are not display strings and must not be
 * localized. Out-of-range returns the EMPLOYEE spelling, matching the default. */
const char     *Results_AffiliationName(results_affil_t affil);

/* One completed run. Song + score is the whole point; anything marvin cannot read
 * off the screen is deliberately absent rather than written as a zero column.
 * `setlist`/`index` are the recognizer's stable song key and what Results_TopN
 * filters on; `song` is carried too so the CSV reads standalone on a PC. */
typedef struct
{
    const char *setlist;      /* "main" | "bonus" */
    uint8_t     index;        /* song ordinal within its setlist */
    const char *song;         /* human title; CSV-quoted on write (may contain ,) */
    const char *difficulty;   /* "easy".."expert" */
    uint32_t    score;        /* the human player's score */
} results_record_t;

/* Append one completed run as a CSV row: mounts on demand, writes the header if
 * the file is new, stamps the timestamp as UTC from the RTC, and uses
 * Results_GetPlayer() for the player column. Returns true on success. */
bool Results_Append(const results_record_t *rec);

/* One high-score line for display. */
typedef struct
{
    char     player[33];
    char     timestamp[24];
    uint32_t score;
} results_score_t;

/* Fill out[0..max) with the highest scores for a song (filtered by difficulty and by
 * affiliation when each is non-NULL/non-empty), highest first. Returns the count filled.
 * `affiliation` takes a Results_AffiliationName() spelling; NULL ranks everyone together.
 *
 * Validates the file's header row against the schema this build writes and
 * returns 0 on a mismatch: the parser reads fixed field indices, so a file from
 * an older schema would otherwise be silently misparsed into a plausible-looking
 * but wrong high-score table. A warning names the header it found. */
int Results_TopN(const char *setlist, uint8_t index, const char *difficulty,
                 const char *affiliation, results_score_t *out, int max);

#endif
