#include "game/game_showdown.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "definitions.h"
#include "log.h"

#include "game/game_selection.h"
#include "game/gameplay_select.h"   /* gp_song_template_of */
#include "storage/storage.h"

#define SHDN_REL_FILE  "games/gh3-wii/showdown.cfg"
#define SHDN_LINE_MAX  128

static showdown_cfg_t s_cfg;

static results_score_t s_top[SHOWDOWN_TOP_MAX];
static int             s_ntop;

/* ---- text helpers ------------------------------------------------------- */

/* The setlist spelling results.csv and the logs use. */
static const char *setlist_name(uint8_t setlist)
{
    return (setlist == (uint8_t)GP_SETLIST_BONUS) ? "bonus" : "main";
}

/* Case-insensitive equality with a lowercase literal — the file is hand-typed, so
 * `Main`/`HARD` have to read the same as `main`/`hard`. */
static bool ieq(const char *s, const char *lower)
{
    size_t i;
    for (i = 0; lower[i] != '\0'; i++)
    {
        if (tolower((unsigned char)s[i]) != lower[i]) { return false; }
    }
    return s[i] == '\0';
}

/* Drop a trailing comment, then trim both ends in place. Returns the new start. */
static char *clean(char *s)
{
    char *cut = strpbrk(s, "#;");
    if (cut != NULL) { *cut = '\0'; }

    while (*s != '\0' && isspace((unsigned char)*s)) { s++; }

    size_t n = strlen(s);
    while (n > 0u && isspace((unsigned char)s[n - 1u])) { s[--n] = '\0'; }
    return s;
}

/* A whole-string unsigned parse: rejects "4x", "" and anything signed, so a typo in
 * the index is a rejected config rather than a silently truncated one. */
static bool parse_u8(const char *s, uint8_t *out)
{
    if (!isdigit((unsigned char)*s)) { return false; }

    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (end == NULL || *end != '\0' || v > 255ul) { return false; }

    *out = (uint8_t)v;
    return true;
}

/* ---- value parsers ------------------------------------------------------ */

static bool parse_setlist(const char *s, uint8_t *out)
{
    if (ieq(s, "main"))  { *out = (uint8_t)GP_SETLIST_MAIN;  return true; }
    if (ieq(s, "bonus")) { *out = (uint8_t)GP_SETLIST_BONUS; return true; }
    return false;
}

/* `<main|bonus>,<index>` — one value, because the pair is one key: neither half means
 * anything alone. */
static bool parse_song(char *val, uint8_t *setlist, uint8_t *index)
{
    char *comma = strchr(val, ',');
    if (comma == NULL) { return false; }
    *comma = '\0';

    return parse_setlist(clean(val), setlist) && parse_u8(clean(comma + 1), index);
}

/* A name ("hard") or the tier number the difficulty menu uses (0..3). Names come from
 * GameSelection_DifficultyName so this can't drift from the results CSV's spelling. */
static bool parse_difficulty(const char *s, uint8_t *out)
{
    for (uint8_t d = 0u; d < 4u; d++)
    {
        if (ieq(s, GameSelection_DifficultyName(d))) { *out = d; return true; }
    }

    uint8_t tier = 0u;
    if (!parse_u8(s, &tier) || tier >= 4u) { return false; }
    *out = tier;
    return true;
}

/* ---- load --------------------------------------------------------------- */

bool Showdown_Reload(void)
{
    uint8_t setlist = 0u, index = 0u, difficulty = 0u;
    bool    have_song = false, have_diff = false;

    /* Built from the file alone — a failed or partial read never leaves the previous
     * config in place, since the button's whole contract is that it is absent unless
     * the card currently says otherwise. The high-score board is scoped to the config,
     * so it is dropped on the same terms. */
    s_cfg.valid = false;
    s_ntop      = 0;

    if (!Storage_Mount()) { LOG_WARN("SHDN: no card\r\n"); return false; }

    char path[80];
    (void)snprintf(path, sizeof path, "%s/%s", Storage_MountPoint(), SHDN_REL_FILE);

    SYS_FS_HANDLE h = SYS_FS_FileOpen(path, SYS_FS_FILE_OPEN_READ);
    if (h == SYS_FS_HANDLE_INVALID)
    {
        LOG_WARN("SHDN: no %s - SHOWDOWN hidden\r\n", SHDN_REL_FILE);
        return false;
    }

    char line[SHDN_LINE_MAX];
    while (!SYS_FS_FileEOF(h))
    {
        if (SYS_FS_FileStringGet(h, line, sizeof line) != SYS_FS_RES_SUCCESS) { break; }

        char *body = clean(line);
        if (*body == '\0') { continue; }

        char *eq = strchr(body, '=');
        if (eq == NULL)
        {
            LOG_WARN("SHDN: ignoring line without '=': %s\r\n", body);
            continue;
        }
        *eq = '\0';

        const char *key = clean(body);
        char       *val = clean(eq + 1);

        if (ieq(key, "song"))
        {
            have_song = parse_song(val, &setlist, &index);
            if (!have_song) { LOG_WARN("SHDN: bad song value (want <main|bonus>,<index>)\r\n"); }
        }
        else if (ieq(key, "difficulty"))
        {
            have_diff = parse_difficulty(val, &difficulty);
            if (!have_diff) { LOG_WARN("SHDN: bad difficulty '%s'\r\n", val); }
        }
        else
        {
            LOG_WARN("SHDN: ignoring unknown key '%s'\r\n", key);
        }
    }
    (void)SYS_FS_FileClose(h);

    if (!have_song || !have_diff)
    {
        LOG_WARN("SHDN: %s missing - SHOWDOWN hidden\r\n",
                 !have_song ? "song" : "difficulty");
        return false;
    }

    /* The song has to be one the recognizer has a template for, or navigation would
     * strum toward a slot that never arrives. Checked here so a bad index is a hidden
     * button instead of a failed run. */
    if (gp_song_template_of(setlist, index) < 0)
    {
        LOG_WARN("SHDN: no such song %s %u - SHOWDOWN hidden\r\n",
                 setlist_name(setlist), (unsigned)index);
        return false;
    }

    s_cfg.setlist    = setlist;
    s_cfg.index      = index;
    s_cfg.difficulty = difficulty;
    s_cfg.valid      = true;

    LOG_INFO("SHDN: %s %u, %s\r\n", setlist_name(setlist), (unsigned)index,
             GameSelection_DifficultyName(difficulty));

    /* The board belongs to the match, so it is loaded with it: every path that learns the
     * config has changed then has the right scores without a second call to remember. */
    (void)Showdown_ReloadTop();
    return true;
}

/* ---- high scores -------------------------------------------------------- */

int Showdown_ReloadTop(void)
{
    s_ntop = 0;
    memset(s_top, 0, sizeof s_top);

    if (!s_cfg.valid) { return 0; }

    /* Filtered by difficulty as well as song: an easy run and an expert run of the same
     * track do not belong on one leaderboard. Clients/partners only — see the header. */
    s_ntop = Results_TopN(setlist_name(s_cfg.setlist), s_cfg.index,
                          GameSelection_DifficultyName(s_cfg.difficulty),
                          Results_AffiliationName(RESULTS_AFFIL_CLIENT),
                          s_top, SHOWDOWN_TOP_MAX);
    return s_ntop;
}

int Showdown_TopCount(void) { return s_ntop; }

const results_score_t *Showdown_Top(int i)
{
    return (i >= 0 && i < s_ntop) ? &s_top[i] : NULL;
}

const showdown_cfg_t *Showdown_Get(void)
{
    return &s_cfg;
}

bool Showdown_Commit(void)
{
    if (!s_cfg.valid) { return false; }

    GameSelection_Set(s_cfg.setlist, s_cfg.index, s_cfg.difficulty, (uint8_t)GAME_MODE_2P);
    return true;
}
