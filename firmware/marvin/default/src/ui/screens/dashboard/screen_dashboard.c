#include "ui/screens/dashboard/screen_dashboard.h"

#include <stdio.h>

#include "FreeRTOS.h"        /* configASSERT */

#include "ui/ui_manager.h"   /* CANVAS_DASH, BASE_W, BASE_H, UiManager_OpenSongSelect */
#include "ui/song_detail.h"
#include "ui/titlebar.h"         /* shared hamburger + logos titlebar */
#include "ui/dashboard_feed.h"   /* DashboardFeed_PostSelection — route updates via the feed */
#include "ui/widgets/button_aa/widget_button_aa.h"
#include "ui/widgets/panel_aa/widget_panel_aa.h"
#include "ui/widgets/bar/widget_bar.h"

#include "actuator/guitar_cmd.h"   /* GUITAR_BTN_* fret mask layout */
#include "detector/detector.h"     /* DETECTOR_* — the detector selector rows */

#include "game/game_catalog.h"
#include "game/game_art.h"
#include "game/game_selection.h"
#include "game/game_controller.h"
#include "results/results.h"   /* Results_Set/GetPlayer — the human card's name */
#include "util/legato_utf8.h"

#include "ui/gfx/ui_surface.h"
#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/string/legato_fixedstring.h"                /* runtime label text */
#include "gfx/legato/string/legato_tablestring.h"                /* design captions */
#include "gfx/legato/widget/legato_widget.h"
#include "gfx/legato/widget/button/legato_widget_button.h"
#include "gfx/legato/widget/label/legato_widget_label.h"
#include "gfx/legato/widget/image/legato_widget_image.h"
#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"                  /* stringID_*, fonts, art */
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* Marvin_PANEL_DASHBOARD */

/* Operator dashboard, built programmatically into the empty MGS layer-0 panel
 * (Marvin_PANEL_DASHBOARD) — the screen_bus.c model. Ports the mockup's
 * DashboardScreen.tsx (PlayerPerformance / VideoStream / NowPlaying) with Tailwind
 * units resolved to pixels: robot card, centre column (live video over the song card),
 * human card.
 *
 * Static captions come from the DESIGN string table (leTableString + stringID_*);
 * everything the feed rewrites at runtime is a leFixedString (s_dyn_*). Live data
 * arrives only through ui/dashboard_feed.c → the ScreenDashboard_Apply* entry points,
 * which are the sole writers of this screen's widgets.
 *
 * Legato quirk: the image widget exposes only its *internal* in-place constructor
 * (external linkage, undeclared in the public header); the public
 * leImageWidget_Constructor is declared but never defined. leImageWidget_New uses this
 * same symbol, so calling it directly for static allocation is safe (same as
 * ui/titlebar.c). */
extern void _leImageWidget_Constructor(leImageWidget *img);

/* Dashboard surface, non-cached so the 2D engine and LCDC DMA read CPU-rendered
 * pixels coherently; 32-byte aligned. RGB565 — steady-state UI needs no more. */
#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

static uint16_t FB_NOCACHE s_fb[BASE_W * BASE_H];

/* ── layout ─────────────────────────────────────────────────────────────────
 * Content spans the panel inside a 12px margin, below the shared titlebar; three
 * columns at the mockup's flex sizes (flex-1 / w-[720px] / flex-1) with gap-3 = 12.
 * Card internals use the mockup's p-3 = 12 padding and gap-2.5 = 10 rhythm; card
 * radius 4 = Tailwind `rounded`, text-2xl/xl/base/sm/xs = 24/20/16/14/12. */
#define MARGIN       12
#define GAP          12
#define CONTENT_Y    77                              /* titlebar 12 + 53 + GAP */
#define CONTENT_W    (BASE_W - 2 * MARGIN)           /* 1256 */
#define CONTENT_H    (BASE_H - CONTENT_Y - 7)        /* 716 */

#define SIDE_W      256                              /* player cards            */
#define CENTER_W    720                              /* video + song card       */
#define CENTER_X    (SIDE_W + GAP)                   /* 268, content-relative   */
#define HUMAN_X     (CENTER_X + CENTER_W + GAP)      /* 1000                    */

#define CARD_R        4
#define PAD          12                              /* card p-3                */

/* Player card (mockup PlayerPerformance) — art at the top with the name/state
 * overlaid on it, then the metric column. */
#define ART_H       208                              /* h-52 */
#define ART_INSET     1                              /* keeps the card border visible */
#define NAME_Y      168
#define SUB_Y       187
#define STATE_X     186
#define STATE_Y     182
#define STATE_W      57
#define STATE_H      20
#define COL_X       (ART_INSET + PAD)                /* 13  */
#define COL_W       (SIDE_W - 2 * COL_X)             /* 230 */

#define SCORE_Y     221                              /* label row + multiplier pills */
#define MULT_W       23
#define MULT_PITCH   27
#define MULT_X      142
#define VALUE_Y     241                              /* text-2xl score               */
#define VALUE_H      36
#define STREAK_Y    287
#define STREAK_BAR_Y 307
#define BAR_H         6                              /* h-1.5 */

/* Robot-only rows below the streak bar. */
#define R_RULE1_Y   323
#define R_FRET_Y    334
#define R_FRETS_Y   356
#define FRET_W       43
#define FRET_PITCH   47
#define FRET_H       24                              /* h-6 */
#define R_STRUM_Y   392
#define R_RULE2_Y   422

/* Detector + actuator rows, derived rather than tabulated: the mockup's own column is
 * top-aligned with fixed gaps and leaves whatever is left over empty at the foot, which
 * on a 716px card was ~34px of dead space. OPT_H is sized so the four option rows and
 * the two section headings consume it — taller touch targets (48px, up from the
 * mockup's py-3 = 42) rather than a gap nobody asked for. Everything below R_RULE2_Y
 * follows from OPT_H, so re-tuning it keeps the block coherent; the block ends at
 * R_ACT_ROW2_Y + OPT_H = 706, i.e. a 10px foot against the 12px sides. */
#define LBL_H        16                              /* text-xs heading  */
#define LBL_GAP       6                              /* heading mb-1.5   */
#define OPT_GAP       6                              /* option gap-1.5   */
#define ACT_GAP       8                              /* actuator grid gap-2 */
#define SEC_GAP      12                              /* section rule → next heading */
#define OPT_H        48

#define R_DET_Y      (R_RULE2_Y + 11)
#define R_DET1_Y     (R_DET_Y + LBL_H + LBL_GAP)
#define R_DET2_Y     (R_DET1_Y + OPT_H + OPT_GAP)
#define R_RULE3_Y    (R_DET2_Y + OPT_H + SEC_GAP)
#define R_ACT_Y      (R_RULE3_Y + 11)
#define R_ACT_ROW_Y  (R_ACT_Y + LBL_H + LBL_GAP)
#define ACT_W        ((COL_W - ACT_GAP) / 2)         /* grid-cols-2 */

/* Video card + test pattern (mockup VideoStream). */
#define VIDEO_W     CENTER_W
#define VIDEO_H     480
#define BAR_TOP_H   320                              /* 2/3 of the height     */
#define BAR_MID_H    40                              /* height/12             */
#define PILL_H       24

/* PLEASE STAND BY slate. STAND_BY_W is the message's measured width in the font its
 * design string is bound to (DejaVuSansMonoBold_40: 15 glyphs x 24px advance); the box
 * pads it by the mockup's 20px each side and keeps the mockup's proportion of box height
 * to text size (50/32), centred on the mockup's baseline at VIDEO_H - 65. */
#define STAND_BY_W       360
#define STAND_BY_BOX_W   (STAND_BY_W + 2 * 20)
#define STAND_BY_BOX_H    62
#define STAND_BY_Y       (VIDEO_H - 65 - STAND_BY_BOX_H / 2)

/* LE_VALIGN_MIDDLE centres the font's ASCENT box, not the ink: leStringUtils_KerningRect
 * sizes the arranged rect to `stringHeight - font->height + font->baseline`, and the
 * ascent reserves space above cap height for accents that all-caps text never uses. So
 * the glyphs land low by half that slack — for DejaVuSansMonoBold_40, baseline 33 against
 * a 30px cap height (bearingY of P/L/E/A/S/N/D/B/Y), i.e. ~1.5px. The label is lifted by
 * that much; the box does not move. Only conspicuous at this size — at 12px it is a
 * fraction of a pixel, which is why no other label needs this. */
#define STAND_BY_INK_LIFT  2

/* Song card (mockup NowPlaying). */
#define SONG_Y      (VIDEO_H + GAP)                  /* 492 */
#define SONG_H      (CONTENT_H - SONG_Y)             /* 224 */
#define ART_XY       16                              /* p-4 */
#define ALBUM_D     144                              /* w-36 h-36 */
#define ALBUM_R       8                              /* rounded-lg */
#define INFO_X      176                              /* art + gap-4 */
#define INFO_W      349
#define SPLIT_X     542                              /* left flex-1 | 1px | w-44 */
#define GAME_X      (SPLIT_X + 1)
#define GAME_W      176

#define FB_MAX_DYN_CAP  80

/* ── widget pools ───────────────────────────────────────────────────────────
 * Static storage, constructed in place — no allocator (see the project's static
 * allocation rule). Sized to the built screen; configASSERT catches undersizing at
 * bring-up. */
#define WGT_MAX   64u
#define CAP_MAX   32u    /* design-string captions */
#define BTN_MAX   24u
#define IMG_MAX    4u

static leWidget       s_wgt[WGT_MAX];
static unsigned       s_nwgt;
static leLabelWidget  s_cap[CAP_MAX];
static leTableString  s_cap_str[CAP_MAX];
static unsigned       s_ncap;
static leButtonWidget s_btn[BTN_MAX];
static leTableString  s_btn_str[BTN_MAX];
static unsigned       s_nbtn;
static leImageWidget  s_img[IMG_MAX];
static unsigned       s_nimg;

/* A two-state caption: both texts are design strings and the label is pointed at one
 * or the other, rather than a runtime literal — so PLAYING/IDLE, ACTIVE/IDLE and
 * START/STOP stay translatable (a leTableString may be shared by several widgets, the
 * same way MGS binds one design string to many). */
typedef struct
{
    leLabelWidget *lbl;
    leTableString  off;
    leTableString  on;
} dual_cap_t;

static dual_cap_t s_cap_state_robot, s_cap_state_human, s_cap_strum;

static void dual_init(dual_cap_t *d, leLabelWidget *lbl, uint32_t off_id, uint32_t on_id)
{
    d->lbl = lbl;
    leTableString_Constructor(&d->off, off_id);
    leTableString_Constructor(&d->on,  on_id);
}

static void dual_set(dual_cap_t *d, bool on)
{
    if (d->lbl != NULL)
    {
        d->lbl->fn->setString(d->lbl, (leString *)(on ? &d->on : &d->off));
    }
}

/* Runtime-written text: every label the feed rewrites owns a fixed string. */
enum {
    DYN_R_SCORE, DYN_R_STREAK,
    DYN_H_NAME, DYN_H_SCORE, DYN_H_STREAK,
    DYN_S_STATUS, DYN_S_TITLE, DYN_S_ARTIST, DYN_S_ALBUM,
    DYN_S_GENRE, DYN_S_DURATION, DYN_S_TIER,
    DYN_S_MODE, DYN_S_DIFF, DYN_S_ELAPSED, DYN_S_TOTAL,
    DYN_COUNT
};

static leChar        s_dyn_buf[DYN_COUNT][FB_MAX_DYN_CAP];
static leFixedString s_dyn_str[DYN_COUNT];
static leLabelWidget s_dyn_lbl[DYN_COUNT];

/* Handles the Apply* entry points need after the build. */
static leWidget            *s_content;         /* the whole three-column region     */
static leWidget            *s_titlebar;
static leButtonWidget      *s_fret[5];
static leButtonWidget      *s_mult_robot[4];
static leButtonWidget      *s_mult_human[4];
static leButtonWidget      *s_detector[2];     /* [0] = CV, [1] = fretboard/NN      */

/* Actuator enables, in the mockup's order. Nothing is driven yet — see
 * actuator_on_release — so the state lives here rather than being read back from the
 * nodes. Order is grid order: GUITAR, LEMMY on the first row, LIGHTSHOW on the second. */
#define ACTUATOR_COUNT  3u
static leButtonWidget      *s_actuator[ACTUATOR_COUNT];
static leWidget            *s_actuator_led[ACTUATOR_COUNT];
static bool                 s_actuator_on[ACTUATOR_COUNT];
static leButtonWidget      *s_start;
static leTableString        s_start_cap, s_stop_cap;
static leWidget            *s_state_robot, *s_state_robot_led;
static leWidget            *s_video_pattern;   /* SMPTE bars group, hidden while video is up */
static leWidget            *s_state_human, *s_state_human_led;
static leWidget            *s_strum_pill;
static leWidget            *s_diff_pill;
static leImageWidget       *s_album;
static leWidget            *s_bar_play, *s_bar_streak_robot, *s_bar_streak_human;

/* Selected song length (s), captured on ApplySelection so ApplyPlaytime can scale
 * elapsed play time into the bar fill. 0 = unknown → no fill. */
static uint16_t s_song_len_s;

/* Last state pushed to the run-dependent chrome (state pills, START/STOP), so a
 * status event that doesn't change it costs no repaint. */
static bool s_run_active;

/* ── build helpers ──────────────────────────────────────────────────────────*/

static leWidget *add_panel(leWidget *parent, int x, int y, int w, int h,
                           const leScheme *scheme, leBool fill)
{
    configASSERT(s_nwgt < WGT_MAX);

    leWidget *p = &s_wgt[s_nwgt++];
    leWidget_Constructor(p);
    p->fn->setPosition(p, x, y);
    p->fn->setSize(p, w, h);
    if (scheme != NULL) { p->fn->setScheme(p, scheme); }
    p->fn->setBackgroundType(p, fill ? LE_WIDGET_BACKGROUND_FILL : LE_WIDGET_BACKGROUND_NONE);
    p->fn->setBorderType(p, LE_WIDGET_BORDER_NONE);
    parent->fn->addChild(parent, p);
    return p;
}

/* A card: zinc-900 fill, square. Its rounded corners + 1px border come from
 * add_card_frame, added AFTER the card's contents (see there). */
static leWidget *add_card(leWidget *parent, int x, int y, int w, int h)
{
    return add_panel(parent, x, y, w, h, &SCHEME_FILL_ZINC_900, LE_TRUE);
}

/* The card's rounded frame, drawn LAST so it lands on top of everything the card
 * holds. Two jobs in one widget: the classic skin draws a 1px rounded border in
 * SHADOWDARK (#404040 ≈ zinc-700, the mockup's border-zinc-700), then PanelAA's
 * round-image pass eats the four corners back to the panel's BASE — black, the page
 * behind the card. That is what makes an image flush to the card's top edge (the
 * player art) read as clipped to the radius, which a fill-and-border card cannot do:
 * the art paints over its top corners. SCHEME_BACKGROUND supplies both colours. */
static void add_card_frame(leWidget *parent, int x, int y, int w, int h, uint32_t radius)
{
    configASSERT(s_nwgt < WGT_MAX);

    leWidget *p = &s_wgt[s_nwgt++];
    leWidget_Constructor(p);
    p->fn->setPosition(p, x, y);
    p->fn->setSize(p, w, h);
    p->fn->setScheme(p, &SCHEME_BACKGROUND);
    p->fn->setBackgroundType(p, LE_WIDGET_BACKGROUND_NONE);
    p->fn->setBorderType(p, LE_WIDGET_BORDER_LINE);
    p->fn->setCornerRadius(p, radius);
    /* Never a pick target. leUtils_PickFromWidget keeps the LAST child whose rect
     * contains the point, so a full-card overlay added after the card's contents (which
     * is what makes it paint on top) would otherwise swallow every touch inside the
     * card. IGNOREPICK makes the pick walk discard it and keep the real target. */
    p->flags |= LE_WIDGET_IGNOREPICK;
    PanelAA_EnableRoundImage(p);
    parent->fn->addChild(parent, p);
}

/* A 1px horizontal rule — the mockup's `h-px bg-zinc-800` section divider. */
static void add_rule(leWidget *parent, int x, int y, int w)
{
    (void)add_panel(parent, x, y, w, 1, &SCHEME_FILL_ZINC_800, LE_TRUE);
}

/* An anti-aliased dot (status LEDs). */
static leWidget *add_dot(leWidget *parent, int x, int y, int d, const leScheme *scheme)
{
    leWidget *p = add_panel(parent, x, y, d, d, scheme, LE_TRUE);
    PanelAA_EnableDot(p);
    return p;
}

/* A rounded pill panel (state chips). */
static leWidget *add_pill(leWidget *parent, int x, int y, int w, int h,
                          const leScheme *scheme)
{
    leWidget *p = add_panel(parent, x, y, w, h, scheme, LE_TRUE);
    p->fn->setCornerRadius(p, CARD_R);
    PanelAA_Enable(p);
    return p;
}

/* A design-string caption. The string carries its own font via the design's binding,
 * so static text needs no font argument here. */
static leLabelWidget *add_cap(leWidget *parent, int x, int y, int w, int h, uint32_t string_id,
                              const leScheme *scheme, leHAlignment ha)
{
    configASSERT(s_ncap < CAP_MAX);

    leLabelWidget *l = &s_cap[s_ncap];
    leLabelWidget_Constructor(l);
    l->fn->setPosition(l, x, y);
    l->fn->setSize(l, w, h);
    l->fn->setScheme(l, scheme);
    l->fn->setBackgroundType(l, LE_WIDGET_BACKGROUND_NONE);
    l->fn->setHAlignment(l, ha);
    l->fn->setVAlignment(l, LE_VALIGN_MIDDLE);

    leTableString_Constructor(&s_cap_str[s_ncap], string_id);
    l->fn->setString(l, (leString *)&s_cap_str[s_ncap]);
    s_ncap++;

    parent->fn->addChild(parent, (leWidget *)l);
    return l;
}

/* A runtime-written label: owns one of the s_dyn fixed strings, addressed by its DYN_*
 * id. The font is explicit — there is no design string to inherit one from. */
static leLabelWidget *add_dyn(leWidget *parent, unsigned id, int x, int y, int w, int h,
                              const leFont *font, const leScheme *scheme, leHAlignment ha)
{
    configASSERT(id < DYN_COUNT);

    leLabelWidget *l = &s_dyn_lbl[id];
    leLabelWidget_Constructor(l);
    l->fn->setPosition(l, x, y);
    l->fn->setSize(l, w, h);
    l->fn->setScheme(l, scheme);
    l->fn->setBackgroundType(l, LE_WIDGET_BACKGROUND_NONE);
    l->fn->setHAlignment(l, ha);
    l->fn->setVAlignment(l, LE_VALIGN_MIDDLE);

    leFixedString_Constructor(&s_dyn_str[id], s_dyn_buf[id], FB_MAX_DYN_CAP);
    leString *fs = (leString *)&s_dyn_str[id];
    fs->fn->setFont(fs, (leFont *)font);
    l->fn->setString(l, fs);

    parent->fn->addChild(parent, (leWidget *)l);
    return l;
}

static void set_dyn(unsigned id, const char *text)
{
    (void)lestring_set_utf8((leString *)&s_dyn_str[id],
                            (text != NULL && text[0] != '\0') ? text : "-");
}

/* A button with a design-string caption, rounded + AA. */
static leButtonWidget *add_button(leWidget *parent, int x, int y, int w, int h,
                                  uint32_t string_id, const leScheme *scheme)
{
    configASSERT(s_nbtn < BTN_MAX);
    unsigned i = s_nbtn++;

    leButtonWidget *b = &s_btn[i];
    leButtonWidget_Constructor(b);
    b->fn->setPosition(b, x, y);
    b->fn->setSize(b, w, h);
    b->fn->setScheme(b, scheme);
    b->fn->setBackgroundType(b, LE_WIDGET_BACKGROUND_FILL);
    b->fn->setBorderType(b, LE_WIDGET_BORDER_NONE);
    b->fn->setCornerRadius(b, CARD_R);
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

/* A value bar (ui/widgets/bar): a filled track panel whose fill, pill ends and optional
 * dithered gradient are painted by the override. Radius is carried by the module, never
 * the widget style — see widget_bar.h. `rgb_to == rgb_from` is a flat fill. */
static leWidget *add_bar(leWidget *parent, int x, int y, int w, int h, uint32_t radius,
                         uint32_t rgb_from, uint32_t rgb_to)
{
    leWidget *bar = add_panel(parent, x, y, w, h, &SCHEME_FILL_ZINC_800, LE_TRUE);
    Bar_Enable(bar, radius, &SCHEME_FILL_ZINC_900, rgb_from, rgb_to);
    return bar;
}

static leImageWidget *add_image(leWidget *parent, int x, int y, int w, int h,
                                const leImage *image)
{
    configASSERT(s_nimg < IMG_MAX);

    leImageWidget *img = &s_img[s_nimg++];
    _leImageWidget_Constructor(img);
    img->fn->setPosition(img, x, y);
    img->fn->setSize(img, w, h);
    img->fn->setBackgroundType(img, LE_WIDGET_BACKGROUND_NONE);
    img->fn->setBorderType(img, LE_WIDGET_BORDER_NONE);
    if (image != NULL) { img->fn->setImage(img, (leImage *)image); }
    parent->fn->addChild(parent, (leWidget *)img);
    return img;
}

/* ── interactions ───────────────────────────────────────────────────────────*/

static void select_song_on_release(leButtonWidget *btn)
{
    (void)btn;
    UiManager_OpenSongSelect();
}

/* Commit callback from the on-screen keyboard, which START raises for a 2-player match:
 * record the entered name as the current player, show it on the human card, then start
 * the run. Empty input leaves the player unchanged. */
/* Confirmation of the 2P name prompt, and the start of the run. Only reached via the
 * keyboard's OK — dismissing it with X never calls this, which is what makes X a true
 * cancel of the game start rather than just of the name edit. */
static void player_name_committed(const char *name)
{
    if (name != NULL && name[0] != '\0')
    {
        Results_SetPlayer(name);
        set_dyn(DYN_H_NAME, Results_GetPlayer());
        s_dyn_lbl[DYN_H_NAME].fn->invalidate(&s_dyn_lbl[DYN_H_NAME]);
    }
    GameController_Start();
}

/* START begins a run from the committed selection; while one is in flight the same
 * button reads STOP and aborts it. A 2-player match has a human competitor, so START
 * first prompts for their name and starts on OK. */
static void start_on_release(leButtonWidget *btn)
{
    const game_selection_t *sel = GameSelection_Get();

    (void)btn;

    if (GameController_IsBusy()) { GameController_Stop(); return; }

    if (sel->valid && sel->mode == GAME_MODE_2P)
    {
        UiManager_OpenKeyboard("ENTER PLAYER NAME", Results_GetPlayer(), 32,
                               player_name_committed);
        return;
    }
    GameController_Start();
}

/* Paint one detector row selected/unselected. A scheme carries a single text colour,
 * so the pair is swapped rather than restyled (the nav drawer's idiom). */
static void detector_paint(unsigned i, bool on)
{
    leButtonWidget *b = s_detector[i];
    b->fn->setScheme(b, on ? &SCHEME_TOGGLE_ON : &SCHEME_TOGGLE_OFF);
    b->fn->invalidate(b);
}

static void detector_show_active(void)
{
    detector_id_t active = Detector_GetActive();
    detector_paint(0u, active == DETECTOR_CV_MARVIN_V1);
    detector_paint(1u, active == DETECTOR_FRETBOARD);
}

/* The two DETECTOR rows pick which detector drives the timing pipeline. The mockup
 * labels them Computer Vision / Neural Network; the fretboard node IS the neural-net
 * implementation, so the caption is the mockup's and the bus module keeps its name. */
static void detector_on_release(leButtonWidget *btn)
{
    Detector_SetActive((btn == s_detector[1]) ? DETECTOR_FRETBOARD : DETECTOR_CV_MARVIN_V1);
    detector_show_active();
}

/* Paint one actuator toggle. Same scheme pair as the detector rows, plus the state dot
 * the mockup puts at the button's right edge. */
static void actuator_paint(unsigned i)
{
    bool on = s_actuator_on[i];

    s_actuator[i]->fn->setScheme(s_actuator[i], on ? &SCHEME_TOGGLE_ON : &SCHEME_TOGGLE_OFF);
    s_actuator_led[i]->fn->setScheme(s_actuator_led[i],
                                     on ? &SCHEME_FILL_GREEN_400 : &SCHEME_FILL_ZINC_600);
    s_actuator[i]->fn->invalidate(s_actuator[i]);
    s_actuator_led[i]->fn->invalidate(s_actuator_led[i]);
}

/* Toggle one actuator node's enable. The node side is not connected yet — this holds the
 * operator's intent and shows it; hooking each entry up to its node (the guitar's output
 * enable, lemmy's motion, lightshow's output) is a call per index from here. */
static void actuator_on_release(leButtonWidget *btn)
{
    for (unsigned i = 0u; i < ACTUATOR_COUNT; i++)
    {
        if (s_actuator[i] == btn)
        {
            s_actuator_on[i] = !s_actuator_on[i];
            actuator_paint(i);
            break;
        }
    }
}

/* ── build: robot card ──────────────────────────────────────────────────────*/

static void build_player_head(leWidget *card, const leImage *art, uint32_t name_id,
                              unsigned name_dyn, const leScheme *name_scheme,
                              uint32_t role_id, leWidget **state, leWidget **led,
                              dual_cap_t *state_cap)
{
    (void)add_image(card, ART_INSET, ART_INSET, SIDE_W - 2 * ART_INSET, ART_H, art);

    /* The art is pre-faded to zinc-900 at its foot (the `_gradient` variants in
     * assets/image), which is what the mockup's gradient overlay does, so the name
     * sits on it directly. */
    if (name_id != 0u)
    {
        add_cap(card, COL_X, NAME_Y, 160, 18, name_id, name_scheme, LE_HALIGN_LEFT);
    }
    else
    {
        (void)add_dyn(card, name_dyn, COL_X, NAME_Y, 160, 18,
                      (const leFont *)&DejaVuSansMonoBold_14, name_scheme, LE_HALIGN_LEFT);
    }
    (void)add_cap(card, COL_X, SUB_Y, 160, 16, role_id, &SCHEME_TEXT_ZINC_400, LE_HALIGN_LEFT);

    *state = add_pill(card, STATE_X, STATE_Y, STATE_W, STATE_H, &SCHEME_FILL_ZINC_800);
    *led   = add_dot(*state, 8, 7, 6, &SCHEME_FILL_ZINC_600);
    dual_init(state_cap,
              add_cap(*state, 20, 2, 32, 16, stringID_PLAYER_ROBOT_Status,
                      &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT),
              stringID_PLAYER_ROBOT_Status, stringID_PLAYER_STATUS_PLAYING);
}

/* SCORE caption + the four multiplier pills + the big score value. */
static void build_score_block(leWidget *card, unsigned score_dyn,
                              const leScheme *value_scheme, const leScheme *pill_scheme,
                              leButtonWidget **mult)
{
    static const uint32_t MULT_ID[4] = {
        stringID_PLAYER_MULTIPLIER_1x, stringID_PLAYER_MULTIPLIER_2x,
        stringID_PLAYER_MULTIPLIER_3x, stringID_PLAYER_MULTIPLIER_4x,
    };

    (void)add_cap(card, COL_X, SCORE_Y, 120, 16, stringID_PLAYER_SCORE,
            &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT);

    for (unsigned i = 0u; i < 4u; i++)
    {
        mult[i] = add_button(card, MULT_X + (int)i * MULT_PITCH, SCORE_Y, MULT_W, 16,
                             MULT_ID[i], pill_scheme);
        mult[i]->fn->setToggleable(mult[i], LE_TRUE);
        mult[i]->widget.flags |= LE_WIDGET_IGNOREEVENTS;
    }

    (void)add_dyn(card, score_dyn, COL_X, VALUE_Y, COL_W, VALUE_H,
                  (const leFont *)&DejaVuSansMonoBold_24, value_scheme, LE_HALIGN_LEFT);
}

/* STREAK caption + value + the bar the mockup added (streak / STREAK_FULL). */
static void build_streak_block(leWidget *card, unsigned streak_dyn, leWidget **bar,
                               uint32_t fill)
{
    (void)add_cap(card, COL_X, STREAK_Y, 60, 16, stringID_PLAYER_STREAK,
            &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT);
    (void)add_dyn(card, streak_dyn, COL_X + 60, STREAK_Y, COL_W - 60, 16,
                  (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_300,
                  LE_HALIGN_RIGHT);
    *bar = add_bar(card, COL_X, STREAK_BAR_Y, COL_W, BAR_H, BAR_H / 2, fill, fill);
}

static void build_robot_card(leWidget *content)
{
    static const struct { const leScheme *scheme; } FRET[5] = {
        { &SCHEME_GUITAR_FRET_GREEN  }, { &SCHEME_GUITAR_FRET_RED    },
        { &SCHEME_GUITAR_FRET_YELLOW }, { &SCHEME_GUITAR_FRET_BLUE   },
        { &SCHEME_GUITAR_FRET_ORANGE },
    };

    leWidget *card = add_card(content, 0, 0, SIDE_W, CONTENT_H);

    build_player_head(card, &PLAYER_ROBOT_ART, stringID_PLAYER_ROBOT_Name, 0u,
                      &STYLE_TEXT_ROBOT, stringID_PLAYER_ROBOT_RobotPlayer,
                      &s_state_robot, &s_state_robot_led, &s_cap_state_robot);

    build_score_block(card, DYN_R_SCORE, &STYLE_TEXT_ROBOT, &SCHEME_PILL_ZINC_800,
                      s_mult_robot);
    build_streak_block(card, DYN_R_STREAK, &s_bar_streak_robot, 0x22D3EEu);   /* cyan-400 */

    add_rule(card, COL_X, R_RULE1_Y, COL_W);

    /* FRET ACTIVITY — a status display, not an input: toggleable so the pressed state
     * holds under program control (ScreenDashboard_ApplyFret), IGNOREEVENTS so operator
     * taps don't fight the live mask. Each fret scheme carries its idle colour in BASE
     * and its lit colour in BACKGROUND, which is the mockup's colour-900/40 → colour-500
     * pair. */
    (void)add_cap(card, COL_X, R_FRET_Y, COL_W, 16, stringID_PLAYER_ROBOT_FRET_ACTIVITY,
            &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT);
    for (unsigned i = 0u; i < 5u; i++)
    {
        s_fret[i] = add_button(card, COL_X + (int)i * FRET_PITCH, R_FRETS_Y,
                               FRET_W, FRET_H, 0u, FRET[i].scheme);
        s_fret[i]->fn->setToggleable(s_fret[i], LE_TRUE);
        s_fret[i]->widget.flags |= LE_WIDGET_IGNOREEVENTS;
    }

    (void)add_cap(card, COL_X, R_STRUM_Y, 96, 16, stringID_PLAYER_STRUM_BAR,
            &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT);
    s_strum_pill = add_pill(card, 198, R_STRUM_Y - 2, 45, STATE_H, &SCHEME_FILL_ZINC_800);
    dual_init(&s_cap_strum,
              add_cap(s_strum_pill, 0, 2, 45, 16, stringID_PLAYER_STRUM_BAR_Status,
                      &SCHEME_TEXT_ZINC_600, LE_HALIGN_CENTER),
              stringID_PLAYER_STRUM_BAR_Status, stringID_PLAYER_STRUM_ACTIVE);

    add_rule(card, COL_X, R_RULE2_Y, COL_W);

    (void)add_cap(card, COL_X, R_DET_Y, COL_W, 16, stringID_PLAYER_ROBOT_DETECTOR,
            &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT);
    s_detector[0] = add_button(card, COL_X, R_DET1_Y, COL_W, OPT_H,
                               stringID_PLAYER_ROBOT_Computer_Vision, &SCHEME_TOGGLE_OFF);
    s_detector[1] = add_button(card, COL_X, R_DET2_Y, COL_W, OPT_H,
                               stringID_PLAYER_ROBOT_Neural_Network, &SCHEME_TOGGLE_OFF);
    for (unsigned i = 0u; i < 2u; i++)
    {
        s_detector[i]->fn->setReleasedEventCallback(s_detector[i], detector_on_release);
    }

    add_rule(card, COL_X, R_RULE3_Y, COL_W);

    /* ACTUATORS — one enable toggle per actuator node, in the mockup's 2-column grid
     * (three buttons, so the last one sits alone on the second row). Each is
     * label-left + state dot-right; the dot is a sibling drawn over the button and
     * marked IGNOREPICK, because a Legato button paints its own caption and cannot host
     * children. Not wired to the nodes yet — the toggle latches its look only. */
    (void)add_cap(card, COL_X, R_ACT_Y, COL_W, 16, stringID_PLAYER_ROBOT_ACTUATORS,
            &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT);

    static const uint32_t ACT_ID[ACTUATOR_COUNT] = {
        stringID_ACTUATOR_GUITAR, stringID_ACTUATOR_LEMMY, stringID_ACTUATOR_LIGHTSHOW,
    };
    for (unsigned i = 0u; i < ACTUATOR_COUNT; i++)
    {
        int x = COL_X + (int)(i % 2u) * (ACT_W + ACT_GAP);
        int y = R_ACT_ROW_Y + (int)(i / 2u) * (OPT_H + ACT_GAP);

        s_actuator[i] = add_button(card, x, y, ACT_W, OPT_H, ACT_ID[i], &SCHEME_TOGGLE_OFF);
        s_actuator[i]->fn->setHAlignment(s_actuator[i], LE_HALIGN_LEFT);
        s_actuator[i]->fn->setMargins(s_actuator[i], PAD, 0, 0, 0);
        s_actuator[i]->fn->setReleasedEventCallback(s_actuator[i], actuator_on_release);

        s_actuator_led[i] = add_dot(card, x + ACT_W - PAD - 8, y + (OPT_H - 8) / 2, 8,
                                    &SCHEME_FILL_ZINC_600);
        s_actuator_led[i]->flags |= LE_WIDGET_IGNOREPICK;
    }

    add_card_frame(content, 0, 0, SIDE_W, CONTENT_H, CARD_R);
}

/* ── build: human card ──────────────────────────────────────────────────────*/

static void build_human_card(leWidget *content)
{
    leWidget *card = add_card(content, HUMAN_X, 0, SIDE_W, CONTENT_H);

    build_player_head(card, &PLAYER_HUMAN_ART, 0u, DYN_H_NAME, &SCHEME_TEXT_HUMAN,
                      stringID_PLAYER_HUMAN_HumanPlayer,
                      &s_state_human, &s_state_human_led, &s_cap_state_human);

    /* The name is display-only; it is entered on the keyboard START raises for a
     * 2-player match (player_name_committed). */
    build_score_block(card, DYN_H_SCORE, &SCHEME_TEXT_HUMAN, &SCHEME_PILL_HUMAN,
                      s_mult_human);
    build_streak_block(card, DYN_H_STREAK, &s_bar_streak_human, 0xFDC700u);

    add_card_frame(content, HUMAN_X, 0, SIDE_W, CONTENT_H, CARD_R);
}

/* ── build: centre column (video + song card) ───────────────────────────────*/

static void build_video(leWidget *content)
{
    /* SMPTE colour bars: seven full-height bars, a middle row, then a black→white ramp —
     * what the panel shows wherever the live video (HEO, above this layer) is not
     * covering. The bar schemes are the standard 75% level (191, matching the mockup's
     * canvas), so anything here that should read as full intensity — the crosshair, the
     * NO SIGNAL dot — uses its own scheme rather than a bar colour. Bar widths alternate
     * 103/102 to sum to the card's inner width. */
    static const leScheme *TOP[7] = {
        &SCHEME_TEST_PATTERN_WHITE, &SCHEME_TEST_PATTERN_YELLOW, &SCHEME_TEST_PATTERN_CYAN,
        &SCHEME_TEST_PATTERN_GREEN, &SCHEME_TEST_PATTERN_MAGENTA, &SCHEME_TEST_PATTERN_RED,
        &SCHEME_TEST_PATTERN_BLUE,
    };
    static const leScheme *MID[7] = {
        &SCHEME_TEST_PATTERN_BLUE, &SCHEME_TEST_PATTERN_DARK, &SCHEME_TEST_PATTERN_MAGENTA,
        &SCHEME_TEST_PATTERN_DARK, &SCHEME_TEST_PATTERN_CYAN, &SCHEME_TEST_PATTERN_DARK,
        &SCHEME_TEST_PATTERN_WHITE,
    };

    leWidget *video = add_panel(content, CENTER_X, 0, VIDEO_W, VIDEO_H,
                                &SCHEME_BACKGROUND, LE_TRUE);

    /* Everything below goes in one transparent child so the whole pattern hides with a
     * single flag: HEO covers this rect only while the video is actually bound, and the
     * gap after a rebind (leaving a full-screen view, source re-locking) would otherwise
     * flash the bars. `video`'s own fill stays as the backdrop for that gap.
     *
     * Built HIDDEN: the compositor turns it on only after the video has been absent for
     * a moment, so neither the boot reveal nor a view switch ever flashes the bars. */
    s_video_pattern = add_panel(video, 0, 0, VIDEO_W, VIDEO_H, NULL, LE_FALSE);
    s_video_pattern->fn->setVisible(s_video_pattern, LE_FALSE);
    video = s_video_pattern;

    int x = 1;
    for (unsigned i = 0u; i < 7u; i++)
    {
        int w = (i % 2u == 0u) ? 103 : 102;
        (void)add_panel(video, x, 1, w, BAR_TOP_H, TOP[i], LE_TRUE);
        (void)add_panel(video, x, 1 + BAR_TOP_H, w, BAR_MID_H, MID[i], LE_TRUE);
        x += w;
    }

    /* Black→white ramp: a bar filled to 100% with a dithered gradient. Legato's
     * gradient widget is compiled out whenever the design stops using one, and it
     * would band badly in RGB565 regardless — see widget_bar.h / ui/gfx/gradient.h. */
    leWidget *ramp = add_bar(video, 1, 1 + BAR_TOP_H + BAR_MID_H, VIDEO_W - 2,
                             VIDEO_H - 2 - BAR_TOP_H - BAR_MID_H, 0u,
                             0x000000u, 0xFFFFFFu);
    Bar_SetPermille(ramp, 1000u);

    /* PLEASE STAND BY slate: white text on a black box over the ramp, centred on the
     * mockup's baseline (VIDEO_H - 65). The mockup sets bold 32px; the design carries
     * bold 24 and 40, so this uses 40 — already in the image for the on-screen keyboard,
     * so it costs no flash, at the price of a slate ~25% larger than the mockup's. The
     * box hugs the text: STAND_BY_W is the measured advance sum for that font. */
    (void)add_panel(video, (VIDEO_W - STAND_BY_BOX_W) / 2, STAND_BY_Y,
                    STAND_BY_BOX_W, STAND_BY_BOX_H, &SCHEME_BACKGROUND, LE_TRUE);
    (void)add_cap(video, (VIDEO_W - STAND_BY_BOX_W) / 2, STAND_BY_Y - STAND_BY_INK_LIFT,
                  STAND_BY_BOX_W, STAND_BY_BOX_H, stringID_VIDEO_STAND_BY,
                  &SCHEME_TEXT_WHITE, LE_HALIGN_CENTER);

    /* Centre crosshair — full white, unlike the 75% bars around it. */
    (void)add_panel(video, VIDEO_W / 2 - 40, VIDEO_H / 2 - 1, 80, 3,
                    &SCHEME_FILL_WHITE, LE_TRUE);
    (void)add_panel(video, VIDEO_W / 2 - 1, VIDEO_H / 2 - 40, 3, 80,
                    &SCHEME_FILL_WHITE, LE_TRUE);

    /* NO SIGNAL chip, top right. (The mockup's TEST PATTERN chip on the left is
     * deliberately not built — the bars say that already.) */
    leWidget *ns = add_pill(video, VIDEO_W - 10 - 98, 10, 98, PILL_H, &SCHEME_BACKGROUND);
    (void)add_dot(ns, 8, 8, 9, &SCHEME_FILL_RED_500);
    (void)add_cap(ns, 24, 4, 66, 16, stringID_VIDEO_NO_SIGNAL, &SCHEME_TEXT_WHITE,
            LE_HALIGN_LEFT);

    add_card_frame(content, CENTER_X, 0, VIDEO_W, VIDEO_H, CARD_R);
}

static void build_song_card(leWidget *content)
{
    leWidget *card = add_card(content, CENTER_X, SONG_Y, CENTER_W, SONG_H);

    /* Album art, rounded by an overlay whose BASE is the card fill (the documented
     * round-image case: the art is inside the card, so its corners are eaten to
     * zinc-900, not to the page black). */
    s_album = add_image(card, ART_XY, ART_XY, ALBUM_D, ALBUM_D, NULL);
    leWidget *art_frame = add_panel(card, ART_XY, ART_XY, ALBUM_D, ALBUM_D,
                                    &SCHEME_FILL_ZINC_900, LE_FALSE);
    art_frame->fn->setCornerRadius(art_frame, ALBUM_R);
    art_frame->flags |= LE_WIDGET_IGNOREPICK;   /* corner-cut overlay, never a target */
    PanelAA_EnableRoundImage(art_frame);

    (void)add_dyn(card, DYN_S_STATUS, INFO_X, 16, INFO_W, 16,
                  (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT);
    (void)add_dyn(card, DYN_S_TITLE, INFO_X, 34, INFO_W, 25,
                  (const leFont *)&DejaVuSansMonoBold_20, &SCHEME_TEXT_WHITE, LE_HALIGN_LEFT);
    (void)add_dyn(card, DYN_S_ARTIST, INFO_X, 59, INFO_W, 20,
                  (const leFont *)&DejaVuSansMono_14, &SCHEME_TEXT_ZINC_400, LE_HALIGN_LEFT);
    (void)add_dyn(card, DYN_S_ALBUM, INFO_X, 79, INFO_W, 16,
                  (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_600, LE_HALIGN_LEFT);

    /* Metric row: GENRE · DURATION · TIER. The mockup also lists BPM, dropped because the
     * catalog's column isn't populated in practice (SONG_INFO_BPM is kept, unused).
     *
     * The fields tile the info column edge to edge — a label clips its text at its own
     * right edge, so a box narrower than its content silently truncates while a generous
     * one costs nothing (transparent background, and the only real hazard is overlapping
     * a neighbour, since siblings paint in order rather than clipping each other). Widths
     * are therefore allocated by worst-case content: DURATION needs its 8-character
     * heading, TIER eight stars, and GENRE — the one that actually overflows, on
     * "Progressive Rock" and friends — takes everything left over, which is what BPM's
     * 50px went to. */
    static const struct { int x, w; uint32_t cap; unsigned dyn; } METRIC[3] = {
        { INFO_X,       180, stringID_SONG_INFO_GENRE,    DYN_S_GENRE    },
        { INFO_X + 180,  80, stringID_SONG_INFO_DURATION, DYN_S_DURATION },
        { INFO_X + 260,  90, stringID_SONG_INFO_TIER,     DYN_S_TIER     },
    };
    for (unsigned i = 0u; i < (sizeof METRIC / sizeof METRIC[0]); i++)
    {
        (void)add_cap(card, METRIC[i].x, 121, METRIC[i].w, 16, METRIC[i].cap,
                &SCHEME_TEXT_ZINC_600, LE_HALIGN_LEFT);
        /* The tier value is stars (U+2605) — DejaVuSansMonoBold_12 carries the glyph
         * and gives them more presence than the plain face. */
        (void)add_dyn(card, METRIC[i].dyn, METRIC[i].x, 137, METRIC[i].w, 16,
                      (METRIC[i].dyn == DYN_S_TIER) ? (const leFont *)&DejaVuSansMonoBold_12
                                                    : (const leFont *)&DejaVuSansMono_12,
                      &SCHEME_TEXT_ZINC_300, LE_HALIGN_LEFT);
    }

    /* Playtime: elapsed / total markers over the mockup's gradient bar. */
    (void)add_dyn(card, DYN_S_ELAPSED, INFO_X, 178, 40, 16,
                  (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT);
    (void)add_dyn(card, DYN_S_TOTAL, INFO_X + INFO_W - 40, 178, 40, 16,
                  (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500, LE_HALIGN_RIGHT);
    /* The mockup's `from-green-600 to-green-300` ramp. */
    s_bar_play = add_bar(card, INFO_X, 199, INFO_W, 8, 4u, 0x00A63Au, 0x7BF1A8u);

    (void)add_panel(card, SPLIT_X, 1, 1, SONG_H - 2, &SCHEME_PILL_ZINC_700, LE_TRUE);

    /* Right column: mode + difficulty, then the two controls, which fill the rest of
     * the card (the mockup's flex-1 buttons — much bigger touch targets than the
     * 36px the imported design had). */
    (void)add_cap(card, GAME_X + 16, 16, 40, 16, stringID_SONG_GAMEPLAY_MODE,
            &SCHEME_TEXT_ZINC_600, LE_HALIGN_LEFT);
    (void)add_dyn(card, DYN_S_MODE, GAME_X + 60, 16, GAME_W - 76, 16,
                  (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_300, LE_HALIGN_RIGHT);

    (void)add_cap(card, GAME_X + 16, 40, 40, 16, stringID_SONG_GAMEPLAY_DIFF,
            &SCHEME_TEXT_ZINC_600, LE_HALIGN_LEFT);
    s_diff_pill = add_pill(card, GAME_X + 104, 40, 56, 20, &SCHEME_BUTTON_EASY);
    (void)add_dyn(s_diff_pill, DYN_S_DIFF, 0, 2, 56, 16,
                  (const leFont *)&DejaVuSansMonoBold_12, &SCHEME_TEXT_RED_200,
                  LE_HALIGN_CENTER);

    (void)add_panel(card, GAME_X + 16, 70, GAME_W - 32, 1, &SCHEME_PILL_ZINC_700, LE_TRUE);

    leButtonWidget *pick = add_button(card, GAME_X + 16, 79, GAME_W - 32, 60,
                                      stringID_GAMEPLAY_SELECT_SONG, &SCHEME_BUTTON_MODE);
    pick->fn->setPressedImage(pick, (leImage *)&BUTTON_FACE_SELECT_SONG);
    pick->fn->setReleasedImage(pick, (leImage *)&BUTTON_FACE_SELECT_SONG);
    pick->fn->setImageMargin(pick, 6);
    pick->fn->setReleasedEventCallback(pick, select_song_on_release);

    s_start = add_button(card, GAME_X + 16, 147, GAME_W - 32, 60,
                         stringID_GAMEPLAY_START, &SCHEME_GUITAR_FRET_GREEN);
    s_start->fn->setPressedImage(s_start, (leImage *)&BUTTON_FACE_START);
    s_start->fn->setReleasedImage(s_start, (leImage *)&BUTTON_FACE_START);
    s_start->fn->setImageMargin(s_start, 6);
    s_start->fn->setReleasedEventCallback(s_start, start_on_release);
    leTableString_Constructor(&s_start_cap, stringID_GAMEPLAY_START);
    leTableString_Constructor(&s_stop_cap,  stringID_GAMEPLAY_STOP);

    add_card_frame(content, CENTER_X, SONG_Y, CENTER_W, SONG_H, CARD_R);
}

/* ── live data: the Apply* entry points (called only by the feed consumer) ───*/

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

/* Selected play difficulty → pill fill (reuses the song-select button schemes so the
 * pill matches the dialog's selected difficulty). */
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

/* Career-tier stars + colour, or BONUS for a song with no tier. */
static void tier_show(const game_catalog_entry_t *e)
{
    leLabelWidget *lbl = &s_dyn_lbl[DYN_S_TIER];
    char           txt[40];
    int            tier = (e != NULL) ? SongDetail_Tier(e->difficulty) : 0;

    if (e == NULL) { set_dyn(DYN_S_TIER, "-"); }
    else
    {
        SongDetail_TierText(tier, txt, sizeof txt);
        set_dyn(DYN_S_TIER, txt);
    }
    lbl->fn->setScheme(lbl, SongDetail_TierScheme(tier));
}

/* Mirror the committed selection onto the song card: artwork + metadata from the
 * catalog, tier from the song, mode + difficulty from the selection. */
void ScreenDashboard_ApplySelection(void)
{
    const game_selection_t *sel = GameSelection_Get();
    game_catalog_entry_t    e;
    bool                    ok = GameCatalog_Lookup(sel->setlist, sel->index, &e);
    char                    tmp[96];

    s_album->fn->setImage(s_album, (leImage *)GameArt_Small(sel->setlist, sel->index));

    if (ok)
    {
        set_dyn(DYN_S_TITLE,  e.title);
        set_dyn(DYN_S_ARTIST, e.artist);
        set_dyn(DYN_S_GENRE,  e.genre);

        /* Album + release year on one line (the card has no separate year label). */
        if (e.album[0] != '\0' && e.year != 0u)
        {
            (void)snprintf(tmp, sizeof tmp, "%s - %u", e.album, (unsigned)e.year);
        }
        else if (e.album[0] != '\0') { (void)snprintf(tmp, sizeof tmp, "%s", e.album); }
        else if (e.year != 0u)       { (void)snprintf(tmp, sizeof tmp, "%u", (unsigned)e.year); }
        else                         { tmp[0] = '\0'; }
        set_dyn(DYN_S_ALBUM, tmp);

        SongDetail_Duration(e.length_s, tmp, sizeof tmp);
        set_dyn(DYN_S_DURATION, tmp);
        set_dyn(DYN_S_TOTAL, tmp);
        s_song_len_s = e.length_s;

        tier_show(&e);
    }
    else
    {
        set_dyn(DYN_S_TITLE,    "-");
        set_dyn(DYN_S_ARTIST,   "-");
        set_dyn(DYN_S_ALBUM,    "-");
        set_dyn(DYN_S_GENRE,    "-");
        set_dyn(DYN_S_DURATION, "-");
        set_dyn(DYN_S_TOTAL,    "-");
        s_song_len_s = 0u;
        tier_show(NULL);
    }

    /* A fresh selection resets the playtime bar and its elapsed marker to 0. */
    set_dyn(DYN_S_ELAPSED, "0:00");
    Bar_SetPermille(s_bar_play, 0u);

    set_dyn(DYN_S_MODE, mode_text(sel->mode));
    set_dyn(DYN_S_DIFF, difficulty_text(sel->difficulty));
    s_diff_pill->fn->setScheme(s_diff_pill, difficulty_scheme(sel->difficulty));
    s_diff_pill->fn->invalidate(s_diff_pill);
}

/* Selection observer — runs in the committing task's context (touch / song-select).
 * Never touches widgets; just enqueues so the feed consumer applies it as the single
 * dashboard writer. */
static void dash_selection_changed(const game_selection_t *sel)
{
    (void)sel;
    DashboardFeed_PostSelection();
}

/* Game-controller status observer — runs in the game-controller task's context.
 * Never touches widgets; just enqueues. */
static void dash_game_status(const char *text)
{
    DashboardFeed_PostStatus(text);
}

/* Reflect the live guitar mask on the ROBOT frets (pressed = fret held) and the STRUM
 * BAR chip. Only the buttons whose state changed are touched, so a steady chord costs
 * nothing. The buttons are toggleable so setPressed latches the state (a non-toggleable
 * button treats setPressed(TRUE) as a click and stays UP), and we invalidate explicitly
 * — setPressed only self-invalidates for image/bevel/offset buttons, not for a plain
 * scheme-filled one, so the UP↔TOGGLED colour swap wouldn't otherwise repaint. */
void ScreenDashboard_ApplyFret(uint8_t mask)
{
    static const uint8_t BIT[5] = {
        GUITAR_BTN_GREEN, GUITAR_BTN_RED, GUITAR_BTN_YELLOW,
        GUITAR_BTN_BLUE,  GUITAR_BTN_ORANGE,
    };
    static uint8_t s_last_mask;
    static bool    s_last_strum;

    uint8_t changed = (uint8_t)(mask ^ s_last_mask);
    if (changed == 0u) { return; }
    s_last_mask = mask;

    for (unsigned i = 0; i < 5u; i++)
    {
        if (changed & BIT[i])
        {
            leButtonWidget *b = s_fret[i];
            b->fn->setPressed(b, (mask & BIT[i]) ? LE_TRUE : LE_FALSE);
            b->fn->invalidate(b);
        }
    }

    bool strum = (mask & (GUITAR_BTN_STRUM_UP | GUITAR_BTN_STRUM_DOWN)) != 0u;
    if (strum != s_last_strum)
    {
        s_last_strum = strum;
        leLabelWidget *l = s_cap_strum.lbl;
        s_strum_pill->fn->setScheme(s_strum_pill,
                                    strum ? &SCHEME_PILL_ZINC_800 : &SCHEME_FILL_ZINC_800);
        l->fn->setScheme(l, strum ? &SCHEME_TEXT_WHITE : &SCHEME_TEXT_ZINC_600);
        dual_set(&s_cap_strum, strum);
        s_strum_pill->fn->invalidate(s_strum_pill);
    }
}

/* CV-read score multiplier (1..4) → highlight the active ROBOT {1,2,3,4}X pill,
 * clearing the other three (same toggle/latch mechanism as ApplyFret). */
void ScreenDashboard_ApplyMultiplier(uint8_t mult)
{
    if (mult < 1u || mult > 4u) { return; }
    static uint8_t s_last_mult;
    if (mult == s_last_mult) { return; }
    s_last_mult = mult;

    for (uint8_t i = 0; i < 4u; i++)
    {
        leButtonWidget *b = s_mult_robot[i];
        b->fn->setPressed(b, (i < mult) ? LE_TRUE : LE_FALSE);
        b->fn->invalidate(b);
    }
}

/* Run-dependent chrome: the two state chips and the START/STOP control. Driven from
 * the status stream, which fires on every controller phase change. */
static void run_state_show(bool active)
{
    const game_selection_t *sel = GameSelection_Get();
    bool human_active = active && sel->valid && sel->mode == GAME_MODE_2P;

    s_run_active = active;

    s_state_robot->fn->setScheme(s_state_robot,
                                 active ? &SCHEME_FILL_GREEN_800 : &SCHEME_FILL_ZINC_800);
    s_state_robot_led->fn->setScheme(s_state_robot_led,
                                     active ? &SCHEME_FILL_GREEN_400 : &SCHEME_FILL_ZINC_600);
    s_cap_state_robot.lbl->fn->setScheme(s_cap_state_robot.lbl,
                                         active ? &SCHEME_TEXT_GREEN_300 : &SCHEME_TEXT_ZINC_500);
    dual_set(&s_cap_state_robot, active);
    s_state_robot->fn->invalidate(s_state_robot);

    s_state_human->fn->setScheme(s_state_human,
                                 human_active ? &SCHEME_FILL_GREEN_800 : &SCHEME_FILL_ZINC_800);
    s_state_human_led->fn->setScheme(s_state_human_led,
                                     human_active ? &SCHEME_FILL_GREEN_400 : &SCHEME_FILL_ZINC_600);
    s_cap_state_human.lbl->fn->setScheme(s_cap_state_human.lbl,
                                         human_active ? &SCHEME_TEXT_GREEN_300 : &SCHEME_TEXT_ZINC_500);
    dual_set(&s_cap_state_human, human_active);
    s_state_human->fn->invalidate(s_state_human);

    s_start->fn->setString(s_start, (leString *)(active ? &s_stop_cap : &s_start_cap));
    s_start->fn->setScheme(s_start, active ? &SCHEME_BUTTON_EXPERT : &SCHEME_GUITAR_FRET_GREEN);
    s_start->fn->setPressedImage(s_start, active ? NULL : (leImage *)&BUTTON_FACE_START);
    s_start->fn->setReleasedImage(s_start, active ? NULL : (leImage *)&BUTTON_FACE_START);
    s_start->fn->invalidate(s_start);
}

/* Game-controller status → the song card's status line, plus the run-dependent chrome. */
void ScreenDashboard_ApplyStatus(const char *text)
{
    set_dyn(DYN_S_STATUS, (text != NULL && text[0] != '\0') ? text : "READY");

    bool active = GameController_IsBusy();
    if (active != s_run_active) { run_state_show(active); }
}

/* CV-read GH3 score → the ROBOT card's score. */
void ScreenDashboard_ApplyScore(uint32_t score)
{
    char tmp[12];
    (void)snprintf(tmp, sizeof tmp, "%lu", (unsigned long)score);
    set_dyn(DYN_R_SCORE, tmp);
}

/* CV-read GH3 note streak → the ROBOT card's streak value + bar. 0 means the odometer
 * isn't shown yet (streak < ~25) or the run reset; shown as "0" (not blank) so the
 * label repaints — setting an empty string does not clear the prior glyphs. */
void ScreenDashboard_ApplyStreak(uint16_t streak)
{
    /* Bar scale: the mockup's streak/99. A longer streak simply pins the bar full. */
    #define STREAK_FULL  99u

    char tmp[8];
    (void)snprintf(tmp, sizeof tmp, "%u", (unsigned)streak);
    set_dyn(DYN_R_STREAK, tmp);

    uint32_t permille = (streak >= STREAK_FULL) ? 1000u
                                                : ((uint32_t)streak * 1000u) / STREAK_FULL;
    Bar_SetPermille(s_bar_streak_robot, permille);
}

/* Elapsed play time → the left marker (m:ss, counting up) and the bar fill, both
 * scaled/clamped against the selected song's duration. The bar is driven at pixel
 * resolution (permille) so it advances by single pixels. Unknown duration leaves the
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

    char elapsed[12];
    unsigned s = (unsigned)(shown_ms / 1000u);
    (void)snprintf(elapsed, sizeof elapsed, "%u:%02u", s / 60u, s % 60u);
    set_dyn(DYN_S_ELAPSED, elapsed);

    Bar_SetPermille(s_bar_play, permille);
}

/* Drop the SMPTE test pattern while the live video is actually on the panel. The pattern
 * lives *under* HEO, so it is only ever meant to be seen when there is nothing over it —
 * but HEO unbinds whenever a full-screen view takes the panel, and rebinding it costs a
 * video-task tick plus a source re-lock, which was long enough to flash the bars on the
 * way back to the dashboard. Hiding the group means that gap shows the video card's own
 * dark fill instead. The NO SIGNAL / PLEASE STAND BY slate goes with it, which is right:
 * it is part of the same "nothing is coming through" story.
 *
 * Called from the feed task with the render lock held. */
void ScreenDashboard_ApplyVideoState(bool displayed)
{
    if (s_video_pattern == NULL) { return; }

    leBool want = displayed ? LE_FALSE : LE_TRUE;
    if (s_video_pattern->fn->getVisible(s_video_pattern) == want) { return; }

    s_video_pattern->fn->setVisible(s_video_pattern, want);
}

/* ── lifecycle ──────────────────────────────────────────────────────────────*/

/* The dashboard has no periodic work of its own — every repaint comes from a feed
 * apply — so being "not shown" is entirely a property of the writer, and the gate lives
 * there. This is the name the compositor uses for the other screens. */
void ScreenDashboard_SetShown(bool shown)
{
    DashboardFeed_SetShown(shown);
}

void ScreenDashboard_InitSurface(void)
{
    UiSurface_Set(CANVAS_DASH, BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565, s_fb);
}

void ScreenDashboard_Setup(void)
{
    /* Full-screen at the origin. The root is on Legato layer 0; the canvas window
     * positions that layer's pixels on the display. */
    gfxcSetWindowPosition(CANVAS_DASH, 0, 0);
    gfxcSetWindowSize(CANVAS_DASH, BASE_W, BASE_H);

    /* Shared titlebar (hamburger + logos + link dot); the handle lets screen_video gate
     * the chrome out of picking while the video is fullscreen. */
    s_titlebar = Titlebar_Add(Marvin_PANEL_DASHBOARD);

    /* One parent for the three columns, so screen_video can gate every interactive
     * dashboard widget with a single flag. */
    s_content = add_panel(Marvin_PANEL_DASHBOARD, MARGIN, CONTENT_Y, CONTENT_W, CONTENT_H,
                          NULL, LE_FALSE);

    build_robot_card(s_content);
    build_video(s_content);
    build_song_card(s_content);
    build_human_card(s_content);

    /* Seed everything the feed would otherwise leave as uninitialized surface. */
    set_dyn(DYN_R_SCORE,  "0");
    set_dyn(DYN_R_STREAK, "0");
    set_dyn(DYN_H_NAME,   Results_GetPlayer());
    set_dyn(DYN_H_SCORE,  "0");
    set_dyn(DYN_H_STREAK, "0");
    set_dyn(DYN_S_STATUS, "READY");
    run_state_show(false);
    detector_show_active();

    /* Mirror the committed gameplay selection onto the song card. Register the observer
     * before song-select's Setup seeds the boot default (ui_manager calls this screen's
     * Setup first), so that first commit lands here. */
    GameSelection_SetObserver(dash_selection_changed);
    if (GameSelection_Get()->valid) { ScreenDashboard_ApplySelection(); }

    GameController_SetStatusObserver(dash_game_status);
}

leWidget *ScreenDashboard_Titlebar(void)
{
    return s_titlebar;
}

leWidget *ScreenDashboard_Content(void)
{
    return s_content;
}
