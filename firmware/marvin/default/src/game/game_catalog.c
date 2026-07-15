#include "game/game_catalog.h"

#include <stdlib.h>
#include <string.h>

#include "definitions.h"
#include "log.h"

#include "storage/storage.h"
#include "util/csv.h"
#include "game/gameplay_metadata.h"   /* GP_SETLIST_*, GP_N_SONGS */

#define CAT_REL_FILE  "games/gh3-wii/songs.csv"
#define CAT_LINE_MAX  256
#define CAT_FIELDS    10  /* setlist,index,title,artist,album,bpm,length_s,year,genre,difficulty */

static game_catalog_entry_t s_entries[GP_N_SONGS];
static int             s_count  = 0;
static bool            s_loaded = false;

/* ---- helpers ------------------------------------------------------------ */

static void build_path(char *buf, size_t n, const char *rel)
{
    (void)snprintf(buf, n, "%s/%s", Storage_MountPoint(), rel);
}

/* "main"/"bonus" -> GP_SETLIST_*, or -1 for an unrecognized setlist. */
static int setlist_id(const char *s)
{
    if (strcmp(s, "main")  == 0) { return GP_SETLIST_MAIN; }
    if (strcmp(s, "bonus") == 0) { return GP_SETLIST_BONUS; }
    return -1;
}

static void copy_field(char *dst, size_t n, const char *src)
{
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

/* ---- load --------------------------------------------------------------- */

void GameCatalog_Initialize(void)
{
    s_count  = 0;
    s_loaded = false;
}

bool GameCatalog_Reload(void)
{
    s_count  = 0;
    s_loaded = true;   /* one attempt; lookups won't re-load until next Reload */

    if (!Storage_Mount()) { return false; }

    char path[80];
    build_path(path, sizeof(path), CAT_REL_FILE);

    SYS_FS_HANDLE h = SYS_FS_FileOpen(path, SYS_FS_FILE_OPEN_READ);
    if (h == SYS_FS_HANDLE_INVALID) { return false; }

    bool first = true;
    char line[CAT_LINE_MAX];

    while (!SYS_FS_FileEOF(h) && s_count < GP_N_SONGS)
    {
        if (SYS_FS_FileStringGet(h, line, sizeof(line)) != SYS_FS_RES_SUCCESS) { break; }
        if (first) { first = false; continue; }   /* header row */
        if (line[0] == '\0' || line[0] == '\n' || line[0] == '\r') { continue; }

        char *f[CAT_FIELDS];
        if (csv_split(line, f, CAT_FIELDS) < CAT_FIELDS) { continue; }

        int sl = setlist_id(f[0]);
        if (sl < 0) { continue; }

        game_catalog_entry_t *e = &s_entries[s_count];
        e->setlist  = (uint8_t)sl;
        e->index    = (uint8_t)atoi(f[1]);
        copy_field(e->title,  sizeof(e->title),  f[2]);
        copy_field(e->artist, sizeof(e->artist), f[3]);
        copy_field(e->album,  sizeof(e->album),  f[4]);
        e->bpm      = (uint16_t)strtoul(f[5], NULL, 10);
        e->length_s = (uint16_t)strtoul(f[6], NULL, 10);
        e->year     = (uint16_t)strtoul(f[7], NULL, 10);
        copy_field(e->genre,      sizeof(e->genre),      f[8]);
        copy_field(e->difficulty, sizeof(e->difficulty), f[9]);
        s_count++;
    }

    (void)SYS_FS_FileClose(h);
    LOG_INFO("CAT: loaded %d song(s)\r\n", s_count);
    return true;
}

/* ---- lookup ------------------------------------------------------------- */

bool GameCatalog_Lookup(uint8_t setlist, uint8_t index, game_catalog_entry_t *out)
{
    if (out == NULL) { return false; }
    if (!s_loaded) { (void)GameCatalog_Reload(); }

    for (int i = 0; i < s_count; i++)
    {
        if (s_entries[i].setlist == setlist && s_entries[i].index == index)
        {
            *out = s_entries[i];
            return true;
        }
    }
    return false;
}

bool GameCatalog_LookupSong(const gp_song_t *song, game_catalog_entry_t *out)
{
    if (song == NULL) { return false; }
    return GameCatalog_Lookup(song->setlist, song->index, out);
}

int  GameCatalog_Count(void)    { return s_count; }
bool GameCatalog_IsLoaded(void) { return s_loaded; }

const game_catalog_entry_t *GameCatalog_At(int i)
{
    if (i < 0 || i >= s_count) { return NULL; }
    return &s_entries[i];
}
