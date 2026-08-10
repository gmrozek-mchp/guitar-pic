#include "ui/screens/wiimotes/screen_wiimotes.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "log.h"

#include "ui/ui_manager.h"   /* CANVAS_WIIMOTES, BASE_W, BASE_H */
#include "ui/titlebar.h"     /* shared hamburger + logos titlebar */
#include "ui/gfx/video_frame.h"
#include "ui/widgets/panel_aa/widget_panel_aa.h"
#include "ui/widgets/button_aa/widget_button_aa.h"
#include "ui/widgets/fret/widget_fret.h"
#include "ui/widgets/whammy/widget_whammy.h"
#include "ui/widgets/tilt/widget_tilt.h"
#include "ui/widgets/slide_unlock/widget_slide_unlock.h"

#include "ui/gfx/ui_surface.h"
#include "ui/gfx/render_probe.h"
#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/string/legato_tablestring.h"
#include "gfx/legato/widget/legato_widget.h"
#include "gfx/legato/widget/button/legato_widget_button.h"
#include "gfx/legato/widget/label/legato_widget_label.h"
#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"                  /* stringID_*, fonts */
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* Marvin_PANEL_WIIMOTES */

#include "net/fauxmote/fauxmote_link.h"
#include "net/fauxmote/fauxmote_pointer.h"
#include "net/fauxmote/mf_proto.h"

/* Manual-override screen, built programmatically into the empty MGS layer-4 panel
 * (Marvin_PANEL_WIIMOTES) — the screen_bus.c model. Ports tools' mockup
 * ManualOverrideScreen.tsx with Tailwind units resolved to pixels: live video
 * centered up top with the link column down its right, then the guitar-extension and
 * wiimote cards side by side.
 *
 * Text comes from the DESIGN string table (leTableString + stringID_*), not C
 * literals: five of these labels are non-ASCII (the d-pad arrows and the true minus
 * U+2212), and MGS only auto-includes glyphs for strings it can see in the design.
 * A C literal would silently render blanks after the next Generate.
 *
 * The controls are gated behind a slide-to-unlock scrim over the two cards. Showing the
 * screen no longer takes the fauxmote override — the unlock does, and leaving the screen
 * releases it and re-locks; see ScreenWiimotes_SetShown. */

/* Wiimotes surface, non-cached so the 2D engine and LCDC DMA read CPU-rendered
 * pixels coherently; 32-byte aligned. RGB565 to match the layer's color mode. */
#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

static uint16_t FB_NOCACHE s_fb[BASE_W * BASE_H];

/* ── layout ─────────────────────────────────────────────────────────────────
 * Two equal cards on the bottom row, the video centered in what is left above. */
#define MARGIN       16
#define GAP          12
#define TITLEBAR_H   65
#define PAD          12                                  /* card p-3 */
#define CARD_R        8                                  /* rounded-lg */

#define CARD_H      272
#define CARD_Y      (BASE_H - MARGIN - CARD_H)           /* 512 */
#define CARD_W      ((BASE_W - 2 * MARGIN - GAP) / 2)    /* 618 */
#define GX          MARGIN                               /* guitar card x  */
#define WX          (GX + CARD_W + GAP)                  /* wiimote card x */

/* Live video (HEO) + its frame overlay (OVR1), centered above the cards. */
#define VID_W       630
#define VID_H       420
#define VID_X       ((BASE_W - VID_W) / 2)                            /* 325 */
#define VID_Y       (TITLEBAR_H + ((CARD_Y - TITLEBAR_H) - VID_H) / 2) /* 78 */

/* Mockup: border-2 border-zinc-700 rounded-sm (Tailwind v4 -> 4px radius). */
#define VID_FRAME_R       4.0f
#define VID_FRAME_STROKE  2.0f
#define VID_FRAME_C4      4u        /* 4-bit grey -> #444444 ~ zinc-700 */

static uint16_t FB_NOCACHE s_frame_fb[VID_W * VID_H];

/* Link column: five action keys stacked down the right of the video row — the mockup's
 * `w-20` gutter holding 72px `rounded-xl` keys on a gap-3 stack, centered in the same
 * vertical band as the video. Manage the Wii link rather than drive the controller, so
 * they sit outside the lock gate's scrim (which spans the card row only). */
#define LNK_N          5
#define LNK_W         72
#define LNK_GAP       12
#define LNK_R         12                                 /* rounded-xl */
#define LNK_COL_W     80
#define LNK_X         (BASE_W - MARGIN - LNK_COL_W + (LNK_COL_W - LNK_W) / 2)  /* 1188 */
#define LNK_H         (LNK_N * LNK_W + (LNK_N - 1) * LNK_GAP)                  /*  408 */
#define LNK_Y         (TITLEBAR_H + ((CARD_Y - TITLEBAR_H) - LNK_H) / 2)       /*   84 */
#define LNK_ICON_GAP   8                                 /* gap-2 icon -> caption */

/* Guitar card interior. */
#define LBL_H        16
#define FRET_Y       (CARD_Y + 38)
#define FRET_H       64
#define FRET_GAP      8
#define FRET_W       ((CARD_W - 2 * PAD - 4 * FRET_GAP) / 5)   /* 112 */

#define ROW_Y        (CARD_Y + 112)      /* strum / whammy / +- row */
#define ROW_H        148
#define BTN_H         71
#define ROW_Y2       (ROW_Y + BTN_H + 6)

#define STRUM_W      180
#define STRUM_X      (GX + PAD)
#define AUX_W         71
#define AUX_X        (GX + CARD_W - PAD - AUX_W)

#define WHAM_W       273
#define WHAM_H        64
#define WHAM_X       (STRUM_X + STRUM_W + ((AUX_X - (STRUM_X + STRUM_W)) - WHAM_W) / 2)
#define WHAM_Y       (ROW_Y + (ROW_H - WHAM_H) / 2)
#define WHAM_LBL_Y   (ROW_Y + 23)

/* Wiimote card interior — the mockup's grid, which matches the sizes the figma
 * import already used (71px keys on a 6px gap). */
#define KEY           71
#define KEY_GAP        6
#define DPAD_X       (WX + PAD)
#define DPAD_Y       (CARD_Y + 35)
#define ACT_X        (DPAD_X + 3 * KEY + 2 * KEY_GAP + 16)   /* WX + 253 */
#define HOME_W       150
#define TILT_XY      157
#define TILT_X       (ACT_X + HOME_W + 16)                   /* WX + 419 */
#define TILT_Y       (CARD_Y + 68)

/* Lock scrim: the mockup's `absolute inset-0` on the card row, so it spans both cards and
 * the gap between them, and dims at bg-black/60. The heading + slider are centred in it as
 * a gap-4 column (20 + 16 + 64 = 100 tall). */
#define SCRIM_X      GX
#define SCRIM_Y      CARD_Y
#define SCRIM_W      (BASE_W - 2 * MARGIN)               /* 1248 */
#define SCRIM_H      CARD_H
#define SCRIM_ALPHA  153u                               /* 60% of 255 */

#define GATE_COL_H   100
#define GATE_LBL_H    20
#define GATE_LBL_W   400
#define GATE_LBL_Y   (SCRIM_Y + (SCRIM_H - GATE_COL_H) / 2)          /* 598 */
#define GATE_LBL_X   (SCRIM_X + (SCRIM_W - GATE_LBL_W) / 2)

#define SLIDE_W      420
#define SLIDE_H       64
#define SLIDE_X      (SCRIM_X + (SCRIM_W - SLIDE_W) / 2)             /* 430 */
#define SLIDE_Y      (GATE_LBL_Y + GATE_LBL_H + 16)                  /* 634 */

/* Fret colours (Tailwind v4): rest = the -600/-500 shade at 75%, held = the
 * brighter -400/-300 shade with a ring of the same. */
static const struct { uint32_t idle, held; } FRET_COLOR[5] =
{
    { 0x00A63Eu, 0x05DF72u },   /* green  600 / 400 */
    { 0xE7000Bu, 0xFF6467u },   /* red    600 / 400 */
    { 0xEFB100u, 0xFFDF20u },   /* yellow 500 / 300 */
    { 0x155DFCu, 0x51A2FFu },   /* blue   600 / 400 */
    { 0xFF6900u, 0xFFB86Au },   /* orange 500 / 300 */
};

/* ── control state ──────────────────────────────────────────────────────────
 * Frets/strum/whammy/± form the GUITAR slice; d-pad + core buttons form the WIIMOTE
 * nav slice (stick centered); tilt is its own ACCEL slice. While this screen is
 * shown it owns fauxmote (override on), so the gameplay mirror can't fight it. */
static uint8_t s_g_mask;    /* MF_G_*   */
static uint8_t s_g_aux;     /* MF_AUX_* */
static uint8_t s_g_whammy = MF_WHAMMY_REST;
static uint8_t s_nav_core;  /* MF_W_A/B/ONE/TWO/HOME */
static uint8_t s_nav_dpad;  /* MF_W_UP/DOWN/LEFT/RIGHT */

/* Whether the gate is open. The screen sends nothing at all while locked: SendGuitar and
 * SendNav apply regardless of the override flag (only SendGuitarMask, the gameplay-mirror
 * hook, is gated), so a "safe defaults" push from a locked screen would still stomp on
 * whatever the mirror is driving. */
static bool s_unlocked;

/* ── widget pool ────────────────────────────────────────────────────────────
 * Static storage, constructed in place — no allocator (see the project's static
 * allocation rule). Sized to the built screen with a little slack. */
#define WGT_MAX   16u
#define BTN_MAX   20u
#define LBL_MAX    7u

static leWidget        s_wgt[WGT_MAX];
static unsigned        s_nwgt;
static leButtonWidget  s_btn[BTN_MAX];
static leTableString   s_btn_cap[BTN_MAX];
static unsigned        s_nbtn;
static leLabelWidget   s_lbl[LBL_MAX];
static leTableString   s_lbl_str[LBL_MAX];
static unsigned        s_nlbl;

/* Frets are addressed by index for the mask mapping; the buttons are looked up by
 * pointer in the dispatch table below. */
static leWidget *s_fret[5];
static leWidget *s_whammy;
static leWidget *s_tilt;
static leWidget *s_titlebar;

/* The link column, in LNK order (reconnect, swap, 1+2, unlink, pair). Held so the
 * actions can be bound to them; none is wired yet. */
static leButtonWidget *s_link[LNK_N];

/* The gate's three widgets, hidden together when it opens. */
static leWidget *s_scrim;
static leWidget *s_gate_lbl;
static leWidget *s_slide;

/* The slider's two captions. The heading is a plain label, so add_label owns its string. */
static leTableString s_slide_cap;
static leTableString s_slide_chev;

static leWidget *next_widget(int x, int y, int w, int h)
{
    configASSERT(s_nwgt < WGT_MAX);

    leWidget *p = &s_wgt[s_nwgt++];
    leWidget_Constructor(p);
    p->fn->setPosition(p, x, y);
    p->fn->setSize(p, w, h);
    p->fn->setBackgroundType(p, LE_WIDGET_BACKGROUND_NONE);
    p->fn->setBorderType(p, LE_WIDGET_BORDER_NONE);
    Marvin_PANEL_WIIMOTES->fn->addChild(Marvin_PANEL_WIIMOTES, p);
    return p;
}

/* A card: zinc-800/40 over the black backdrop reads as zinc-900; 1px zinc-700
 * border (SCHEME_FILL_ZINC_900's shadowDark is already ~zinc-700). */
static leWidget *add_card(int x, int y, int w, int h)
{
    configASSERT(s_nwgt < WGT_MAX);

    leWidget *p = &s_wgt[s_nwgt++];
    leWidget_Constructor(p);
    p->fn->setPosition(p, x, y);
    p->fn->setSize(p, w, h);
    p->fn->setScheme(p, &SCHEME_FILL_ZINC_900);
    p->fn->setBackgroundType(p, LE_WIDGET_BACKGROUND_FILL);
    p->fn->setBorderType(p, LE_WIDGET_BORDER_LINE);
    p->fn->setCornerRadius(p, CARD_R);
    PanelAA_Enable(p);
    Marvin_PANEL_WIIMOTES->fn->addChild(Marvin_PANEL_WIIMOTES, p);
    return p;
}

/* A design-string label. The string carries its own font via the design's binding,
 * which is the whole reason these are table strings (see the file header). */
static leLabelWidget *add_label(int x, int y, int w, int h, uint32_t string_id,
                               const leScheme *scheme, leHAlignment ha)
{
    configASSERT(s_nlbl < LBL_MAX);

    leLabelWidget *l = &s_lbl[s_nlbl];
    leLabelWidget_Constructor(l);
    l->fn->setPosition(l, x, y);
    l->fn->setSize(l, w, h);
    l->fn->setScheme(l, scheme);
    l->fn->setBackgroundType(l, LE_WIDGET_BACKGROUND_NONE);
    l->fn->setHAlignment(l, ha);
    l->fn->setVAlignment(l, LE_VALIGN_MIDDLE);

    leTableString_Constructor(&s_lbl_str[s_nlbl], string_id);
    l->fn->setString(l, (leString *)&s_lbl_str[s_nlbl]);
    s_nlbl++;

    Marvin_PANEL_WIIMOTES->fn->addChild(Marvin_PANEL_WIIMOTES, (leWidget *)l);
    return l;
}

static void control_on_press(leButtonWidget *btn);
static void control_on_release(leButtonWidget *btn);

/* A momentary control button: zinc-800 fill, AA rounded corners, its caption from
 * the design string table. */
static leButtonWidget *add_button(int x, int y, int w, int h, uint32_t string_id)
{
    configASSERT(s_nbtn < BTN_MAX);

    unsigned i = s_nbtn++;

    leButtonWidget *b = &s_btn[i];
    leButtonWidget_Constructor(b);
    b->fn->setPosition(b, x, y);
    b->fn->setSize(b, w, h);
    b->fn->setScheme(b, &SCHEME_BUTTON_MODE);
    b->fn->setBackgroundType(b, LE_WIDGET_BACKGROUND_FILL);
    b->fn->setBorderType(b, LE_WIDGET_BORDER_NONE);
    b->fn->setCornerRadius(b, CARD_R);

    leTableString_Constructor(&s_btn_cap[i], string_id);
    b->fn->setString(b, (leString *)&s_btn_cap[i]);

    ButtonAA_Enable(b);
    b->fn->setPressedEventCallback(b, control_on_press);
    b->fn->setReleasedEventCallback(b, control_on_release);

    Marvin_PANEL_WIIMOTES->fn->addChild(Marvin_PANEL_WIIMOTES, (leWidget *)b);
    return b;
}

/* ── fauxmote plumbing ──────────────────────────────────────────────────────*/

/* Locked means silent, enforced here rather than at each control: the scrim already makes the
 * controls unreachable, but Whammy_Set(0) fires its change callback from the show/hide path,
 * and this is the one place that cannot be forgotten by a future producer. */
static void send_guitar(void)
{
    if (!s_unlocked) { return; }

    Fauxmote_SendGuitar(s_g_mask, s_g_whammy, s_g_aux);
}

static void send_nav(void)
{
    if (!s_unlocked) { return; }

    Fauxmote_SendNav(s_nav_core, s_nav_dpad, MF_STICK_CENTER, MF_STICK_CENTER);
}

/* Which slice/field a control button drives. */
typedef enum { FLD_GMASK, FLD_GAUX, FLD_NCORE, FLD_NDPAD } ctrl_field_t;

typedef struct
{
    leButtonWidget *btn;
    ctrl_field_t    field;
    uint8_t         bit;
} ctrl_map_t;

static ctrl_map_t   s_ctrls[BTN_MAX];
static unsigned int s_nctrl;

static void ctrl_register(leButtonWidget *btn, ctrl_field_t field, uint8_t bit)
{
    if (btn == NULL || s_nctrl >= BTN_MAX) { return; }

    s_ctrls[s_nctrl].btn   = btn;
    s_ctrls[s_nctrl].field = field;
    s_ctrls[s_nctrl].bit   = bit;
    s_nctrl++;
}

/* Apply a press/release to whichever field the button owns, then push that slice. */
static void control_apply(const ctrl_map_t *c, bool pressed)
{
    uint8_t *field;

    switch (c->field)
    {
        case FLD_GMASK: field = &s_g_mask;   break;
        case FLD_GAUX:  field = &s_g_aux;    break;
        case FLD_NCORE: field = &s_nav_core; break;
        default:        field = &s_nav_dpad; break;
    }

    if (pressed) { *field |= c->bit; }
    else         { *field &= (uint8_t)~c->bit; }

    if (c->field == FLD_GMASK || c->field == FLD_GAUX) { send_guitar(); }
    else                                               { send_nav(); }
}

static void control_dispatch(leButtonWidget *btn, bool pressed)
{
    unsigned int i;

    for (i = 0u; i < s_nctrl; i++)
    {
        if (s_ctrls[i].btn == btn) { control_apply(&s_ctrls[i], pressed); return; }
    }
}

static void control_on_press(leButtonWidget *btn)   { control_dispatch(btn, true); }
static void control_on_release(leButtonWidget *btn) { control_dispatch(btn, false); }

static const uint8_t FRET_BIT[5] =
{
    MF_G_GREEN, MF_G_RED, MF_G_YELLOW, MF_G_BLUE, MF_G_ORANGE
};

static void fret_changed(unsigned index, bool held)
{
    if (index >= 5u) { return; }

    if (held) { s_g_mask |=  FRET_BIT[index]; }
    else      { s_g_mask &= (uint8_t)~FRET_BIT[index]; }
    send_guitar();
}

/* Whammy: bipolar -100..+100 -> the 5-bit wire field, rest at MF_WHAMMY_REST. */
static void whammy_changed(int32_t value)
{
    int32_t wire = (int32_t)MF_WHAMMY_REST + (value * (int32_t)MF_WHAMMY_REST) / 100;

    if (wire < 0) { wire = 0; }
    if (wire > (int32_t)MF_WHAMMY_MASK) { wire = (int32_t)MF_WHAMMY_MASK; }

    s_g_whammy = (uint8_t)wire;
    send_guitar();
}

static void tilt_changed(int32_t degrees)
{
    if (!s_unlocked) { return; }

    Fauxmote_SendTilt((int16_t)degrees);
}

/* ── build ──────────────────────────────────────────────────────────────────*/

static void build_guitar_card(void)
{
    unsigned i;

    (void)add_card(GX, CARD_Y, CARD_W, CARD_H);
    (void)add_label(GX + PAD, CARD_Y + PAD, 240, LBL_H,
                    stringID_GUITAR_EXTENSION, &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT);

    for (i = 0u; i < 5u; i++)
    {
        leWidget *pad = next_widget(GX + PAD + (int)i * (FRET_W + FRET_GAP),
                                    FRET_Y, FRET_W, FRET_H);
        Fret_Enable(pad, i, FRET_COLOR[i].idle, FRET_COLOR[i].held,
                    FRET_COLOR[i].held, fret_changed);
        s_fret[i] = pad;
    }

    ctrl_register(add_button(STRUM_X, ROW_Y,  STRUM_W, BTN_H, stringID_GUITAR_STRUM_UP),
                  FLD_GMASK, MF_G_STRUM_UP);
    ctrl_register(add_button(STRUM_X, ROW_Y2, STRUM_W, BTN_H, stringID_GUITAR_STRUM_DOWN),
                  FLD_GMASK, MF_G_STRUM_DOWN);

    ctrl_register(add_button(AUX_X, ROW_Y,  AUX_W, BTN_H, stringID_GUITAR_PLUS),
                  FLD_GAUX, MF_AUX_PLUS);
    ctrl_register(add_button(AUX_X, ROW_Y2, AUX_W, BTN_H, stringID_GUITAR_MINUS),
                  FLD_GAUX, MF_AUX_MINUS);

    (void)add_label(WHAM_X, WHAM_LBL_Y, WHAM_W, LBL_H,
                    stringID_GUITAR_WHAMMY, &SCHEME_TEXT_ZINC_500, LE_HALIGN_CENTER);

    s_whammy = next_widget(WHAM_X, WHAM_Y, WHAM_W, WHAM_H);
    Whammy_Enable(s_whammy, whammy_changed);
}

static void build_wiimote_card(void)
{
    (void)add_card(WX, CARD_Y, CARD_W, CARD_H);
    (void)add_label(WX + PAD, CARD_Y + PAD, 160, LBL_H,
                    stringID_WIIMOTE_HEADING, &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT);

    ctrl_register(add_button(DPAD_X + KEY + KEY_GAP, DPAD_Y, KEY, KEY,
                            stringID_WIIMOTE_DPAD_UP), FLD_NDPAD, MF_W_UP);
    ctrl_register(add_button(DPAD_X, DPAD_Y + KEY + KEY_GAP, KEY, KEY,
                            stringID_WIIMOTE_DPAD_LEFT), FLD_NDPAD, MF_W_LEFT);
    ctrl_register(add_button(DPAD_X + 2 * (KEY + KEY_GAP), DPAD_Y + KEY + KEY_GAP, KEY, KEY,
                            stringID_WIIMOTE_DPAD_RIGHT), FLD_NDPAD, MF_W_RIGHT);
    ctrl_register(add_button(DPAD_X + KEY + KEY_GAP, DPAD_Y + 2 * (KEY + KEY_GAP), KEY, KEY,
                            stringID_WIIMOTE_DPAD_DOWN), FLD_NDPAD, MF_W_DOWN);

    ctrl_register(add_button(ACT_X, CARD_Y + 32, HOME_W, KEY, stringID_WIIMOTE_HOME),
                  FLD_NCORE, MF_W_HOME);
    ctrl_register(add_button(ACT_X, CARD_Y + 111, KEY, KEY, stringID_WIIMOTE_A),
                  FLD_NCORE, MF_W_A);
    ctrl_register(add_button(ACT_X + KEY + 8, CARD_Y + 111, KEY, KEY, stringID_WIIMOTE_B),
                  FLD_NCORE, MF_W_B);
    ctrl_register(add_button(ACT_X, CARD_Y + 190, KEY, KEY, stringID_WIIMOTE_ONE),
                  FLD_NCORE, MF_W_ONE);
    ctrl_register(add_button(ACT_X + KEY + 8, CARD_Y + 190, KEY, KEY, stringID_WIIMOTE_TWO),
                  FLD_NCORE, MF_W_TWO);

    s_tilt = next_widget(TILT_X, TILT_Y, TILT_XY, TILT_XY);
    Tilt_Enable(s_tilt, tilt_changed);

    (void)add_label(TILT_X + 4, TILT_Y + 1, 44, LBL_H,
                    stringID_WIIMOTE_TILT, &SCHEME_TEXT_ZINC_500, LE_HALIGN_LEFT);
}

/* ── link column ────────────────────────────────────────────────────────────*/

/* One key in the link column. Bordered, unlike the control keys, so it needs the
 * pressed-border AA variant: the mockup lightens the border along with the fill. The
 * caption sits under a 24px icon, which Legato centres as one block for us. */
static leButtonWidget *add_link_button(unsigned row, uint32_t string_id,
                                       const leScheme *scheme, const leImage *icon)
{
    configASSERT(s_nbtn < BTN_MAX);

    unsigned i = s_nbtn++;

    leButtonWidget *b = &s_btn[i];
    leButtonWidget_Constructor(b);
    b->fn->setPosition(b, LNK_X, LNK_Y + (int)row * (LNK_W + LNK_GAP));
    b->fn->setSize(b, LNK_W, LNK_W);
    b->fn->setScheme(b, scheme);
    b->fn->setBackgroundType(b, LE_WIDGET_BACKGROUND_FILL);
    b->fn->setBorderType(b, LE_WIDGET_BORDER_LINE);
    b->fn->setCornerRadius(b, LNK_R);

    leTableString_Constructor(&s_btn_cap[i], string_id);
    b->fn->setString(b, (leString *)&s_btn_cap[i]);

    if (icon != NULL)
    {
        b->fn->setImagePosition(b, LE_RELATIVE_POSITION_ABOVE);
        b->fn->setImageMargin(b, LNK_ICON_GAP);
        b->fn->setPressedImage(b, (leImage *)icon);
        b->fn->setReleasedImage(b, (leImage *)icon);
    }

    ButtonAA_EnablePressedBorder(b);

    Marvin_PANEL_WIIMOTES->fn->addChild(Marvin_PANEL_WIIMOTES, (leWidget *)b);
    return b;
}

/* Reconnect / swap / 1+2 / unlink / pair. Inert for now — no callbacks bound. UNLINK and
 * PAIR carry their own schemes so a press reads red / blue, the mockup's colour for the
 * destructive and the pairing action; 1+2 is a chord on the wiimote's own keys, so it has
 * a caption where the others have an icon. */
static void build_link_column(void)
{
    s_link[0] = add_link_button(0u, stringID_WIIMOTE_LINK_RECONNECT,
                                &SCHEME_BUTTON_LINK, &BUTTON_ICON_RECONNECT);
    s_link[1] = add_link_button(1u, stringID_WIIMOTE_LINK_SWAP,
                                &SCHEME_BUTTON_LINK, &BUTTON_ICON_SWAP);
    s_link[2] = add_link_button(2u, stringID_WIIMOTE_LINK_ONE_TWO,
                                &SCHEME_BUTTON_LINK, NULL);
    s_link[3] = add_link_button(3u, stringID_WIIMOTE_LINK_UNLINK,
                                &SCHEME_BUTTON_LINK_UNLINK, &BUTTON_ICON_UNLINK);
    s_link[4] = add_link_button(4u, stringID_WIIMOTE_LINK_PAIR,
                                &SCHEME_BUTTON_LINK_PAIR, &BUTTON_ICON_PAIR);
}

/* ── video touch -> IR pointer ───────────────────────────────────────────────
 * An invisible widget over the live-video rect, so a touch on the picture aims the
 * Wii's IR cursor there. It paints nothing (BACKGROUND_NONE), which is what lets HEO
 * show through while the widget still picks — the tilt and whammy widgets sit on the
 * same footing. The touch is reported as a fraction of the picture, because the HEO
 * scaler maps the detected active source rect onto exactly this rect: no letterbox,
 * so screen position within it *is* the fraction of the Wii's screen.
 *
 * Whether pointing is allowed is decided by fauxmote_pointer's armed flag, which the
 * lock gate drives — deliberately not by an s_unlocked test here, since the scrim
 * covers only the card row and never reaches the video. */
static leWidget *s_video_hit;

static uint8_t frac_of(int32_t v, int32_t origin, int32_t span)
{
    int32_t f = ((v - origin) * 255) / (span - 1);

    if (f < 0)   { f = 0; }
    if (f > 255) { f = 255; }
    return (uint8_t)f;
}

static void video_touch(int32_t x, int32_t y, bool press)
{
    FauxmotePointer_Touch(frac_of(x, VID_X, VID_W), frac_of(y, VID_Y, VID_H), press);
}

static void video_touchDown(leWidget *wgt, leWidgetEvent_TouchDown *evt)
{
    video_touch(evt->x, evt->y, true);
    leWidgetEvent_Accept(&evt->event, wgt);
}

static void video_touchMove(leWidget *wgt, leWidgetEvent_TouchMove *evt)
{
    video_touch(evt->x, evt->y, false);
    leWidgetEvent_Accept(&evt->event, wgt);
}

/* Release leaves the pointer where it was: latched, so the operator can now reach for
 * A or HOME on the wiimote card. */
static void video_touchUp(leWidget *wgt, leWidgetEvent_TouchUp *evt)
{
    leWidgetEvent_Accept(&evt->event, wgt);
}

static leWidgetVTable s_video_vt;
static leBool         s_video_vt_ready;

static void build_video_touch(void)
{
    s_video_hit = next_widget(VID_X, VID_Y, VID_W, VID_H);

    if (!s_video_vt_ready)
    {
        s_video_vt = *s_video_hit->fn;
        s_video_vt.touchDownEvent = video_touchDown;
        s_video_vt.touchMoveEvent = video_touchMove;
        s_video_vt.touchUpEvent   = video_touchUp;
        s_video_vt_ready = LE_TRUE;
    }
    s_video_hit->fn = &s_video_vt;
}

/* ── lock gate ──────────────────────────────────────────────────────────────*/

/* Show/hide the gate's three widgets together. VISIBLE is what the renderer tests AND what
 * leUtils_PickFromWidget tests, so hiding the scrim both stops the dim and stops it swallowing
 * touches; setVisible invalidates, which repaints the row's cards at full brightness. */
static void gate_show(bool on)
{
    leWidget *part[3] = { s_scrim, s_gate_lbl, s_slide };
    unsigned  i;

    for (i = 0u; i < 3u; i++)
    {
        if (part[i] != NULL)
        {
            (void)part[i]->fn->setVisible(part[i], on ? LE_TRUE : LE_FALSE);
        }
    }
}

/* Ask fauxmote to bring the Wii link up if it is down. Called on both of this screen's
 * edges, and cheap on both: the call sends nothing when the link is already up, and
 * debounces so the second edge cannot restart what the first one began.
 *
 * Doing it on SHOW is what makes the latency free — the slide is a deliberate one-second
 * gesture, so the reconnect runs underneath it and is usually done by the time the
 * controls go live. The unlock edge then covers a link that dropped while the screen sat
 * open, or one whose STATUS had not arrived yet on entry.
 *
 * Never blocks: this is the UI task, and the blocking wait belongs to game_controller. */
static void link_kick(const char *when)
{
    switch (Fauxmote_ReconnectIfNeeded())
    {
        case FAUXMOTE_LINK_UNPAIRED:
            LOG_WARN("UI: wiimotes (%s): fauxmote never synced to the Wii; needs red-SYNC\r\n",
                     when);
            break;
        case FAUXMOTE_LINK_UNKNOWN:
            LOG_WARN("UI: wiimotes (%s): no fauxmote status; cannot manage the Wii link\r\n",
                     when);
            break;
        case FAUXMOTE_LINK_RECONNECTING:
            LOG_INFO("UI: wiimotes (%s): Wii link down, reconnect requested\r\n", when);
            break;
        case FAUXMOTE_LINK_CONNECTED:
        default:
            break;
    }
}

/* The gate opening is the edge that takes the override — not the screen being shown. Push the
 * current (all-released) state right after, so fauxmote starts from a known pose instead of
 * whatever the gameplay mirror last left latched. */
static void gate_unlocked(void)
{
    gate_show(false);

    s_unlocked = true;
    Fauxmote_SetOverride(true);
    send_guitar();
    send_nav();
    Fauxmote_SendTilt((int16_t)Tilt_Degrees());
    FauxmotePointer_SetEnabled(true);

    link_kick("unlock");
}

/* Scrim over the card row, with the heading and the slider as LATER siblings: later paints on
 * top of the dim, and later also wins the pick, so the slider is reachable while everything the
 * scrim covers is not. */
static void build_lock_gate(void)
{
    s_scrim = next_widget(SCRIM_X, SCRIM_Y, SCRIM_W, SCRIM_H);
    /* SCHEME_BACKGROUND is already the pair this needs: base #000000 to dim with, shadowDark
     * #404040 for the rim (zinc-700 to within a RGB565 step). */
    s_scrim->fn->setScheme(s_scrim, &SCHEME_BACKGROUND);
    s_scrim->fn->setBorderType(s_scrim, LE_WIDGET_BORDER_LINE);
    s_scrim->fn->setCornerRadius(s_scrim, CARD_R);
    PanelAA_EnableScrim(s_scrim, SCRIM_ALPHA);

    s_gate_lbl = (leWidget *)add_label(GATE_LBL_X, GATE_LBL_Y, GATE_LBL_W, GATE_LBL_H,
                                       stringID_WIIMOTE_LOCK_HEADING,
                                       &SCHEME_TEXT_ZINC_400, LE_HALIGN_CENTER);

    leTableString_Constructor(&s_slide_cap,  stringID_WIIMOTE_LOCK_SLIDE);
    leTableString_Constructor(&s_slide_chev, stringID_WIIMOTE_LOCK_CHEVRON);

    s_slide = next_widget(SLIDE_X, SLIDE_Y, SLIDE_W, SLIDE_H);
    SlideUnlock_Enable(s_slide, (leString *)&s_slide_cap, (leString *)&s_slide_chev,
                       gate_unlocked);
}

void ScreenWiimotes_InitSurface(void)
{
    UiSurface_Set(CANVAS_WIIMOTES, BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565, s_fb);
}

void ScreenWiimotes_Setup(void)
{
    /* Full-screen at the origin. The root is on Legato layer 4 (built by MGS); the
     * canvas window positions that layer's pixels on the display. */
    gfxcSetWindowPosition(CANVAS_WIIMOTES, 0, 0);
    gfxcSetWindowSize(CANVAS_WIIMOTES, BASE_W, BASE_H);

    VideoFrame_Fill(s_frame_fb, VID_W, VID_H,
                    VID_FRAME_R, VID_FRAME_STROKE, VID_FRAME_C4);

    /* Shared titlebar (hamburger + logos), same chrome as the other base views. */
    s_titlebar = Titlebar_Add(Marvin_PANEL_WIIMOTES);

    build_guitar_card();
    build_wiimote_card();
    build_link_column();
    build_video_touch();
    build_lock_gate();

    /* Start not shown → gate out of picking (see ScreenWiimotes_SetInput). */
    ScreenWiimotes_SetInput(false);
}

/* Gate the screen's whole subtree in/out of picking. Legato picks across every
 * attached layer top-to-bottom regardless of canvas visibility, so while this
 * screen isn't the shown base view its background panel would otherwise swallow
 * touches meant for the view beneath it. Clearing LE_WIDGET_ENABLED on the
 * background panel gates the subtree without repainting (leUtils_PickFromWidget
 * descends only into ENABLED children; the renderer never reads the flag) — the
 * same mechanism ui_manager uses for the closed song-select overlays. ui_manager
 * drives this on base-view show/hide and drawer-modal open/close. */
void ScreenWiimotes_SetInput(bool on)
{
    if (on) { Marvin_PANEL_WIIMOTES->flags |=  LE_WIDGET_ENABLED; }
    else    { Marvin_PANEL_WIIMOTES->flags &= ~LE_WIDGET_ENABLED; }
}

/* Where the live video sits on this screen, for ui_manager's HEO + overlay binding. */
void ScreenWiimotes_VideoRect(uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h)
{
    if (x != NULL) { *x = (uint32_t)VID_X; }
    if (y != NULL) { *y = (uint32_t)VID_Y; }
    if (w != NULL) { *w = (uint32_t)VID_W; }
    if (h != NULL) { *h = (uint32_t)VID_H; }
}

const void *ScreenWiimotes_VideoFrameSurface(void)
{
    return s_frame_fb;
}

/* Relinquish the fauxmote link as this base view is hidden; taking it is the gate's job
 * (gate_unlocked), so merely visiting the screen leaves the gameplay mirror alone.
 *
 * On hide, release every input to safe defaults *before* dropping the override, so a dead
 * touch never leaves a note held — but only if the gate was open, since SendGuitar/SendNav
 * apply whether or not the override is held and would otherwise zero the mirror's state.
 * Then re-lock: the gate is per-visit, and there is no re-lock control on the screen. */
void ScreenWiimotes_SetShown(bool shown)
{
    unsigned i;

    Titlebar_SetShown(s_titlebar, shown);

    s_g_mask   = 0u;
    s_g_aux    = 0u;
    s_g_whammy = MF_WHAMMY_REST;
    s_nav_core = 0u;
    s_nav_dpad = 0u;

    Whammy_Set(0);
    for (i = 0u; i < 5u; i++) { Fret_SetHeld(s_fret[i], false); }

    if (!shown && s_unlocked)
    {
        send_guitar();
        send_nav();
        Fauxmote_SetOverride(false);
    }

    /* Disarming hides the latched IR pointer, so the Wii's cursor leaves with the
     * screen. Unconditional: cheap when already disarmed, and it must not depend on
     * s_unlocked, which is cleared just below. */
    if (!shown) { FauxmotePointer_SetEnabled(false); }

    s_unlocked = false;
    SlideUnlock_Reset();
    gate_show(true);

    /* On the way in, so the reconnect overlaps the slide the user is about to make. */
    if (shown) { link_kick("show"); }
}

/* ── render probe ────────────────────────────────────────────────────────────*/

void ScreenWiimotes_Probe(unsigned iters, wiimotes_probe_fn out, void *ctx)
{
    static const struct { const char *name; leWidget **w; } PART[] = {
        { "whammy", &s_whammy },
        { "tilt",   &s_tilt   },
        { "fret[0]",&s_fret[0]},
        { "slide",  &s_slide  },
        { "scrim",  &s_scrim  },
    };

    if (out == NULL) { return; }

    /* This screen keeps no shown flag, so ask the compositor which canvas owns BASE — a
     * probe on a canvas that is not bound would time a paint nobody can see. */
    if (UiManager_BaseCanvas() != CANVAS_WIIMOTES)
    {
        out(ctx, "wiimotes: not the shown base view (nav to it first)");
        return;
    }
    if (iters == 0u || iters > 200u) { iters = 20u; }

    char line[96];

    for (size_t i = 0u; i < (sizeof(PART) / sizeof(PART[0])); i++)
    {
        leWidget *w = *PART[i].w;
        uint32_t  us;
        leRect    r;

        if (w == NULL) { continue; }

        w->fn->rectToScreen(w, &r);
        us = RenderProbe_WidgetUs(w, iters);

        /* Fixed frame cost plus the parent's fill are the floor; both are measured by
         * `titlebar probe` on this same layer, so compare against that. */
        (void)snprintf(line, sizeof line, "  %-8s %3dx%-3d (%5d px) = %6lu us",
                       PART[i].name, (int)r.width, (int)r.height,
                       (int)(r.width * r.height), (unsigned long)us);
        out(ctx, line);
    }
}
