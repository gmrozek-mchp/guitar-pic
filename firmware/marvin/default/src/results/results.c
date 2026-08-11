#include "results.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "definitions.h"
#include "log.h"

#include "storage/storage.h"
#include "util/csv.h"

#define RES_REL_DIR   "players"
#define RES_REL_FILE  "players/results.csv"

#define RES_HEADER \
    "player,affiliation,setlist,index,song,difficulty,score,timestamp"

#define RES_LINE_MAX  256
#define RES_FIELDS    8

/* Field indices in a row, matching RES_HEADER. */
#define RES_F_PLAYER      0
#define RES_F_AFFILIATION 1
#define RES_F_SETLIST     2
#define RES_F_INDEX       3
#define RES_F_SONG        4
#define RES_F_DIFFICULTY  5
#define RES_F_SCORE       6
#define RES_F_TIMESTAMP   7

static char            s_player[33] = "HOOMAN";
static results_affil_t s_affil      = RESULTS_AFFIL_EMPLOYEE;

/* ---- helpers ------------------------------------------------------------ */

static void build_path(char *buf, size_t n, const char *rel)
{
    (void)snprintf(buf, n, "%s/%s", Storage_MountPoint(), rel);
}

static void iso8601_utc(char *buf, size_t n)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    RTC_TimeGet(&t);
    (void)snprintf(buf, n, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                   t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                   t.tm_hour, t.tm_min, t.tm_sec);
}

/* ---- player ------------------------------------------------------------- */

void Results_Initialize(void)
{
    /* Default player is the static initializer; nothing else to set up. */
}

void Results_SetPlayer(const char *name)
{
    if (name == NULL || name[0] == '\0') { return; }

    size_t j = 0;
    for (size_t i = 0; name[i] != '\0' && j < sizeof(s_player) - 1; i++)
    {
        char c = name[i];
        if (c == ',' || c == '"' || c == '\n' || c == '\r') { continue; }
        s_player[j++] = c;
    }
    s_player[j] = '\0';
}

const char *Results_GetPlayer(void) { return s_player; }

void Results_SetAffiliation(results_affil_t affil)
{
    if (affil < RESULTS_AFFIL_COUNT) { s_affil = affil; }
}

results_affil_t Results_GetAffiliation(void) { return s_affil; }

const char *Results_AffiliationName(results_affil_t affil)
{
    return (affil == RESULTS_AFFIL_CLIENT) ? "client" : "employee";
}

/* ---- append ------------------------------------------------------------- */

bool Results_Append(const results_record_t *rec)
{
    if (rec == NULL) { return false; }
    if (!Storage_Mount()) { return false; }

    char dir[64];
    build_path(dir, sizeof(dir), RES_REL_DIR);
    (void)SYS_FS_DirectoryMake(dir);   /* harmless if it already exists */

    char path[80];
    build_path(path, sizeof(path), RES_REL_FILE);

    bool need_header = true;
    SYS_FS_FSTAT st;
    memset(&st, 0, sizeof st);   /* lfname MUST be NULL — else FATFS_stat writes
                                  * through it (a garbage stack value → data abort) */
    if (SYS_FS_FileStat(path, &st) == SYS_FS_RES_SUCCESS && st.fsize > 0u)
    {
        need_header = false;
    }

    SYS_FS_HANDLE h = SYS_FS_FileOpen(path, SYS_FS_FILE_OPEN_APPEND);
    if (h == SYS_FS_HANDLE_INVALID)
    {
        LOG_WARN("RES: open append failed (fs err %d)\r\n", (int)SYS_FS_Error());
        return false;
    }

    char line[RES_LINE_MAX];
    int len;

    if (need_header)
    {
        len = snprintf(line, sizeof(line), "%s\n", RES_HEADER);
        if (len > 0 && (size_t)len < sizeof(line))
        {
            (void)SYS_FS_FileWrite(h, line, (size_t)len);
        }
    }

    char ts[24];
    iso8601_utc(ts, sizeof(ts));

    char song_q[96];
    csv_quote(rec->song, song_q, sizeof(song_q));

    len = snprintf(line, sizeof(line),
                   "%s,%s,%s,%u,%s,%s,%lu,%s\n",
                   s_player,
                   Results_AffiliationName(s_affil),
                   (rec->setlist != NULL) ? rec->setlist : "",
                   (unsigned)rec->index,
                   song_q,
                   (rec->difficulty != NULL) ? rec->difficulty : "",
                   (unsigned long)rec->score,
                   ts);

    bool ok = false;
    if (len > 0 && (size_t)len < sizeof(line))
    {
        ok = (SYS_FS_FileWrite(h, line, (size_t)len) == (size_t)len);
    }
    (void)SYS_FS_FileClose(h);
    return ok;
}

/* ---- top-N read --------------------------------------------------------- */

int Results_TopN(const char *setlist, uint8_t index, const char *difficulty,
                 const char *affiliation, results_score_t *out, int max)
{
    if (out == NULL || max <= 0 || setlist == NULL) { return 0; }
    if (!Storage_Mount()) { return 0; }

    char path[80];
    build_path(path, sizeof(path), RES_REL_FILE);

    /* An unopenable file and a file with no matching rows both return 0, and on the
     * dashboard both look the same — an empty board. Say which: with FF_FS_MAX_FILES = 1
     * the interesting failure is another task holding the one file slot, and that is
     * invisible unless it is logged here. */
    SYS_FS_HANDLE h = SYS_FS_FileOpen(path, SYS_FS_FILE_OPEN_READ);
    if (h == SYS_FS_HANDLE_INVALID)
    {
        LOG_WARN("RES: cannot open %s (fs err %d) — no scores this pass\r\n",
                 RES_REL_FILE, (int)SYS_FS_Error());
        return 0;
    }

    int  count = 0;
    bool first = true;
    char line[RES_LINE_MAX];

    while (!SYS_FS_FileEOF(h))
    {
        if (SYS_FS_FileStringGet(h, line, sizeof(line)) != SYS_FS_RES_SUCCESS) { break; }
        if (first)
        {
            /* Validate the schema before trusting any field index below. A file
             * written by an older schema has the columns in other positions, so
             * parsing it would yield a wrong-but-plausible table. */
            first = false;
            size_t n = strlen(line);
            while (n > 0u && (line[n - 1] == '\n' || line[n - 1] == '\r')) { line[--n] = '\0'; }
            if (strcmp(line, RES_HEADER) != 0)
            {
                LOG_WARN("RES: results.csv header mismatch — ignoring the file\r\n");
                LOG_WARN("RES:   found    '%s'\r\n", line);
                LOG_WARN("RES:   expected '%s'\r\n", RES_HEADER);
                (void)SYS_FS_FileClose(h);
                return 0;
            }
            continue;
        }
        if (line[0] == '\0' || line[0] == '\n' || line[0] == '\r') { continue; }

        char *f[RES_FIELDS];
        if (csv_split(line, f, RES_FIELDS) < RES_FIELDS) { continue; }

        if (strcmp(f[RES_F_SETLIST], setlist) != 0) { continue; }
        if ((unsigned)atoi(f[RES_F_INDEX]) != (unsigned)index) { continue; }
        if (difficulty != NULL && difficulty[0] != '\0' &&
            strcmp(f[RES_F_DIFFICULTY], difficulty) != 0) { continue; }
        if (affiliation != NULL && affiliation[0] != '\0' &&
            strcmp(f[RES_F_AFFILIATION], affiliation) != 0) { continue; }

        uint32_t score = (uint32_t)strtoul(f[RES_F_SCORE], NULL, 10);

        /* Insert into the bounded, score-descending top list. */
        int pos = count;
        for (int i = 0; i < count; i++)
        {
            if (score > out[i].score) { pos = i; break; }
        }
        if (pos >= max) { continue; }

        int last = (count < max) ? count : (max - 1);
        for (int i = last; i > pos; i--) { out[i] = out[i - 1]; }

        strncpy(out[pos].player, f[RES_F_PLAYER], sizeof(out[pos].player) - 1);
        out[pos].player[sizeof(out[pos].player) - 1] = '\0';
        strncpy(out[pos].timestamp, f[RES_F_TIMESTAMP], sizeof(out[pos].timestamp) - 1);
        out[pos].timestamp[sizeof(out[pos].timestamp) - 1] = '\0';
        out[pos].score = score;

        if (count < max) { count++; }
    }

    (void)SYS_FS_FileClose(h);
    return count;
}
