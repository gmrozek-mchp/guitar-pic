#include "ui/screens/song_select/screen_song_select.h"

#include <stdio.h>

#include "FreeRTOS.h"        /* configASSERT */

#include "ui/ui_manager.h"   /* CANVAS_SONGSEL, BASE_W, BASE_H */
#include "ui/widgets/button_aa/widget_button_aa.h"
#include "ui/widgets/panel_aa/widget_panel_aa.h"
#include "ui/widgets/song_list/widget_song_list.h"

#include "game/game_catalog.h"
#include "game/game_art.h"
#include "game/game_selection.h"
#include "ui/song_detail.h"
#include "log.h"
#include "util/legato_utf8.h"

#include "ui/gfx/aa_corners.h"
#include "ui/gfx/ui_surface.h"
#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/core/legato_scheme.h"
#include "gfx/legato/string/legato_fixedstring.h"                /* runtime label text */
#include "gfx/legato/string/legato_tablestring.h"                /* design captions */
#include "gfx/legato/widget/label/legato_widget_label.h"
#include "gfx/legato/widget/button/legato_widget_button.h"
#include "gfx/legato/widget/image/legato_widget_image.h"
#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"                  /* stringID_*, fonts, icons */
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* the two empty root panels */

/* Song/mode-select dialog, built programmatically into two empty MGS root panels —
 * the screen_bus.c / screen_navigation.c model. Ports the mockup's SongSelectModal.tsx
 * with Tailwind v4 units resolved to pixels: a header, a scrolling setlist, a preview
 * column, and a difficulty/mode/START column.
 *
 * TWO layer-screens, and that split is the point: the dialog chrome is layer 2
 * (CANVAS_SONGSEL, 1100x660 RGB565 on OVR1) while the album-art strip is layer 3
 * (CANVAS_ALBUM_ART, 508x208 RGBA8888 on OVR2, positioned over the dialog's art rect
 * by screen_album_art.c). Everything that changes when the selection changes — the
 * cover, the tier line, the title, the artist — lives on that small second surface, so
 * switching songs repaints 0.42 MB instead of the whole dialog. This module owns the
 * widgets on both; screen_album_art.c owns layer 3's surface and window.
 *
 * Captions come from the DESIGN string table (leTableString + stringID_*), not C
 * literals, so they stay translatable and MGS keeps auto-including their glyphs. Text
 * that is song DATA is a leFixedString written at runtime. */

#define SONGSEL_W   1100u
#define SONGSEL_H    660u
#define SONGSEL_X   ((int)((BASE_W - SONGSEL_W) / 2u))   /* 90  */
#define SONGSEL_Y   ((int)((BASE_H - SONGSEL_H) / 2u))   /* 70  */

/* Legato quirk: the image widget's public leImageWidget_Constructor is declared but
 * never defined; only the internal in-place one is linkable (same workaround as
 * ui/titlebar.c and screen_dashboard.c). */
extern void _leImageWidget_Constructor(leImageWidget *img);

#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

static uint16_t FB_NOCACHE s_fb_songsel[SONGSEL_W * SONGSEL_H];

void ScreenSongSelect_InitSurface(void)
{
    UiSurface_Set(CANVAS_SONGSEL, SONGSEL_W, SONGSEL_H, GFX_COLOR_MODE_RGB_565, s_fb_songsel);
}

/* Radio-group sizes. Declared before the layout because the MODE heading's position is
 * derived from how many difficulty buttons precede it. */
#define DIFFICULTY_COUNT    4u
#define MODE_COUNT          2u   /* 1P robot, 2P robot-vs-human (1P-human dropped) */

/* ── layout ─────────────────────────────────────────────────────────────────
 * Mockup Tailwind resolved to pixels. Spacing: p-6 = 24, p-4 = 16, gap-3 = 12,
 * gap-2 = 8. Line heights: text-xs = 16, text-sm = 20, text-lg = 28, text-2xl = 30.
 * Every position is derived, so the columns stay consistent if one width changes. */
#define PAD_6   24
#define PAD_4   16
#define PAD_3   12
#define PAD_2    8

#define TXT_XS  16
#define TXT_SM  20

#define DLG_R   12               /* rounded-xl */

/* The dialog's own 1px border, and the content box INSIDE it. Everything is laid out
 * relative to that box, never to the surface: the mockup's box is `border` +
 * `overflow-hidden`, so its content starts after the border and no child of it may
 * paint on the border row. Getting this wrong is visible — the list's row separators
 * span the list's full width, so a list at x=0 redraws the left border every 57px in
 * the separator's colour. It also cost a pixel of accuracy everywhere: with the inset,
 * the derivation lands exactly on the figma export's own values (art at 434,160;
 * metrics at 314; SELECT at 587). */
#define DLG_BORDER 1
#define CONTENT_X  DLG_BORDER
#define CONTENT_Y  DLG_BORDER
#define CONTENT_W  ((int)SONGSEL_W - 2 * DLG_BORDER)
#define CONTENT_H  ((int)SONGSEL_H - 2 * DLG_BORDER)

/* Header: the 32px close button (p-1.5 around a 20px icon) is its tallest child, so
 * it sets the row height. Its border-b is the row after it. */
#define CLOSE_D    32
#define HDR_H      (PAD_4 + CLOSE_D + PAD_4)
#define HDR_RULE_Y (CONTENT_Y + HDR_H)
#define BODY_Y     (HDR_RULE_Y + 1)
#define BODY_H     (CONTENT_Y + CONTENT_H - BODY_Y)

#define LEFT_W     320                                   /* w-80, incl. its border-r */
#define RIGHT_W    224                                   /* w-56 */
#define LEFT_RULE_X  (CONTENT_X + LEFT_W - 1)
#define CENTER_X     (LEFT_RULE_X + 1)
#define RIGHT_X      (CONTENT_X + CONTENT_W - RIGHT_W)
#define CENTER_W     (RIGHT_X - CENTER_X)
#define CENTER_RULE_X (RIGHT_X - 1)
#define CENTER_CONTENT_W (CENTER_W - 1)                  /* less the column's border-r */

/* Left column: a SETLIST header strip (its last row the border-b) over the list. */
#define LEFT_CONTENT_W  (LEFT_W - 1)                     /* less the column's border-r */
#define SETLIST_H  (PAD_2 + TXT_XS + PAD_2 + 1)
#define LIST_Y     (BODY_Y + SETLIST_H)
/* Stops DLG_R short of the bottom edge: the list's row separators and selected-row
 * fill span its full width, so a list running to y = SONGSEL_H would paint into the
 * dialog's rounded bottom-left corner and undo ScreenSongSelect_RoundCorners on every
 * scroll. Ending at the start of the arc is also what the mockup's rounded
 * overflow-hidden container does to its last row. */
#define LIST_H     (BODY_H - SETLIST_H - DLG_R)
/* px-4 py-3 + two text-xs lines + the border-b, which the widget draws as the row's
 * last pixel row: 12 + 32 + 12 + 1. */
#define LIST_ROW_H 57

/* Center column: the album-art rect, then the 2x2 metric grid. ART_W/ART_H are an
 * ASSET property — the covers are decoded into 508x208 slots (game_art.c) — while the
 * mockup's art is its column's content width, 506. So the art is CENTRED in the column
 * rather than placed at p-6: 23px of side padding instead of 24, which absorbs those
 * 2px symmetrically and is where the mockup's own numbers land anyway. */
#define ART_W      508
#define ART_H      208
#define ART_X      (CENTER_X + (CENTER_CONTENT_W - ART_W) / 2)
#define ART_Y      (BODY_Y + PAD_6)
#define ART_R      DLG_R                                 /* rounded-xl, as the dialog */

#define INFO_Y      (ART_Y + ART_H + PAD_4)
#define INFO_COL_W  ((ART_W - PAD_3) / 2)
#define INFO_PITCH  (TXT_XS + TXT_SM + PAD_3)            /* label + value + gap-3 */

/* Right column. */
#define R_X          (RIGHT_X + PAD_4)
#define R_W          (RIGHT_W - 2 * PAD_4)
#define DIFF_LBL_Y   (BODY_Y + PAD_4)
#define DIFF_Y0      (DIFF_LBL_Y + TXT_XS + PAD_2)       /* mt-2 */
/* Deliberately TALLER than the mockup: it specifies py-2.5 / py-3 (42 px both), and
 * these are py-4 — a bigger touch target on a real panel, the same call as the
 * dashboard's 60px gameplay buttons. Everything below is derived from that padding, so
 * PAD_BTN_Y is the one number to change.
 *
 * Heights include the 1px border on each edge: these are auto-height elements, so
 * content + padding + border is the rendered height regardless of box-sizing. */
#define PAD_BTN_Y    16                                  /* py-4 */
#define BTN_BORDER   2                                   /* 1px top + 1px bottom */

#define DIFF_H       (2 * PAD_BTN_Y + TXT_SM + BTN_BORDER)   /* text-sm caption */
#define DIFF_PITCH   (DIFF_H + PAD_2)
#define MODE_LBL_Y   (DIFF_Y0 + DIFFICULTY_COUNT * DIFF_PITCH - PAD_2 + PAD_3 + PAD_2)
#define MODE_Y0      (MODE_LBL_Y + TXT_XS + PAD_2)
#define MODE_H       (2 * PAD_BTN_Y + TXT_XS + BTN_BORDER)   /* text-xs caption */
#define MODE_PITCH   (MODE_H + PAD_2)
#define SEL_H        56                                  /* py-4 + text-base, no border */
#define SEL_Y        (CONTENT_Y + CONTENT_H - PAD_4 - SEL_H)   /* mt-auto */

#define BTN_R         4                                  /* rounded */
#define SEL_R         8                                  /* rounded-lg */

/* ── difficulty + mode radio groups ──────────────────────────────────────────
 * The four DIFFICULTY buttons are one single-select group, the three MODE buttons
 * another. They are plain push buttons sharing one unselected scheme
 * (SCHEME_BUTTON_DIFFICULTY / SCHEME_BUTTON_MODE); the radio behaviour is driven here
 * by swapping schemes on release. Difficulty's selected look is per-button (Easy→green
 * … Expert→red); mode uses one shared selected scheme. The selection persists in
 * s_difficulty / s_mode — the source of truth the repaint reads. */
#define DIFFICULTY_DEFAULT  0u   /* Easy      */
#define MODE_DEFAULT        0u   /* 1P robot  */

static unsigned int s_difficulty = DIFFICULTY_DEFAULT;
static unsigned int s_mode       = MODE_DEFAULT;
static int          s_sel_index  = 0;   /* catalog position of the highlighted song */

/* ── widget pools ───────────────────────────────────────────────────────────
 * Static storage, constructed in place — no allocator (see the project's static
 * allocation rule). configASSERT catches undersizing at bring-up. The song list is
 * the one exception: SongList_New() allocates from the Legato widget pool. */
#define WGT_MAX   6u    /* 4 hairlines + the album-art corner overlay */
#define CAP_MAX   8u    /* design-string captions */
#define BTN_MAX   8u    /* close + 4 difficulty + 2 mode + SELECT */

static leWidget       s_wgt[WGT_MAX];
static unsigned       s_nwgt;
static leLabelWidget  s_cap[CAP_MAX];
static leTableString  s_cap_str[CAP_MAX];
static unsigned       s_ncap;
static leButtonWidget s_btn[BTN_MAX];
static leTableString  s_btn_str[BTN_MAX];
static unsigned       s_nbtn;

/* Runtime-written text: the setlist count, the metric values, and the three lines over
 * the cover. All song data, none of it translatable. */
enum {
    DYN_SETLIST,
    DYN_ALBUM, DYN_YEAR, DYN_GENRE, DYN_DURATION,
    DYN_TIER, DYN_TITLE, DYN_ARTIST,
    DYN_COUNT
};

#define DYN_CAP  80

static leChar        s_dyn_buf[DYN_COUNT][DYN_CAP];
static leFixedString s_dyn_str[DYN_COUNT];
static leLabelWidget s_dyn_lbl[DYN_COUNT];

/* Handles the selection feed needs after the build. */
static leButtonWidget *s_difficulty_btn[DIFFICULTY_COUNT];
static leButtonWidget *s_mode_btn[MODE_COUNT];
static leImageWidget   s_art;
static leWidget       *s_list;

/* ── build helpers ──────────────────────────────────────────────────────────*/

static leWidget *add_panel(leWidget *parent, int x, int y, int w, int h,
                           const leScheme *scheme)
{
    configASSERT(s_nwgt < WGT_MAX);

    leWidget *p = &s_wgt[s_nwgt++];
    leWidget_Constructor(p);
    p->fn->setPosition(p, x, y);
    p->fn->setSize(p, w, h);
    p->fn->setScheme(p, scheme);
    p->fn->setBackgroundType(p, LE_WIDGET_BACKGROUND_FILL);
    p->fn->setBorderType(p, LE_WIDGET_BORDER_NONE);
    parent->fn->addChild(parent, p);
    return p;
}

/* A 1px hairline. The mockup's dialog rules are border-zinc-700; the one under the
 * SETLIST header is border-zinc-800. */
static void add_rule(int x, int y, int w, int h, const leScheme *scheme)
{
    (void)add_panel(Marvin_PANEL_SONG_SELECT, x, y, w, h, scheme);
}

/* A design-string caption. The string carries its own font via the design's binding,
 * so static text needs no font argument here. */
static leLabelWidget *add_cap(leWidget *parent, int x, int y, int w, int h,
                              uint32_t string_id, const leScheme *scheme)
{
    configASSERT(s_ncap < CAP_MAX);

    leLabelWidget *l = &s_cap[s_ncap];
    leLabelWidget_Constructor(l);
    l->fn->setPosition(l, x, y);
    l->fn->setSize(l, w, h);
    l->fn->setScheme(l, scheme);
    l->fn->setBackgroundType(l, LE_WIDGET_BACKGROUND_NONE);
    l->fn->setHAlignment(l, LE_HALIGN_LEFT);
    l->fn->setVAlignment(l, LE_VALIGN_MIDDLE);

    leTableString_Constructor(&s_cap_str[s_ncap], string_id);
    l->fn->setString(l, (leString *)&s_cap_str[s_ncap]);
    s_ncap++;

    parent->fn->addChild(parent, (leWidget *)l);
    return l;
}

/* A runtime-written label, addressed by its DYN_* id. The font is explicit — there is
 * no design string to inherit one from. */
static leLabelWidget *add_dyn(leWidget *parent, unsigned id, int x, int y, int w, int h,
                              const leFont *font, const leScheme *scheme)
{
    configASSERT(id < DYN_COUNT);

    leLabelWidget *l = &s_dyn_lbl[id];
    leLabelWidget_Constructor(l);
    l->fn->setPosition(l, x, y);
    l->fn->setSize(l, w, h);
    l->fn->setScheme(l, scheme);
    l->fn->setBackgroundType(l, LE_WIDGET_BACKGROUND_NONE);
    l->fn->setHAlignment(l, LE_HALIGN_LEFT);
    l->fn->setVAlignment(l, LE_VALIGN_MIDDLE);

    leFixedString_Constructor(&s_dyn_str[id], s_dyn_buf[id], DYN_CAP);
    leString *fs = (leString *)&s_dyn_str[id];
    fs->fn->setFont(fs, (leFont *)font);
    l->fn->setString(l, fs);

    parent->fn->addChild(parent, (leWidget *)l);
    return l;
}

/* Set a runtime label; "-" for an empty/unknown value. The label repaints via the
 * string's invalidate callback (installed by setString). */
static void set_dyn(unsigned id, const char *text)
{
    (void)lestring_set_utf8((leString *)&s_dyn_str[id],
                            (text != NULL && text[0] != '\0') ? text : "-");
}

/* A button with a design-string caption, rounded + anti-aliased. `radius` first:
 * ButtonAA smooths whatever cornerRadius the button has. A LINE border becomes the
 * mockup's 1px border — the classic skin draws it in the scheme's SHADOWDARK, which
 * every button scheme here carries as its Tailwind border colour. */
static leButtonWidget *add_button(leWidget *parent, int x, int y, int w, int h,
                                  uint32_t string_id, const leScheme *scheme,
                                  uint32_t radius, leBool bordered)
{
    configASSERT(s_nbtn < BTN_MAX);
    unsigned i = s_nbtn++;

    leButtonWidget *b = &s_btn[i];
    leButtonWidget_Constructor(b);
    b->fn->setPosition(b, x, y);
    b->fn->setSize(b, w, h);
    b->fn->setScheme(b, scheme);
    b->fn->setBackgroundType(b, LE_WIDGET_BACKGROUND_FILL);
    b->fn->setBorderType(b, bordered ? LE_WIDGET_BORDER_LINE : LE_WIDGET_BORDER_NONE);
    b->fn->setCornerRadius(b, radius);
    b->fn->setPressedOffset(b, 0);

    if (string_id != 0u)
    {
        leTableString_Constructor(&s_btn_str[i], string_id);
        b->fn->setString(b, (leString *)&s_btn_str[i]);
    }

    ButtonAA_Enable(b);
    parent->fn->addChild(parent, (leWidget *)b);
    return b;
}

/* ── radio groups ───────────────────────────────────────────────────────────*/

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

static uint32_t difficulty_string(unsigned int i)
{
    switch (i)
    {
        case 0:  return stringID_SONG_SELECT_EASY;
        case 1:  return stringID_SONG_SELECT_MEDIUM;
        case 2:  return stringID_SONG_SELECT_HARD;
        default: return stringID_SONG_SELECT_EXPERT;
    }
}

/* Map a mode-radio index to the game_mode_t committed into the selection. Index 1
 * is 2P (GAME_MODE_1P_HUMAN is retired but its enum value is left defined so any
 * persisted selection keeps its meaning). */
static uint8_t mode_value(unsigned int i)
{
    return (i == 0u) ? (uint8_t)GAME_MODE_1P_ROBOT : (uint8_t)GAME_MODE_2P;
}

/* Paint the active button in each group with its selected scheme, the rest with the
 * group's shared unselected scheme. The explicit invalidate is required: setScheme's
 * damage does not cover every pixel the AA corner pass touches. */
static void difficulty_repaint(void)
{
    unsigned int i;
    for (i = 0u; i < DIFFICULTY_COUNT; i++)
    {
        leButtonWidget *b = s_difficulty_btn[i];
        b->fn->setScheme(b, (i == s_difficulty) ? difficulty_selected_scheme(i)
                                                : &SCHEME_BUTTON_DIFFICULTY);
        b->fn->invalidate(b);
    }
}

/* Mode's selected look is a solid zinc-200 chip with no border (the mockup's
 * border-zinc-200 = its own fill), so the border type moves with the scheme. */
static void mode_repaint(void)
{
    unsigned int i;
    for (i = 0u; i < MODE_COUNT; i++)
    {
        leButtonWidget *b   = s_mode_btn[i];
        leBool          sel = (i == s_mode) ? LE_TRUE : LE_FALSE;

        b->fn->setScheme(b, sel ? &SCHEME_BUTTON_MODE_SELECTED : &SCHEME_BUTTON_MODE);
        b->fn->setBorderType(b, sel ? LE_WIDGET_BORDER_NONE : LE_WIDGET_BORDER_LINE);
        b->fn->invalidate(b);
    }
}

/* Released-event sinks: select the pressed entry and repaint its group. Re-picking
 * the active entry is a no-op (radio: no deselect). */
static void difficulty_on_release(leButtonWidget *btn)
{
    unsigned int i;
    for (i = 0u; i < DIFFICULTY_COUNT; i++)
    {
        if (btn == s_difficulty_btn[i]) { s_difficulty = i; difficulty_repaint(); return; }
    }
}

static void mode_on_release(leButtonWidget *btn)
{
    unsigned int i;
    for (i = 0u; i < MODE_COUNT; i++)
    {
        if (btn == s_mode_btn[i]) { s_mode = i; mode_repaint(); return; }
    }
}

/* ── selected-song detail ────────────────────────────────────────────────────*/

/* Point the album-art widget at the selected song's pre-decoded large strip, or blank
 * it (NULL) when the song or its cover is absent. The tier tint and the mockup's
 * from-black/80 gradient are baked into that PNG offline. */
static void song_art_show(const game_catalog_entry_t *e)
{
    const leImage *img = (e != NULL) ? GameArt_Large(e->setlist, e->index) : NULL;

    s_art.fn->setImage(&s_art, (leImage *)img);
}

/* The tier line over the cover: "★★★  TIER 3" for career tiers 1-8, "BONUS" for the
 * bonus setlist, coloured by tier (SongDetail_* — the shared palette matching the
 * baked art fade). */
static void tier_show(const game_catalog_entry_t *e)
{
    leLabelWidget *lbl  = &s_dyn_lbl[DYN_TIER];
    int            tier = (e != NULL) ? SongDetail_Tier(e->difficulty) : 0;
    char           txt[48];

    if (e == NULL)
    {
        set_dyn(DYN_TIER, "-");
        lbl->fn->setScheme(lbl, SongDetail_TierScheme(0));
        return;
    }

    SongDetail_TierTextLong(tier, txt, sizeof txt);
    set_dyn(DYN_TIER, txt);
    lbl->fn->setScheme(lbl, SongDetail_TierScheme(tier));
}

/* Mirror catalog entry `index` into the preview (all "-" if no such song). */
static void song_detail_show(int index)
{
    const game_catalog_entry_t *e = GameCatalog_At(index);
    char tmp[16];

    song_art_show(e);
    tier_show(e);

    if (e == NULL)
    {
        set_dyn(DYN_TITLE,  "-"); set_dyn(DYN_ARTIST,   "-");
        set_dyn(DYN_ALBUM,  "-"); set_dyn(DYN_YEAR,     "-");
        set_dyn(DYN_GENRE,  "-"); set_dyn(DYN_DURATION, "-");
        return;
    }

    set_dyn(DYN_TITLE,  e->title);
    set_dyn(DYN_ARTIST, e->artist);
    set_dyn(DYN_ALBUM,  e->album);
    set_dyn(DYN_GENRE,  e->genre);

    if (e->year != 0u)
    {
        (void)snprintf(tmp, sizeof tmp, "%u", (unsigned)e->year);
        set_dyn(DYN_YEAR, tmp);
    }
    else { set_dyn(DYN_YEAR, "-"); }

    SongDetail_Duration(e->length_s, tmp, sizeof tmp);
    set_dyn(DYN_DURATION, tmp);
}

/* ── catalog-backed song list ─────────────────────────────────────────────────*/

/* Row provider: map catalog entry `index` into the list's row cells. The tier badge
 * and duration are formatted into static scratch the widget reads immediately. */
static bool song_row(void *ctx, int index, songlist_row_t *out)
{
    const game_catalog_entry_t *e = GameCatalog_At(index);
    static char dur[8];
    static char badge[4];
    int         tier;

    (void)ctx;
    if (e == NULL) { return false; }

    if (e->length_s != 0u)
    {
        (void)snprintf(dur, sizeof dur, "%u:%02u",
                       (unsigned)(e->length_s / 60u), (unsigned)(e->length_s % 60u));
    }
    else { dur[0] = '\0'; }

    tier = SongDetail_Tier(e->difficulty);
    if (tier > 0) { (void)snprintf(badge, sizeof badge, "T%d", tier); }
    else          { (void)snprintf(badge, sizeof badge, "B");        }

    out->title      = e->title;
    out->artist     = e->artist;
    out->right      = dur;
    out->badge      = badge;
    out->badgeColor = SongDetail_TierColor(tier);
    return true;
}

static void song_selected(void *ctx, int index)
{
    const game_catalog_entry_t *e = GameCatalog_At(index);

    (void)ctx;
    LOG_INFO("songsel: selected #%d  %s - %s\r\n", index,
             (e != NULL) ? e->title : "?", (e != NULL) ? e->artist : "?");

    s_sel_index = index;
    song_detail_show(index);
}

/* Commit the highlighted song + current difficulty/mode as the gameplay selection.
 * The dashboard SONG card mirrors it via its Selection observer. */
static void songsel_commit(void)
{
    const game_catalog_entry_t *e = GameCatalog_At(s_sel_index);
    if (e == NULL) { return; }
    GameSelection_Set(e->setlist, e->index, (uint8_t)s_difficulty, mode_value(s_mode));
}

/* SELECT commits the highlighted song as the gameplay selection (mirrored onto the
 * dashboard SONG card) and closes the dialog. */
static void song_select_on_release(leButtonWidget *btn)
{
    (void)btn;
    songsel_commit();
    UiManager_CloseSongSelect();
}

/* The X (close) button dismisses the dialog without committing — the dashboard keeps
 * whatever song was last selected. ui_manager hides the OVR1 dialog and the OVR2
 * album-art strip together. */
static void song_close_on_release(leButtonWidget *btn)
{
    (void)btn;
    UiManager_CloseSongSelect();
}

/* ── build ──────────────────────────────────────────────────────────────────*/

static void build_header(void)
{
    int title_x = CONTENT_X + PAD_6;
    int close_x = CONTENT_X + CONTENT_W - PAD_6 - CLOSE_D;

    (void)add_cap(Marvin_PANEL_SONG_SELECT, title_x, CONTENT_Y + PAD_4,
                  close_x - title_x - PAD_2, CLOSE_D,
                  stringID_SONG_SELECT_SELECT_SONG, &SCHEME_TEXT_ZINC_200);

    /* The close button has no resting fill (the mockup only tints it on hover), so
     * there is nothing for ButtonAA to round — radius 0, no border. */
    leButtonWidget *close = add_button(Marvin_PANEL_SONG_SELECT, close_x, CONTENT_Y + PAD_4,
                                       CLOSE_D, CLOSE_D, 0u, &SCHEME_FILL_ZINC_900,
                                       0u, LE_FALSE);
    close->fn->setBackgroundType(close, LE_WIDGET_BACKGROUND_NONE);
    close->fn->setPressedImage(close, (leImage *)&BUTTON_ICON_CLOSE);
    close->fn->setReleasedImage(close, (leImage *)&BUTTON_ICON_CLOSE);
    close->fn->setReleasedEventCallback(close, song_close_on_release);

    add_rule(CONTENT_X, HDR_RULE_Y, CONTENT_W, 1, &SCHEME_FILL_ZINC_700);   /* border-b */
}

static void build_setlist(void)
{
    (void)add_dyn(Marvin_PANEL_SONG_SELECT, DYN_SETLIST, CONTENT_X + PAD_4, BODY_Y + PAD_2,
                  LEFT_CONTENT_W - 2 * PAD_4, TXT_XS,
                  (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500);
    add_rule(CONTENT_X, LIST_Y - 1, LEFT_CONTENT_W, 1, &SCHEME_FILL_ZINC_800);  /* border-b */

    s_list = SongList_New();
    if (s_list == NULL) { return; }

    (void)GameCatalog_Reload();

    s_list->fn->setPosition(s_list, CONTENT_X, LIST_Y);
    s_list->fn->setSize(s_list, LEFT_CONTENT_W, LIST_H);
    /* Transparent: the dialog's zinc-900 panel shows through behind the rows. Match its
     * scheme so the glyph anti-alias blends against that real backdrop. */
    s_list->fn->setScheme(s_list, &SCHEME_FILL_ZINC_900);
    SongList_SetTransparent(s_list, true);
    SongList_SetFonts(s_list,
                      (const leFont *)&DejaVuSansMonoBold_12,   /* title — text-xs bold */
                      (const leFont *)&DejaVuSansMono_12,       /* artist / duration    */
                      (const leFont *)&DejaVuSansMonoBold_12);  /* tier badge — bold    */
    SongList_SetRowHeight(s_list, LIST_ROW_H);
    SongList_SetModel(s_list, GameCatalog_Count(), song_row, NULL);
    SongList_SetSelectHandler(s_list, song_selected, NULL);

    Marvin_PANEL_SONG_SELECT->fn->addChild(Marvin_PANEL_SONG_SELECT, s_list);

    SongList_SetSelected(s_list, 0);   /* default to the first song (no-op while empty) */
}

/* One metric cell: a zinc-600 label over its zinc-200 value. */
static void add_metric(int col, int row, uint32_t cap_id, unsigned dyn_id)
{
    int x = ART_X + col * (INFO_COL_W + PAD_3);
    int y = INFO_Y + row * INFO_PITCH;

    (void)add_cap(Marvin_PANEL_SONG_SELECT, x, y, INFO_COL_W, TXT_XS,
                  cap_id, &SCHEME_TEXT_ZINC_600);
    (void)add_dyn(Marvin_PANEL_SONG_SELECT, dyn_id, x, y + TXT_XS, INFO_COL_W, TXT_SM,
                  (const leFont *)&DejaVuSansMono_14, &SCHEME_TEXT_ZINC_200);
}

static void build_center(void)
{
    /* The album-art rect itself is layer 3's business — nothing is drawn here, the
     * cover strip is composited over it from its own canvas. */
    add_metric(0, 0, stringID_SONG_SELECT_ALBUM,    DYN_ALBUM);
    add_metric(1, 0, stringID_SONG_SELECT_YEAR,     DYN_YEAR);
    add_metric(0, 1, stringID_SONG_SELECT_GENRE,    DYN_GENRE);
    add_metric(1, 1, stringID_SONG_SELECT_DURATION, DYN_DURATION);
}

static void build_right(void)
{
    unsigned int i;

    (void)add_cap(Marvin_PANEL_SONG_SELECT, R_X, DIFF_LBL_Y, R_W, TXT_XS,
                  stringID_SONG_SELECT_DIFFICULTY, &SCHEME_TEXT_ZINC_500);

    for (i = 0u; i < DIFFICULTY_COUNT; i++)
    {
        leButtonWidget *b = add_button(Marvin_PANEL_SONG_SELECT,
                                       R_X, DIFF_Y0 + (int)i * DIFF_PITCH, R_W, DIFF_H,
                                       difficulty_string(i), &SCHEME_BUTTON_DIFFICULTY,
                                       BTN_R, LE_TRUE);
        b->fn->setReleasedEventCallback(b, difficulty_on_release);
        s_difficulty_btn[i] = b;
    }

    (void)add_cap(Marvin_PANEL_SONG_SELECT, R_X, MODE_LBL_Y, R_W, TXT_XS,
                  stringID_SONG_SELECT_MODE, &SCHEME_TEXT_ZINC_500);

    s_mode_btn[0] = add_button(Marvin_PANEL_SONG_SELECT, R_X, MODE_Y0, R_W, MODE_H,
                               stringID_SONG_SELECT_1P_ROBOT, &SCHEME_BUTTON_MODE,
                               BTN_R, LE_TRUE);
    s_mode_btn[1] = add_button(Marvin_PANEL_SONG_SELECT, R_X, MODE_Y0 + MODE_PITCH, R_W, MODE_H,
                               stringID_SONG_SELECT_2P_ROBOT_vs_HUMAN, &SCHEME_BUTTON_MODE,
                               BTN_R, LE_TRUE);
    for (i = 0u; i < MODE_COUNT; i++)
    {
        s_mode_btn[i]->fn->setReleasedEventCallback(s_mode_btn[i], mode_on_release);
    }

    leButtonWidget *sel = add_button(Marvin_PANEL_SONG_SELECT, R_X, SEL_Y, R_W, SEL_H,
                                     stringID_SONG_SELECT_SELECT, &SCHEME_BUTTON_SELECT,
                                     SEL_R, LE_FALSE);
    sel->fn->setPressedImage(sel, (leImage *)&BUTTON_ICON_CHECK);
    sel->fn->setReleasedImage(sel, (leImage *)&BUTTON_ICON_CHECK);
    sel->fn->setImageMargin(sel, PAD_2);                  /* gap-2 between icon and text */
    sel->fn->setReleasedEventCallback(sel, song_select_on_release);
}

/* The album-art strip: layer 3's own canvas, whose widgets are built here because
 * their content is the selection this module owns. Cover, then a corner overlay that
 * rounds it, then the three text lines the mockup stacks at the bottom-left. */
static void build_album_art(void)
{
    leWidget *parent = Marvin_PANEL_SONG_SELECT_ALBUM_ART;

    _leImageWidget_Constructor(&s_art);
    s_art.fn->setPosition(&s_art, 0, 0);
    s_art.fn->setSize(&s_art, ART_W, ART_H);
    s_art.fn->setBackgroundType(&s_art, LE_WIDGET_BACKGROUND_NONE);
    s_art.fn->setBorderType(&s_art, LE_WIDGET_BORDER_NONE);
    parent->fn->addChild(parent, (leWidget *)&s_art);

    /* Round the cover: an empty overlay over it eats its corners back to the dialog
     * gray it sits in front of. IGNOREPICK because it paints on top of the whole strip
     * — in Legato those are the same property (leUtils_PickFromWidget keeps the last
     * child containing the point), so without it the overlay would swallow every touch
     * in the art rect. */
    leWidget *overlay = add_panel(parent, 0, 0, ART_W, ART_H, &SCHEME_FILL_ZINC_900);
    overlay->fn->setBackgroundType(overlay, LE_WIDGET_BACKGROUND_NONE);
    overlay->fn->setCornerRadius(overlay, ART_R);
    overlay->flags |= LE_WIDGET_IGNOREPICK;
    PanelAA_EnableRoundImage(overlay);

    /* p-4 from the bottom-left, stacked upward: artist (text-sm), title (text-2xl),
     * tier (text-xs, mb-1). */
    int artist_y = ART_H - PAD_4 - TXT_SM;
    int title_y  = artist_y - 30;                 /* text-2xl, leading-tight */
    int tier_y   = title_y - TXT_XS - 4;          /* mb-1 */

    (void)add_dyn(parent, DYN_TIER,   PAD_4, tier_y,   ART_W - 2 * PAD_4, TXT_XS,
                  (const leFont *)&DejaVuSansMonoBold_12, SongDetail_TierScheme(0));
    (void)add_dyn(parent, DYN_TITLE,  PAD_4, title_y,  ART_W - 2 * PAD_4, 30,
                  (const leFont *)&DejaVuSansMonoBold_24, &SCHEME_TEXT_WHITE);
    (void)add_dyn(parent, DYN_ARTIST, PAD_4, artist_y, ART_W - 2 * PAD_4, TXT_SM,
                  (const leFont *)&DejaVuSansMono_14, &SCHEME_TEXT_ZINC_300);
}

/* ── rounded dialog corners ─────────────────────────────────────────────────
 * The dialog is an OPAQUE RGB565 overlay, so its rounded corners cannot be
 * transparent — there is no per-pixel alpha to reveal the layer below. Instead the
 * corner boxes are filled with the pixels the BASE view has at the same screen
 * position, anti-aliased against the dialog's own fill and 1px border. The result is
 * indistinguishable from transparency for as long as the dialog is up, because what
 * sits behind those four 12x12 boxes is static page/card background.
 *
 * Snapshot semantics: this runs on open (ui_manager), not per frame, so live content
 * moving under a corner would go stale. Both surfaces are RGB565 — if the base view's
 * is not, the dialog simply stays square rather than showing converted garbage. */
typedef struct { const uint16_t *px; uint32_t stride; int x0, y0; } base_sampler_t;

/* Scale an RGB565 pixel by (100 - MODAL_SCRIM_PCT)%, per channel at its own depth.
 * Required because the corner holds a COPY of the base view, while what the panel
 * actually shows there is the base view as dimmed by the modal scrim — an undimmed
 * copy would read as four bright notches in the dialog's corners. Same arithmetic the
 * LCDC blender does for the rest of the screen. */
static uint16_t scrim_dim565(uint16_t px)
{
    uint32_t keep = 100u - MODAL_SCRIM_PCT;
    uint32_t r    = (((uint32_t)px >> 11) & 0x1Fu) * keep / 100u;
    uint32_t g    = (((uint32_t)px >>  5) & 0x3Fu) * keep / 100u;
    uint32_t b    = ( (uint32_t)px        & 0x1Fu) * keep / 100u;

    return (uint16_t)((r << 11) | (g << 5) | b);
}

static leColor base_sample(void *ctx, int32_t x, int32_t y)
{
    const base_sampler_t *s = (const base_sampler_t *)ctx;

    return (leColor)scrim_dim565(s->px[(uint32_t)(s->y0 + y) * s->stride + (uint32_t)(s->x0 + x)]);
}

void ScreenSongSelect_RoundCorners(void)
{
    const void       *buf  = NULL;
    uint16_t          w    = 0u;
    uint16_t          h    = 0u;
    GFXC_COLOR_FORMAT mode = GFX_COLOR_MODE_RGB_565;
    base_sampler_t    ctx;
    leRect            rect = { 0, 0, (int)SONGSEL_W, (int)SONGSEL_H };

    if (!UiSurface_Get(UiManager_BaseCanvas(), &buf, &w, &h, &mode)) { return; }
    if (buf == NULL || mode != GFX_COLOR_MODE_RGB_565)               { return; }
    if (w < SONGSEL_X + SONGSEL_W || h < SONGSEL_Y + SONGSEL_H)      { return; }

    ctx.px     = (const uint16_t *)buf;
    ctx.stride = w;
    ctx.x0     = SONGSEL_X;
    ctx.y0     = SONGSEL_Y;

    AaCorners_RenderSurface565(s_fb_songsel, SONGSEL_W, &rect, DLG_R, 1u,
                               leScheme_GetColor(&SCHEME_FILL_ZINC_900, LE_SCHM_BASE,
                                                 LE_COLOR_MODE_RGB_565),
                               leScheme_GetColor(&SCHEME_FILL_ZINC_900, LE_SCHM_SHADOWDARK,
                                                 LE_COLOR_MODE_RGB_565),
                               base_sample, &ctx);
}

void ScreenSongSelect_ArtOrigin(int *x, int *y)
{
    if (x != NULL) { *x = SONGSEL_X + ART_X; }
    if (y != NULL) { *y = SONGSEL_Y + ART_Y; }
}

void ScreenSongSelect_Setup(void)
{
    char setlist[32];

    /* Center the dialog. The root is on Legato layer 2; the canvas window positions
     * that layer's pixels on the display. */
    gfxcSetWindowSize(CANVAS_SONGSEL, SONGSEL_W, SONGSEL_H);
    gfxcSetWindowPosition(CANVAS_SONGSEL, SONGSEL_X, SONGSEL_Y);

    /* The dialog's own rounded 1px border: the classic skin draws it stepped in the
     * scheme's SHADOWDARK (#404040 ≈ zinc-700), and ScreenSongSelect_RoundCorners
     * re-cuts the four corner boxes properly once there is a base view to sample. */
    Marvin_PANEL_SONG_SELECT->fn->setBorderType(Marvin_PANEL_SONG_SELECT, LE_WIDGET_BORDER_LINE);
    Marvin_PANEL_SONG_SELECT->fn->setCornerRadius(Marvin_PANEL_SONG_SELECT, DLG_R);

    /* Column rules (border-r), spanning the body but stopping inside the dialog's own
     * bottom border. */
    add_rule(LEFT_RULE_X,   BODY_Y, 1, BODY_H, &SCHEME_FILL_ZINC_700);
    add_rule(CENTER_RULE_X, BODY_Y, 1, BODY_H, &SCHEME_FILL_ZINC_700);

    build_header();
    build_setlist();
    build_center();
    build_right();
    build_album_art();

    difficulty_repaint();   /* apply the default selections */
    mode_repaint();

    (void)snprintf(setlist, sizeof setlist, "SETLIST \xC2\xB7 %d TRACKS", GameCatalog_Count());
    set_dyn(DYN_SETLIST, setlist);

    song_detail_show(0);   /* mirror the default (first-song) selection */

    /* Seed the committed selection so the dashboard SONG card shows a coherent default
     * at boot. Runs after ScreenDashboard_Setup has registered its Selection observer,
     * so this render lands on the card. */
    songsel_commit();
}
