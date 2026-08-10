#ifndef MARVIN_GAME_SHOWDOWN_H
#define MARVIN_GAME_SHOWDOWN_H

#include <stdbool.h>
#include <stdint.h>

#include "results/results.h"   /* results_score_t — the match's high-score board */

/* The 2-player match the dashboard's SHOWDOWN button starts, read from
 * <card>/games/gh3-wii/showdown.cfg so the demo can be re-pointed at another song
 * without a firmware rebuild.
 *
 * A tolerant key=value file (`song = main,4` / `difficulty = hard`), parsed into a
 * static cache. The song is keyed by the recognizer's stable (setlist, index) — the
 * same key songs.csv uses — and is only accepted if a template exists for it, so an
 * unreachable song is rejected at load rather than failing mid-navigation.
 *
 * There is no fallback: with no usable config the dashboard hides the button
 * entirely, which is why ->valid is the only thing callers gate on. */

typedef struct
{
    bool    valid;       /* false = no usable config on the card */
    uint8_t setlist;     /* GP_SETLIST_* */
    uint8_t index;       /* song ordinal within its setlist */
    uint8_t difficulty;  /* game_difficulty_t */
} showdown_cfg_t;

/* Re-read the card and return whether a usable config is now cached. Blocks on SD
 * I/O, so call from a task. Every rejection logs its reason. */
bool Showdown_Reload(void);

/* The cached config; ->valid is false before the first Reload and after a failed one. */
const showdown_cfg_t *Showdown_Get(void);

/* Commit the cached config as the gameplay selection, forced to 2-player. Returns
 * false (committing nothing) if there is no valid config. Does no I/O — pair it with
 * a Reload when freshness matters. */
bool Showdown_Commit(void);

/* The match's high-score board — the best runs on the card for the cached song and
 * difficulty, which is what the dashboard shows above the SHOWDOWN button.
 *
 * Kept here rather than in results/ because the *scope* is the config: these are only
 * comparable numbers because every one of them is the same song at the same difficulty,
 * and the config is what decides which. Reload therefore refreshes both together, and an
 * invalid config leaves an empty board rather than a stale one.
 *
 * **Clients/partners only** (RESULTS_AFFIL_CLIENT). This board faces the visitor standing
 * at the machine, and staff rehearsing the demo would otherwise fill all three rows and
 * leave nothing for them to beat. Employee scores are not lost — song-select ranks both
 * groups side by side. */
#define SHOWDOWN_TOP_MAX  3

/* Re-read the card's results for the cached match. Blocks on SD I/O, so call from a task,
 * and never with a render lock held. Returns the count now cached (0 with no valid config,
 * and 0 is also the answer for a match nobody has played yet). */
int Showdown_ReloadTop(void);

/* The cached board, highest first: 0..SHOWDOWN_TOP_MAX entries, i < Showdown_TopCount().
 * Showdown_Top returns NULL out of range. Neither does I/O. */
int                     Showdown_TopCount(void);
const results_score_t  *Showdown_Top(int i);

#endif /* MARVIN_GAME_SHOWDOWN_H */
