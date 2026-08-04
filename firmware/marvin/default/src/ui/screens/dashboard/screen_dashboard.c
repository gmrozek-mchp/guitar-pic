#include "ui/screens/dashboard/screen_dashboard.h"

#include <stdio.h>

#include "ui/ui_manager.h"   /* CANVAS_DASH, BASE_W, BASE_H, UiManager_OpenSongSelect */
#include "ui/song_detail.h"
#include "ui/dashboard_feed.h"   /* DashboardFeed_PostSelection — route updates via the feed */
#include "ui/widgets/button_aa/widget_button_aa.h"
#include "ui/widgets/panel_aa/widget_panel_aa.h"
#include "ui/widgets/progressbar_aa/widget_progressbar_aa.h"

#include "actuator/guitar_cmd.h"   /* GUITAR_BTN_* fret mask layout */

#include "game/game_catalog.h"
#include "game/game_art.h"
#include "game/game_selection.h"
#include "game/game_controller.h"
#include "results/results.h"   /* Results_Set/GetPlayer — 2P player-name prompt */
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

/* Commit callback from the on-screen keyboard: record the entered name as the
 * current player, then start the run. Empty input leaves the player unchanged. */
static void player_name_committed(const char *name)
{
    if (name != NULL && name[0] != '\0') { Results_SetPlayer(name); }
    GameController_Start();
}

/* The gameplay card's START button kicks off the game-state controller: navigate
 * GH3 to the committed selection and hand off to the CV detector. A 2-player match
 * has a human competitor, so first prompt for their name via the on-screen keyboard
 * and start on OK; 1P-robot starts immediately. */
static void start_on_release(leButtonWidget *btn)
{
    const game_selection_t *sel = GameSelection_Get();

    (void)btn;

    if (sel->valid && sel->mode == GAME_MODE_2P)
    {
        UiManager_OpenKeyboard("ENTER PLAYER NAME", Results_GetPlayer(), 32,
                               player_name_committed);
        return;
    }
    GameController_Start();
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

/* Runtime text for the SONG card's Status label, driven by the game controller
 * (READY / NAVIGATING / PLAYING / FAILED / …). Same fixed-string mechanism. */
static leChar        s_status_buf[24];
static leFixedString s_status_str;

/* Runtime text for the playtime bar's markers: left counts up with elapsed play
 * time, right holds the selected song's total duration (both m:ss). */
static leChar        s_start_buf[8];
static leFixedString s_start_str;
static leChar        s_stop_buf[8];
static leFixedString s_stop_str;

/* Runtime text for the ROBOT card's score label — the CV-read GH3 score, updated
 * live during a run by the game controller via the dashboard feed. */
static leChar        s_score_buf[12];
static leFixedString s_score_str;

/* Runtime text for the ROBOT card's note-streak label — the CV-read GH3 streak
 * (blank until the odometer appears at ~25), likewise fed live during a run. */
static leChar        s_streak_buf[8];
static leFixedString s_streak_str;

/* Selected song length (s), captured on ApplySelection so ApplyPlaytime can scale
 * elapsed play time into the 0-100 progress-bar fill. 0 = unknown → no fill. */
static uint16_t s_song_len_s;

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

    /* Status label: point it at its own fixed string, inheriting its MGS font. */
    leLabelWidget *st  = Marvin_LABEL_DASHBOARD_SONG_Status;
    leString      *sfs = (leString *)&s_status_str;
    leString      *scur = st->fn->getString(st);
    leFixedString_Constructor(&s_status_str, s_status_buf,
                              sizeof(s_status_buf) / sizeof(s_status_buf[0]));
    if (scur != NULL) { sfs->fn->setFont(sfs, scur->fn->getFont(scur)); }
    st->fn->setString(st, sfs);

    /* Playtime markers: point each at its own fixed string, inheriting its MGS font,
     * so the elapsed/duration text can be rewritten during play and on selection. */
    leLabelWidget *start = Marvin_LABEL_DASHBOARD_SONG_START_TIME;
    leString      *efs   = (leString *)&s_start_str;
    leString      *ecur  = start->fn->getString(start);
    leFixedString_Constructor(&s_start_str, s_start_buf,
                              sizeof(s_start_buf) / sizeof(s_start_buf[0]));
    if (ecur != NULL) { efs->fn->setFont(efs, ecur->fn->getFont(ecur)); }
    start->fn->setString(start, efs);

    leLabelWidget *stop  = Marvin_LABEL_DASHBOARD_SONG_STOP_TIME;
    leString      *tfs   = (leString *)&s_stop_str;
    leString      *tcur  = stop->fn->getString(stop);
    leFixedString_Constructor(&s_stop_str, s_stop_buf,
                              sizeof(s_stop_buf) / sizeof(s_stop_buf[0]));
    if (tcur != NULL) { tfs->fn->setFont(tfs, tcur->fn->getFont(tcur)); }
    stop->fn->setString(stop, tfs);

    /* ROBOT score label: point it at its own fixed string, inheriting its MGS font. */
    leLabelWidget *score = Marvin_LABEL_DASHBOARD_ROBOT_Score;
    leString      *pfs   = (leString *)&s_score_str;
    leString      *pcur  = score->fn->getString(score);
    leFixedString_Constructor(&s_score_str, s_score_buf,
                              sizeof(s_score_buf) / sizeof(s_score_buf[0]));
    if (pcur != NULL) { pfs->fn->setFont(pfs, pcur->fn->getFont(pcur)); }
    score->fn->setString(score, pfs);

    /* ROBOT note-streak label: same fixed-string handoff, inheriting its MGS font. */
    leLabelWidget *streak = Marvin_LABEL_DASHBOARD_ROBOT_Streak;
    leString      *kfs    = (leString *)&s_streak_str;
    leString      *kcur   = streak->fn->getString(streak);
    leFixedString_Constructor(&s_streak_str, s_streak_buf,
                              sizeof(s_streak_buf) / sizeof(s_streak_buf[0]));
    if (kcur != NULL) { kfs->fn->setFont(kfs, kcur->fn->getFont(kcur)); }
    streak->fn->setString(streak, kfs);
}

/* Game-controller status observer — runs in the game-controller task's context.
 * Never touches widgets; just enqueues so the feed consumer applies it. */
static void dash_game_status(const char *text)
{
    DashboardFeed_PostStatus(text);
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
        case GAME_MODE_1P_ROBOT: return "1P ROBOT";
        case GAME_MODE_1P_HUMAN: return "1P HUMAN";
        default:                return "2P R vs H";
    }
}

static const char *difficulty_text(uint8_t d)
{
    switch (d)
    {
        case GAME_DIFF_EASY:   return "EASY";
        case GAME_DIFF_MEDIUM: return "MEDIUM";
        case GAME_DIFF_HARD:   return "HARD";
        default:              return "EXPERT";
    }
}

/* Selected play difficulty → pill fill color (reuses the song-select button
 * schemes so the pill matches the dialog's selected difficulty). */
static const leScheme *difficulty_scheme(uint8_t d)
{
    switch (d)
    {
        case GAME_DIFF_EASY:   return &SCHEME_BUTTON_EASY;
        case GAME_DIFF_MEDIUM: return &SCHEME_BUTTON_MEDIUM;
        case GAME_DIFF_HARD:   return &SCHEME_BUTTON_HARD;
        default:              return &SCHEME_BUTTON_EXPERT;
    }
}

/* Set SONG_SongTier's text + color from the song's career tier (or "-" when the
 * song has no catalog entry). */
static void tier_show(const game_catalog_entry_t *e)
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
 * catalog, tier from the song, mode + difficulty from the selection. Sole caller is
 * the dashboard feed consumer (plus a synchronous seed from Setup, pre-reveal). */
void ScreenDashboard_ApplySelection(void)
{
    const game_selection_t *sel = GameSelection_Get();
    game_catalog_entry_t e;
    bool            ok = GameCatalog_Lookup(sel->setlist, sel->index, &e);
    char            tmp[96];

    Marvin_PANEL_DASHBOARD_SONG_AlbumArt->fn->setImage(
        Marvin_PANEL_DASHBOARD_SONG_AlbumArt,
        (leImage *)GameArt_Small(sel->setlist, sel->index));

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
        (void)lestring_set_utf8((leString *)&s_stop_str, tmp);
        s_song_len_s = e.length_s;

        tier_show(&e);
    }
    else
    {
        set_detail(DASH_TITLE,    "-");
        set_detail(DASH_ARTIST,   "-");
        set_detail(DASH_ALBUM,    "-");
        set_detail(DASH_GENRE,    "-");
        set_detail(DASH_DURATION, "-");
        (void)lestring_set_utf8((leString *)&s_stop_str, "-");
        s_song_len_s = 0u;
        tier_show(NULL);
    }

    /* A fresh selection resets the playtime bar and its elapsed marker to 0. */
    (void)lestring_set_utf8((leString *)&s_start_str, "0:00");
    ProgressBarAA_SetPermille(Marvin_PROGRESSBAR_DASHBOARD_SONG_PLAYTIME, 0u);

    set_detail(DASH_MODE, mode_text(sel->mode));

    set_detail(DASH_DIFF, difficulty_text(sel->difficulty));
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_Difficulty->fn->setScheme(
        Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_Difficulty, difficulty_scheme(sel->difficulty));
}

/* Selection observer — runs in the committing task's context (touch / song-select).
 * Never touches widgets; just enqueues so the feed consumer applies it as the single
 * dashboard writer. */
static void dash_selection_changed(const game_selection_t *sel)
{
    (void)sel;
    DashboardFeed_PostSelection();
}

/* Reflect the live guitar mask on the ROBOT fret buttons: pressed = fret held. Only
 * the buttons whose state changed are touched, so a steady chord costs nothing. The
 * buttons are toggleable so setPressed latches the state (a non-toggleable button
 * treats setPressed(TRUE) as a click and stays UP), and we invalidate explicitly —
 * setPressed only self-invalidates for image/bevel/offset buttons, not for a plain
 * scheme-filled one, so the UP↔TOGGLED scheme-colour swap wouldn't otherwise repaint. */
void ScreenDashboard_ApplyFret(uint8_t mask)
{
    static const struct { leButtonWidget **btn; uint8_t bit; } frets[] = {
        { &Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN,  GUITAR_BTN_GREEN  },
        { &Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED,    GUITAR_BTN_RED    },
        { &Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW, GUITAR_BTN_YELLOW },
        { &Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE,   GUITAR_BTN_BLUE   },
        { &Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE, GUITAR_BTN_ORANGE },
    };
    static uint8_t s_last_mask;

    uint8_t changed = (uint8_t)(mask ^ s_last_mask);
    if (changed == 0u) { return; }
    s_last_mask = mask;

    for (unsigned i = 0; i < (sizeof frets / sizeof frets[0]); i++)
    {
        if (changed & frets[i].bit)
        {
            leButtonWidget *b = *frets[i].btn;
            b->fn->setPressed(b, (mask & frets[i].bit) ? LE_TRUE : LE_FALSE);
            b->fn->invalidate(b);
        }
    }
}

/* CV-read score multiplier (1..4) → highlight the active ROBOT {1,2,3,4}X button,
 * clearing the other three (same toggle/latch mechanism as ApplyFret). */
void ScreenDashboard_ApplyMultiplier(uint8_t mult)
{
    if (mult < 1u || mult > 4u) { return; }
    static uint8_t s_last_mult;
    if (mult == s_last_mult) { return; }
    s_last_mult = mult;

    leButtonWidget *btns[4] = {
        Marvin_BUTTON_DASHBOARD_ROBOT_1X, Marvin_BUTTON_DASHBOARD_ROBOT_2X,
        Marvin_BUTTON_DASHBOARD_ROBOT_3X, Marvin_BUTTON_DASHBOARD_ROBOT_4X,
    };
    for (uint8_t i = 0; i < 4u; i++)
    {
        leButtonWidget *b = btns[i];
        b->fn->setPressed(b, (i == (uint8_t)(mult - 1u)) ? LE_TRUE : LE_FALSE);
        b->fn->invalidate(b);
    }
}

/* Game-controller status → SONG card Status label. */
void ScreenDashboard_ApplyStatus(const char *text)
{
    (void)lestring_set_utf8((leString *)&s_status_str,
                            (text != NULL && text[0] != '\0') ? text : "READY");
}

/* CV-read GH3 score → the ROBOT card's score label. */
void ScreenDashboard_ApplyScore(uint32_t score)
{
    char tmp[12];
    (void)snprintf(tmp, sizeof tmp, "%lu", (unsigned long)score);
    (void)lestring_set_utf8((leString *)&s_score_str, tmp);
}

/* CV-read GH3 note streak → the ROBOT card's streak label. 0 means the odometer
 * isn't shown yet (streak < ~25) or the run reset; shown as "0" (not blank) so the
 * label repaints — setting an empty string does not clear the prior glyphs, which
 * left a stale count on song start / after a miss. */
void ScreenDashboard_ApplyStreak(uint16_t streak)
{
    char tmp[8];
    (void)snprintf(tmp, sizeof tmp, "%u", (unsigned)streak);
    (void)lestring_set_utf8((leString *)&s_streak_str, tmp);
}

/* Elapsed play time → the left marker (m:ss, counting up) and the bar fill, both
 * scaled/clamped against the selected song's duration (captured in ApplySelection).
 * The bar is driven at pixel resolution (permille, 0-1000) rather than the stock 0-100
 * value so it advances by single pixels, not ~3.5px steps. Unknown duration leaves the
 * bar empty and the marker un-clamped. */
void ScreenDashboard_ApplyPlaytime(uint32_t elapsed_ms)
{
    uint32_t total_ms = (uint32_t)s_song_len_s * 1000u;
    uint32_t permille = 0u;
    uint32_t shown_ms = elapsed_ms;
    if (total_ms != 0u)
    {
        if (shown_ms > total_ms) { shown_ms = total_ms; }
        permille = (shown_ms * 1000u) / total_ms;
    }

    char elapsed[8];
    unsigned s = (unsigned)(shown_ms / 1000u);
    (void)snprintf(elapsed, sizeof elapsed, "%u:%02u", s / 60u, s % 60u);
    (void)lestring_set_utf8((leString *)&s_start_str, elapsed);

    ProgressBarAA_SetPermille(Marvin_PROGRESSBAR_DASHBOARD_SONG_PLAYTIME, permille);
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

    /* Playtime bar as an 8px-high rounded capsule. The radius (half the height →
     * full pill ends) is carried by the AA pass, not the widget cornerRadius — a
     * nonzero widget radius makes the stock skin draw its own rounded track that
     * overshoots the 8px rect. Corners are eaten back to the card colour (18181B). */
    leProgressBarWidget *bar = Marvin_PROGRESSBAR_DASHBOARD_SONG_PLAYTIME;
    bar->fn->setSize(bar, bar->fn->getWidth(bar), 8u);
    ProgressBarAA_EnableRoundImage(bar, 4u, &SCHEME_FILL_ZINC_900);

    round_button(Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG, 4);
    round_button(Marvin_BUTTON_DASHBOARD_GAMEPLAY_START,       4);

    round_button(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN,     4);
    round_button(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED,       4);
    round_button(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW,    4);
    round_button(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE,      4);
    round_button(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE,    4);

    /* The fret buttons are a status display, not an input: toggleable so their
     * pressed state holds under program control (ScreenDashboard_ApplyFret), and
     * IGNOREEVENTS so operator taps don't fight the live mask. */
    leButtonWidget *frets[] = {
        Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN,  Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED,
        Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW, Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE,
        Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE,
    };
    for (unsigned i = 0; i < (sizeof frets / sizeof frets[0]); i++)
    {
        frets[i]->fn->setToggleable(frets[i], LE_TRUE);
        frets[i]->widget.flags |= LE_WIDGET_IGNOREEVENTS;
    }

    Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG->fn->setReleasedEventCallback(
        Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG, select_song_on_release);
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_START->fn->setReleasedEventCallback(
        Marvin_BUTTON_DASHBOARD_GAMEPLAY_START, start_on_release);

    /* Mirror the committed gameplay selection onto the SONG card. Register the
     * observer before song-select's Setup seeds the boot default (ui_manager calls
     * this screen's Setup first), so that first commit lands here. */
    song_detail_init();
    GameSelection_SetObserver(dash_selection_changed);
    if (GameSelection_Get()->valid) { ScreenDashboard_ApplySelection(); }

    GameController_SetStatusObserver(dash_game_status);
}
