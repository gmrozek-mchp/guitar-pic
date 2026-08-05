#include "ui/screens/wiimotes/screen_wiimotes.h"

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"

#include "ui/ui_manager.h"   /* CANVAS_WIIMOTES, BASE_W, BASE_H */
#include "ui/titlebar.h"     /* shared hamburger + logos titlebar */
#include "ui/gfx/video_frame.h"
#include "ui/widgets/panel_aa/widget_panel_aa.h"
#include "ui/widgets/button_aa/widget_button_aa.h"
#include "ui/widgets/fret/widget_fret.h"
#include "ui/widgets/whammy/widget_whammy.h"
#include "ui/widgets/tilt/widget_tilt.h"

#include "ui/gfx/ui_surface.h"
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
#include "net/fauxmote/mf_proto.h"

/* Manual-override screen, built programmatically into the empty MGS layer-4 panel
 * (Marvin_PANEL_WIIMOTES) — the screen_bus.c model. Ports tools' mockup
 * ManualOverrideScreen.tsx with Tailwind units resolved to pixels: live video
 * centered up top, then the guitar-extension and wiimote cards side by side.
 *
 * Text comes from the DESIGN string table (leTableString + stringID_*), not C
 * literals: five of these labels are non-ASCII (the d-pad arrows and the true minus
 * U+2212), and MGS only auto-includes glyphs for strings it can see in the design.
 * A C literal would silently render blanks after the next Generate.
 *
 * Every control drives fauxmote directly while this screen is shown; see
 * ScreenWiimotes_SetShown. */

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

/* ── widget pool ────────────────────────────────────────────────────────────
 * Static storage, constructed in place — no allocator (see the project's static
 * allocation rule). Sized to the built screen with a little slack. */
#define WGT_MAX   12u
#define BTN_MAX   16u
#define LBL_MAX    6u

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

static void send_guitar(void)
{
    Fauxmote_SendGuitar(s_g_mask, s_g_whammy, s_g_aux);
}

static void send_nav(void)
{
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
    (void)Titlebar_Add(Marvin_PANEL_WIIMOTES);

    build_guitar_card();
    build_wiimote_card();

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

/* Take/relinquish the fauxmote link as this base view is shown/hidden. While shown
 * the screen owns the GUITAR slice (override blocks the gameplay mirror) and drives
 * fauxmote directly from its controls. On hide, release every input to safe defaults
 * *before* dropping the override so a dead touch never leaves a note held, then hand
 * fauxmote back to the gameplay mirror. */
void ScreenWiimotes_SetShown(bool shown)
{
    unsigned i;

    s_g_mask   = 0u;
    s_g_aux    = 0u;
    s_g_whammy = MF_WHAMMY_REST;
    s_nav_core = 0u;
    s_nav_dpad = 0u;

    Whammy_Set(0);
    for (i = 0u; i < 5u; i++) { Fret_SetHeld(s_fret[i], false); }

    if (shown)
    {
        Fauxmote_SetOverride(true);
        send_guitar();
        send_nav();
        Fauxmote_SendTilt((int16_t)Tilt_Degrees());
    }
    else
    {
        send_guitar();
        send_nav();
        Fauxmote_SetOverride(false);
    }
}
