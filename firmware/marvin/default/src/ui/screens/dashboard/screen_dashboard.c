#include "ui/screens/dashboard/screen_dashboard.h"

#include <stdio.h>

#include "ui/ui_manager.h"   /* CANVAS_DASH, BASE_W, BASE_H, UiManager_OpenSongSelect */
#include "ui/song_detail.h"
#include "ui/widgets/button_aa/widget_button_aa.h"
#include "ui/widgets/panel_aa/widget_panel_aa.h"

#include "game/catalog.h"
#include "game/art.h"
#include "game/selection.h"
#include "util/legato_utf8.h"

#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/string/legato_fixedstring.h"                /* runtime label text */
#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"                  /* DejaVu fonts */
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* dashboard widgets */

/* Dashboard surface, non-cached so the 2D engine and LCDC DMA read CPU-rendered
 * pixels coherently; 32-byte aligned. RGB565 — steady-state UI needs no more. */
#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

static uint16_t FB_NOCACHE s_fb[BASE_W * BASE_H];

void ScreenDashboard_InitSurface(void)
{
    gfxcSetPixelBuffer(CANVAS_DASH, BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565, s_fb);
}

/* The gameplay card's SELECT SONG button opens the song-select modal. */
static void select_song_on_release(leButtonWidget *btn)
{
    (void)btn;
    UiManager_OpenSongSelect();
}

/* Round the corners of a dashboard card and enable anti-aliased smoothing.
 * Set the radius before enabling. */
static void round_card(leWidget *panel, uint32_t radius)
{
    panel->fn->setCornerRadius(panel, radius);
    PanelAA_EnableRoundImage(panel);
}

/* Round the corners of a dashboard card and enable anti-aliased smoothing.
 * Set the radius before enabling. */
static void round_button(leButtonWidget *button, uint32_t radius)
{
    button->fn->setCornerRadius(button, radius);
    ButtonAA_Enable(button);
}

/* ── SONG card: mirror the committed gameplay selection ───────────────────────
 * The dashboard SONG card reflects the song + difficulty + mode the operator
 * confirmed in the song-select dialog (SELECT). Each dynamic label is pointed at a
 * per-label leFixedString (static buffer, no heap) rewritten on selection; the tier
 * text/color and duration reuse the shared ui/song_detail helpers. Same fixed-string
 * mechanism the song-select dialog uses. */
enum { DASH_TITLE, DASH_ARTIST, DASH_ALBUM, DASH_GENRE, DASH_DURATION, DASH_TIER,
       DASH_MODE, DASH_DIFF, DASH_COUNT };

#define DASH_CAP  80

static leChar        s_detail_buf[DASH_COUNT][DASH_CAP];
static leFixedString s_detail_str[DASH_COUNT];

static leLabelWidget *detail_label(int i)
{
    switch (i)
    {
        case DASH_TITLE:    return Marvin_LABEL_DASHBOARD_SONG_SongTitle;
        case DASH_ARTIST:   return Marvin_LABEL_DASHBOARD_SONG_SongArtist;
        case DASH_ALBUM:    return Marvin_LABEL_DASHBOARD_SONG_SongAlbum;
        case DASH_GENRE:    return Marvin_LABEL_DASHBOARD_SONG_SongGenre;
        case DASH_DURATION: return Marvin_LABEL_DASHBOARD_SONG_SongDuration;
        case DASH_TIER:     return Marvin_LABEL_DASHBOARD_SONG_SongTier;
        case DASH_MODE:     return Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode;
        default:            return Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty;
    }
}

/* Point each dynamic label at its fixed string, inheriting the MGS-assigned font
 * (tier forces DejaVuSansMonoBold_12, the only font carrying the U+2605 ★ glyph). */
static void song_detail_init(void)
{
    int i;
    for (i = 0; i < DASH_COUNT; i++)
    {
        leLabelWidget *lbl = detail_label(i);
        leString      *fs  = (leString *)&s_detail_str[i];
        leString      *cur = lbl->fn->getString(lbl);

        leFixedString_Constructor(&s_detail_str[i], s_detail_buf[i], DASH_CAP);
        if (i == DASH_TIER) { fs->fn->setFont(fs, (leFont *)&DejaVuSansMonoBold_12); }
        else if (cur != NULL) { fs->fn->setFont(fs, cur->fn->getFont(cur)); }
        lbl->fn->setString(lbl, fs);
    }
}

/* Set a detail value; "-" for an empty/unknown field. */
static void set_detail(int i, const char *s)
{
    leString *fs = (leString *)&s_detail_str[i];
    (void)lestring_set_utf8(fs, (s != NULL && s[0] != '\0') ? s : "-");
}

static const char *mode_text(uint8_t m)
{
    switch (m)
    {
        case SEL_MODE_1P_ROBOT: return "1P ROBOT";
        case SEL_MODE_1P_HUMAN: return "1P HUMAN";
        default:                return "2P R vs H";
    }
}

static const char *difficulty_text(uint8_t d)
{
    switch (d)
    {
        case SEL_DIFF_EASY:   return "EASY";
        case SEL_DIFF_MEDIUM: return "MEDIUM";
        case SEL_DIFF_HARD:   return "HARD";
        default:              return "EXPERT";
    }
}

/* Selected play difficulty → pill fill color (reuses the song-select button
 * schemes so the pill matches the dialog's selected difficulty). */
static const leScheme *difficulty_scheme(uint8_t d)
{
    switch (d)
    {
        case SEL_DIFF_EASY:   return &SCHEME_BUTTON_EASY;
        case SEL_DIFF_MEDIUM: return &SCHEME_BUTTON_MEDIUM;
        case SEL_DIFF_HARD:   return &SCHEME_BUTTON_HARD;
        default:              return &SCHEME_BUTTON_EXPERT;
    }
}

/* Set SONG_SongTier's text + color from the song's career tier (or "-" when the
 * song has no catalog entry). */
static void tier_show(const catalog_entry_t *e)
{
    leLabelWidget *lbl = detail_label(DASH_TIER);
    char           txt[40];

    if (e == NULL)
    {
        set_detail(DASH_TIER, "-");
        lbl->fn->setScheme(lbl, SongDetail_TierScheme(0));
        return;
    }
    int tier = SongDetail_Tier(e->difficulty);
    SongDetail_TierText(tier, txt, sizeof txt);
    set_detail(DASH_TIER, txt);
    lbl->fn->setScheme(lbl, SongDetail_TierScheme(tier));
}

/* Mirror the committed selection onto the SONG card: artwork + metadata from the
 * catalog, tier from the song, mode + difficulty from the selection. */
static void dash_selection_changed(const selection_t *sel)
{
    catalog_entry_t e;
    bool            ok = Catalog_Lookup(sel->setlist, sel->index, &e);
    char            tmp[96];

    Marvin_PANEL_DASHBOARD_SONG_AlbumArt->fn->setImage(
        Marvin_PANEL_DASHBOARD_SONG_AlbumArt,
        (leImage *)Art_Small(sel->setlist, sel->index));

    if (ok)
    {
        set_detail(DASH_TITLE,  e.title);
        set_detail(DASH_ARTIST, e.artist);
        set_detail(DASH_GENRE,  e.genre);

        /* Album + release year on one line (the card has no separate year label). */
        if (e.album[0] != '\0' && e.year != 0u)
        {
            (void)snprintf(tmp, sizeof tmp, "%s - %u", e.album, (unsigned)e.year);
        }
        else if (e.album[0] != '\0') { (void)snprintf(tmp, sizeof tmp, "%s", e.album); }
        else if (e.year != 0u)       { (void)snprintf(tmp, sizeof tmp, "%u", (unsigned)e.year); }
        else                         { tmp[0] = '\0'; }
        set_detail(DASH_ALBUM, tmp);

        SongDetail_Duration(e.length_s, tmp, sizeof tmp);
        set_detail(DASH_DURATION, tmp);

        tier_show(&e);
    }
    else
    {
        set_detail(DASH_TITLE,    "-");
        set_detail(DASH_ARTIST,   "-");
        set_detail(DASH_ALBUM,    "-");
        set_detail(DASH_GENRE,    "-");
        set_detail(DASH_DURATION, "-");
        tier_show(NULL);
    }

    set_detail(DASH_MODE, mode_text(sel->mode));

    set_detail(DASH_DIFF, difficulty_text(sel->difficulty));
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_Difficulty->fn->setScheme(
        Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_Difficulty, difficulty_scheme(sel->difficulty));
}

void ScreenDashboard_Setup(void)
{
    /* Full-screen at the origin. The root is on Legato layer 0 (built by MGS); the
     * canvas window positions that layer's pixels on the display. */
    gfxcSetWindowPosition(CANVAS_DASH, 0, 0);
    gfxcSetWindowSize(CANVAS_DASH, BASE_W, BASE_H);

    round_card(Marvin_PANEL_DASHBOARD_ROBOT,                4u);
    round_card(Marvin_PANEL_DASHBOARD_TEST_PATTERN_BORDER,  6u);
    round_card(Marvin_PANEL_DASHBOARD_SONG,                 4u);
    round_card(Marvin_PANEL_DASHBOARD_HUMAN,                4u);
    round_card(Marvin_PANEL_DASHBOARD_NO_SIGNAL,            4u);
    round_card(Marvin_PANEL_DASHBOARD_NO_SIGNAL_LED,        4u);

    round_card(Marvin_PANEL_DASHBOARD_SONG_ALBUM_ART_BORDER,   10u);

    round_button(Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG, 4);
    round_button(Marvin_BUTTON_DASHBOARD_GAMEPLAY_START,       4);

    round_button(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN,     4);
    round_button(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED,       4);
    round_button(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW,    4);
    round_button(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE,      4);
    round_button(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE,    4);

    Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG->fn->setReleasedEventCallback(
        Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG, select_song_on_release);

    /* Mirror the committed gameplay selection onto the SONG card. Register the
     * observer before song-select's Setup seeds the boot default (ui_manager calls
     * this screen's Setup first), so that first commit lands here. */
    song_detail_init();
    Selection_SetObserver(dash_selection_changed);
    if (Selection_Get()->valid) { dash_selection_changed(Selection_Get()); }
}
