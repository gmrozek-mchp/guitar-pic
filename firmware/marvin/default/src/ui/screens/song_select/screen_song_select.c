#include "ui/screens/song_select/screen_song_select.h"

#include <stdio.h>

#include "ui/ui_manager.h"   /* CANVAS_SONGSEL, BASE_W, BASE_H */
#include "ui/widgets/button_aa/widget_button_aa.h"
#include "ui/widgets/panel_aa/widget_panel_aa.h"
#include "ui/widgets/song_list/widget_song_list.h"

#include "game/catalog.h"
#include "game/art.h"
#include "log.h"
#include "util/legato_utf8.h"

#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/string/legato_fixedstring.h"                /* runtime label text */
#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"                  /* DejaVu fonts */
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* song-select widgets */

/* The song/mode-select dialog is layer 2 of the Marvin screen; MGS sizes that
 * layer's root to the dialog (1100x660) and renders it into CANVAS_SONGSEL. The
 * canvas is smaller than the panel, so the canvas window places it centered on the
 * 1280x800 display. */
#define SONGSEL_W   1100u
#define SONGSEL_H    660u
#define SONGSEL_X   ((int)((BASE_W - SONGSEL_W) / 2u))   /* 90  */
#define SONGSEL_Y   ((int)((BASE_H - SONGSEL_H) / 2u))   /* 70  */

#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

static uint16_t FB_NOCACHE s_fb_songsel[SONGSEL_W * SONGSEL_H];

void ScreenSongSelect_InitSurface(void)
{
    gfxcSetPixelBuffer(CANVAS_SONGSEL, SONGSEL_W, SONGSEL_H, GFX_COLOR_MODE_RGB_565, s_fb_songsel);
}

/* ── difficulty + mode radio groups ──────────────────────────────────────────
 * The four DIFFICULTY buttons are one single-select group, the three MODE buttons
 * another. MGS builds them as plain push buttons all sharing one unselected scheme
 * (SCHEME_BUTTON_DIFFICULTY / SCHEME_BUTTON_MODE); the radio behaviour is driven
 * here by swapping schemes on release. Difficulty's selected look is per-button
 * (Easy→green … Expert→red); mode uses one shared selected scheme. The selection
 * persists in s_difficulty / s_mode — the source of truth the repaint reads. */
#define DIFFICULTY_COUNT    4u
#define MODE_COUNT          3u
#define DIFFICULTY_DEFAULT  0u   /* Easy      */
#define MODE_DEFAULT        0u   /* 1P robot  */

/* Corner radii (px) for the dialog's AA'd buttons. */
#define DIFFICULTY_RADIUS   4u
#define MODE_RADIUS         4u
#define SELECT_RADIUS       10u

/* Corner radius (px) for the large album-art strip. */
#define ALBUM_ART_RADIUS    14u

static unsigned int s_difficulty = DIFFICULTY_DEFAULT;
static unsigned int s_mode       = MODE_DEFAULT;

static leButtonWidget *difficulty_button(unsigned int i)
{
    switch (i)
    {
        case 0:  return Marvin_BUTTON_SONG_SELECT_EASY;
        case 1:  return Marvin_BUTTON_SONG_SELECT_MEDIUM;
        case 2:  return Marvin_BUTTON_SONG_SELECT_HARD;
        default: return Marvin_BUTTON_SONG_SELECT_EXPERT;
    }
}

static const leScheme *difficulty_selected_scheme(unsigned int i)
{
    switch (i)
    {
        case 0:  return &SCHEME_BUTTON_EASY;
        case 1:  return &SCHEME_BUTTON_MEDIUM;
        case 2:  return &SCHEME_BUTTON_HARD;
        default: return &SCHEME_BUTTON_EXPERT;
    }
}

static leButtonWidget *mode_button(unsigned int i)
{
    switch (i)
    {
        case 0:  return Marvin_BUTTON_SONG_SELECT_1P_ROBOT;
        case 1:  return Marvin_BUTTON_SONG_SELECT_1P_HUMAN;
        default: return Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN;
    }
}

/* Paint the active button in each group with its selected scheme, the rest with
 * the group's shared unselected scheme. */
static void difficulty_repaint(void)
{
    unsigned int i;
    for (i = 0u; i < DIFFICULTY_COUNT; i++)
    {
        leButtonWidget *b = difficulty_button(i);
        b->fn->setScheme(b, (i == s_difficulty) ? difficulty_selected_scheme(i)
                                                 : &SCHEME_BUTTON_DIFFICULTY);
    }
}

static void mode_repaint(void)
{
    unsigned int i;
    for (i = 0u; i < MODE_COUNT; i++)
    {
        leButtonWidget *b = mode_button(i);
        b->fn->setScheme(b, (i == s_mode) ? &SCHEME_BUTTON_MODE_SELECTED
                                          : &SCHEME_BUTTON_MODE);
    }
}

/* Released-event sinks: select the pressed entry and repaint its group. Re-picking
 * the active entry is a no-op (radio: no deselect). */
static void difficulty_on_release(leButtonWidget *btn)
{
    unsigned int i;
    for (i = 0u; i < DIFFICULTY_COUNT; i++)
    {
        if (btn == difficulty_button(i)) { s_difficulty = i; difficulty_repaint(); return; }
    }
}

static void mode_on_release(leButtonWidget *btn)
{
    unsigned int i;
    for (i = 0u; i < MODE_COUNT; i++)
    {
        if (btn == mode_button(i)) { s_mode = i; mode_repaint(); return; }
    }
}

/* Round the corners and enable anti-aliased smoothing on a button (not an MGS
 * option). ButtonAA smooths at whatever cornerRadius the button has, so set the
 * radius first. */
static void round_button(leButtonWidget *b, uint32_t radius)
{
    b->fn->setCornerRadius(b, radius);
    ButtonAA_Enable(b);
}

static void radio_groups_init(void)
{
    unsigned int i;

    for (i = 0u; i < DIFFICULTY_COUNT; i++)
    {
        leButtonWidget *b = difficulty_button(i);
        b->fn->setReleasedEventCallback(b, difficulty_on_release);
        round_button(b, DIFFICULTY_RADIUS);
    }
    for (i = 0u; i < MODE_COUNT; i++)
    {
        leButtonWidget *b = mode_button(i);
        b->fn->setReleasedEventCallback(b, mode_on_release);
        round_button(b, MODE_RADIUS);
    }

    difficulty_repaint();   /* apply the default selections */
    mode_repaint();
}

/* ── selected-song detail labels ──────────────────────────────────────────────
 * The dialog's detail labels mirror the selected catalog entry. MGS gives each a
 * static table string; we swap in a per-label leFixedString (static buffer, no
 * heap) and rewrite it on selection. setString only references the string, so the
 * fixed strings must persist (file-scope) — and Legato strings carry their own
 * font, so each fixed string inherits the font from the table string it replaces. */
enum { DET_LEVEL, DET_TITLE, DET_ARTIST, DET_ALBUM, DET_YEAR, DET_GENRE, DET_DURATION, DET_COUNT };

#define DETAIL_CAP  64

static leChar        s_detail_buf[DET_COUNT][DETAIL_CAP];
static leFixedString s_detail_str[DET_COUNT];

static leLabelWidget *detail_label(int i)
{
    switch (i)
    {
        case DET_LEVEL:  return Marvin_LABEL_SONG_SELECT_SongTier;
        case DET_TITLE:  return Marvin_LABEL_SONG_SELECT_SongTitle;
        case DET_ARTIST: return Marvin_LABEL_SONG_SELECT_Artist;
        case DET_ALBUM:  return Marvin_LABEL_SONG_SELECT_SongAlbum;
        case DET_YEAR:   return Marvin_LABEL_SONG_SELECT_SongYear;
        case DET_GENRE:  return Marvin_LABEL_SONG_SELECT_SongGenre;
        default:         return Marvin_LABEL_SONG_SELECT_SongDuration;
    }
}

/* Point each detail label at its fixed string, inheriting the MGS-assigned font. */
static void song_detail_init(void)
{
    int i;
    for (i = 0; i < DET_COUNT; i++)
    {
        leLabelWidget *lbl = detail_label(i);
        leString      *fs  = (leString *)&s_detail_str[i];
        leString      *cur = lbl->fn->getString(lbl);

        leFixedString_Constructor(&s_detail_str[i], s_detail_buf[i], DETAIL_CAP);
        /* The tier label is DejaVuSansMonoBold 12pt (carries the U+2605 ★ glyph);
         * the others inherit the font MGS assigned to their table string. */
        if (i == DET_LEVEL) { fs->fn->setFont(fs, (leFont *)&DejaVuSansMonoBold_12); }
        else if (cur != NULL) { fs->fn->setFont(fs, cur->fn->getFont(cur)); }
        lbl->fn->setString(lbl, fs);
    }
}

/* Set a detail value; "-" for an empty/unknown field. The label repaints via the
 * string's invalidate callback (installed by setString). */
static void set_detail(int i, const char *s)
{
    leString *fs = (leString *)&s_detail_str[i];
    (void)lestring_set_utf8(fs, (s != NULL && s[0] != '\0') ? s : "-");
}

/* Point the dialog's album-art widget at the selected song's pre-decoded large
 * strip, or blank it (NULL) when the song or its cover is absent. */
static void song_art_show(const catalog_entry_t *e)
{
    const leImage *img = (e != NULL) ? Art_Large(e->setlist, e->index) : NULL;
    Marvin_IMAGE_ALBUM_ART->fn->setImage(Marvin_IMAGE_ALBUM_ART, (leImage *)img);
}

/* ── tier label ───────────────────────────────────────────────────────────────
 * SONG_LEVEL shows "★…★ TIER n" for main career tiers 1-8 (n black stars), or
 * "BONUS" for the bonus setlist, colored by difficulty: an 8-tier palette
 * (green(1)→red(8) with blue/purple accents), light gray for bonus. The per-tier
 * text colors live in MGS schemes SCHEME_TEXT_TIER_1..SCHEME_TEXT_TIER_8 (text
 * color = the tier color, matching the art fade); bonus reuses SCHEME_TEXT_GRAY_D4D4D8. */
static const leScheme *tier_scheme(int tier)
{
    switch (tier)
    {
        case 1:  return &SCHEME_TEXT_TIER_1;
        case 2:  return &SCHEME_TEXT_TIER_2;
        case 3:  return &SCHEME_TEXT_TIER_3;
        case 4:  return &SCHEME_TEXT_TIER_4;
        case 5:  return &SCHEME_TEXT_TIER_5;
        case 6:  return &SCHEME_TEXT_TIER_6;
        case 7:  return &SCHEME_TEXT_TIER_7;
        case 8:  return &SCHEME_TEXT_TIER_8;
        default: return &SCHEME_TEXT_GRAY_D4D4D8;   /* bonus / unknown → light gray */
    }
}

/* Catalog difficulty string ("1".."8" or "bonus") → career tier, or 0 (bonus). */
static int difficulty_tier(const char *d)
{
    if (d != NULL && d[0] >= '1' && d[0] <= '8' && d[1] == '\0') { return d[0] - '0'; }
    return 0;
}

/* UTF-8 tier text: `tier` black stars (U+2605) + " TIER n", or "BONUS" for 0. */
static void make_tier_text(int tier, char *buf, size_t n)
{
    size_t p = 0;
    if (tier == 0) { (void)snprintf(buf, n, "BONUS"); return; }
    for (int i = 0; i < tier && p + 3u < n; i++)
    {
        buf[p++] = (char)0xE2; buf[p++] = (char)0x98; buf[p++] = (char)0x85;   /* U+2605 ★ */
    }
    (void)snprintf(buf + p, n - p, " TIER %d", tier);
}

/* Set SONG_LEVEL's text + color from the song's difficulty. */
static void tier_label_show(const catalog_entry_t *e)
{
    leLabelWidget *lbl  = detail_label(DET_LEVEL);
    int            tier = (e != NULL) ? difficulty_tier(e->difficulty) : 0;
    char           txt[40];

    if (e == NULL)
    {
        set_detail(DET_LEVEL, "-");
        lbl->fn->setScheme(lbl, &SCHEME_TEXT_GRAY_D4D4D8);
        return;
    }
    make_tier_text(tier, txt, sizeof txt);
    set_detail(DET_LEVEL, txt);
    lbl->fn->setScheme(lbl, tier_scheme(tier));
}

/* Mirror catalog entry `index` into the detail labels (all "-" if no such song). */
static void song_detail_show(int index)
{
    const catalog_entry_t *e = Catalog_At(index);
    char tmp[16];

    song_art_show(e);
    tier_label_show(e);   /* DET_LEVEL: tier text + difficulty color */

    if (e == NULL)
    {
        int i;
        for (i = DET_TITLE; i < DET_COUNT; i++) { set_detail(i, "-"); }
        return;
    }

    set_detail(DET_TITLE,  e->title);
    set_detail(DET_ARTIST, e->artist);
    set_detail(DET_ALBUM,  e->album);
    set_detail(DET_GENRE,  e->genre);

    if (e->year != 0u)
    {
        (void)snprintf(tmp, sizeof tmp, "%u", (unsigned)e->year);
        set_detail(DET_YEAR, tmp);
    }
    else { set_detail(DET_YEAR, "-"); }

    if (e->length_s != 0u)
    {
        (void)snprintf(tmp, sizeof tmp, "%u:%02u",
                       (unsigned)(e->length_s / 60u), (unsigned)(e->length_s % 60u));
        set_detail(DET_DURATION, tmp);
    }
    else { set_detail(DET_DURATION, "-"); }
}

/* ── catalog-backed song list ─────────────────────────────────────────────────
 * A SongList widget fills the dialog's LEFT panel below the 33px SETLIST header,
 * backed directly by the SD song catalog. */
#define SONGLIST_HEADER_H  33   /* SETLIST header above the list */
#define SONGLIST_ROW_H     44

/* Row provider: map catalog entry `index` into the list's row cells. The duration
 * is formatted into a static scratch buffer the widget reads immediately. */
static bool song_row(void *ctx, int index, songlist_row_t *out)
{
    const catalog_entry_t *e = Catalog_At(index);
    static char dur[8];

    (void)ctx;
    if (e == NULL) { return false; }

    if (e->length_s != 0u)
    {
        (void)snprintf(dur, sizeof dur, "%u:%02u",
                       (unsigned)(e->length_s / 60u), (unsigned)(e->length_s % 60u));
    }
    else { dur[0] = '\0'; }

    out->title      = e->title;
    out->artist     = e->artist;
    out->right      = dur;
    out->badge      = "";
    out->badgeColor = 0;
    return true;
}

static void song_selected(void *ctx, int index)
{
    const catalog_entry_t *e = Catalog_At(index);

    (void)ctx;
    LOG_INFO("songsel: selected #%d  %s - %s\r\n", index,
             (e != NULL) ? e->title : "?", (e != NULL) ? e->artist : "?");

    song_detail_show(index);
}

/* Build the song list into the LEFT panel. DejaVu Mono 12 — bold title over
 * regular artist/duration. */
static void song_list_init(void)
{
    leWidget *list = SongList_New();
    if (list == NULL) { return; }

    (void)Catalog_Reload();

    list->fn->setPosition(list, 0, SONGLIST_HEADER_H);
    list->fn->setSize(list, 320, 594 - SONGLIST_HEADER_H);
    /* Transparent: the dialog's gray panel shows through behind the rows. Match its
     * scheme so the glyph anti-alias blends against that real backdrop (0x18181B). */
    list->fn->setScheme(list, &SCHEME_PANEL_GRAY_18181B);
    SongList_SetTransparent(list, true);
    SongList_SetFonts(list,
                      (const leFont *)&DejaVuSansMonoBold_12,   /* title  — 12 bold    */
                      (const leFont *)&DejaVuSansMono_12,       /* artist/duration — 12 */
                      (const leFont *)&DejaVuSansMono_12);      /* badge (unused)      */
    SongList_SetRowHeight(list, SONGLIST_ROW_H);
    SongList_SetModel(list, Catalog_Count(), song_row, NULL);
    SongList_SetSelectHandler(list, song_selected, NULL);

    Marvin_PANEL_SONG_SELECT_LEFT->fn->addChild(Marvin_PANEL_SONG_SELECT_LEFT, list);

    SongList_SetSelected(list, 0);   /* default to the first song (no-op while empty) */
}

/* Dismiss the dialog. The X (close) and SELECT buttons both close it for now;
 * SELECT will start gameplay once that lands. ui_manager hides the OVR1 dialog and
 * OVR2 cover together and frees OVR1. */
static void song_close_on_release(leButtonWidget *btn)
{
    (void)btn;
    UiManager_CloseSongSelect();
}

void ScreenSongSelect_Setup(void)
{
    /* Center the dialog. The root is already on Legato layer 2 (built by MGS); the
     * canvas window positions that layer's pixels on the display. The surface is
     * painted once at boot (ui_manager paint_all_screens_once), so no invalidate
     * here — showing the dialog later is a pure layer bind. */
    gfxcSetWindowSize(CANVAS_SONGSEL, SONGSEL_W, SONGSEL_H);
    gfxcSetWindowPosition(CANVAS_SONGSEL, SONGSEL_X, SONGSEL_Y);

    radio_groups_init();
    round_button(Marvin_BUTTON_SONG_SELECT_SELECT, SELECT_RADIUS);   /* AA corners; not a radio */

    /* Close the dialog from the X (upper-right) or SELECT (placeholder until SELECT
     * starts gameplay). */
    Marvin_BUTTON_SONG_SELECT_CLOSE->fn->setReleasedEventCallback(Marvin_BUTTON_SONG_SELECT_CLOSE,
                                                                  song_close_on_release);
    Marvin_BUTTON_SONG_SELECT_SELECT->fn->setReleasedEventCallback(Marvin_BUTTON_SONG_SELECT_SELECT,
                                                                   song_close_on_release);

    /* Round the album-art strip: the empty overlay panel over the cover eats its
     * corners back to the dialog gray (0x18181B) it sits in front of. */
    Marvin_PANEL_ALBUM_ART_OVERLAY->fn->setScheme(Marvin_PANEL_ALBUM_ART_OVERLAY,
                                                  &SCHEME_PANEL_GRAY_18181B);
    Marvin_PANEL_ALBUM_ART_OVERLAY->fn->setCornerRadius(Marvin_PANEL_ALBUM_ART_OVERLAY,
                                                        ALBUM_ART_RADIUS);
    PanelAA_EnableRoundImage(Marvin_PANEL_ALBUM_ART_OVERLAY);

    song_detail_init();
    song_list_init();
    song_detail_show(0);   /* mirror the default (first-song) selection */
}
