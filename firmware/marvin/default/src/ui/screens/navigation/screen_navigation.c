#include "ui/screens/navigation/screen_navigation.h"

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"        /* configASSERT */

#include "ui/ui_manager.h"   /* CANVAS_NAVIGATION */
#include "ui/widgets/button_aa/widget_button_aa.h"
#include "ui/widgets/panel_aa/widget_panel_aa.h"

#include "ui/gfx/ui_surface.h"
#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/string/legato_tablestring.h"
#include "gfx/legato/widget/legato_widget.h"
#include "gfx/legato/widget/button/legato_widget_button.h"
#include "gfx/legato/widget/label/legato_widget_label.h"
#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"                 /* stringID_*, NAV_ICON_* */
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* Marvin_PANEL_NAVIGATION */

/* Navigation drawer, built programmatically into the empty MGS layer-1 panel
 * (Marvin_PANEL_NAVIGATION) — the screen_bus.c model. Ports the mockup's
 * NavigationDrawer.tsx with Tailwind units resolved to pixels: a header, one row per
 * base view, and a status footer, over a zinc-900 backdrop.
 *
 * The row set is NAV_ENTRY below — one row per screen that exists. The design's four
 * remaining nav icon pairs and their strings (Activity Logs, Performance, System
 * Info, Settings, Diagnostics) are kept for the screens they name, so adding a row
 * is one line here.
 *
 * Text comes from the DESIGN string table (leTableString + stringID_*), not C
 * literals, so the captions stay translatable and MGS keeps auto-including their
 * glyphs.
 *
 * The drawer is layer-agnostic: ui_manager (the compositor) decides which hardware
 * layer this canvas is shown on, and the slide works on the canvas window,
 * independent of the layer. */

#define NAVIGATION_W   320u
#define NAVIGATION_H   800u

/* gfxcStartEffectMove delta — slide feel (DEC = ease-out). Tune to taste. */
#define NAVIGATION_SLIDE_DELTA  6u

/* Off-screen-left X for the closed drawer / slide-out target.
 *
 * A canvas layer can't truly sit off-screen — the window-clip (gfx_canvas.c
 * _gfxcCanvasUpdate) emulates a negative X by shifting the source pointer right
 * and shrinking the window. But it first aligns X *down* to a multiple of
 * CANVAS_WIN_X_ALIGN (`x &= ~0x3` for non-32bpp surfaces), and once |X| reaches
 * the surface width the shifted pointer wraps to the next row, flashing the
 * panel's left columns through. So the furthest the drawer can validly sit is
 * the largest aligned X with |X| < NAVIGATION_W. We slide to there; navigation_fx_done
 * then disables the layer, clearing the residual edge sliver. Derived from
 * NAVIGATION_W so it tracks the surface width, not a hard-coded pixel count. */
#define CANVAS_WIN_X_ALIGN  4u   /* mirrors the clip's `x &= ~0x3` */
#define NAVIGATION_CLOSED_X (-(int)((NAVIGATION_W - 1u) & ~(CANVAS_WIN_X_ALIGN - 1u)))

/* ── layout ─────────────────────────────────────────────────────────────────
 * Mockup Tailwind resolved to pixels: p-6 = 24 (header/footer), p-4 = 16 (row
 * strip), mb-2 = 8 (row gap), rounded-lg = 8, w-3 = 12 (status dot), gap-3 = 12
 * (dot → text), gap-4 = 16 (icon → caption, as the row's left margin + imageMargin).
 * Fonts come from each string's design binding: text-lg bold / base / sm / xs =
 * DejaVuSansMonoBold_18 / Mono_16 / _14 / _12. */
#define PAD          24                  /* p-6 */
#define HDR_TITLE_Y  PAD
#define HDR_TITLE_H  28                  /* text-lg, line-height 1.5 */
#define HDR_SUB_Y    (HDR_TITLE_Y + HDR_TITLE_H + 4)   /* mt-1 */
#define HDR_SUB_H    16                  /* text-xs */
#define HDR_H        (HDR_SUB_Y + HDR_SUB_H + PAD + 1) /* border-b on the last row */
#define ROW_X        16                  /* p-4 */
#define ROW_W        (NAVIGATION_W - 2u * ROW_X)
#define ROW_H        56                  /* p-4 + 24px icon */
#define ROW_PITCH    (ROW_H + 8)         /* mb-2 */
#define ROW_Y0       (HDR_H + ROW_X)
#define ROW_R         8                  /* rounded-lg */
#define ICON_GAP     16                  /* gap-4 */
#define NAV_ICON_D   24                  /* the NAV_ICON_* assets are 24x24 */
#define DOT_D        12                  /* w-3 h-3 */

/* Footer: a 36-tall content row PAD in from the bottom-left corner, with the
 * border-t one row above the block's top padding (1 + 24 + 36 + 24 = 85 tall). */
#define FTR_ROW_H    36
#define FTR_H        (1 + PAD + FTR_ROW_H + PAD)
#define FTR_Y        ((int)NAVIGATION_H - FTR_H)   /* the border-t row */
#define FTR_ROW_Y    (FTR_Y + 1 + PAD)
#define FTR_TXT_X    (PAD + DOT_D + 12)  /* dot + gap-3 */
#define FTR_TXT_W    ((int)NAVIGATION_W - FTR_TXT_X - PAD)

#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

static uint16_t FB_NOCACHE s_fb_navigation[NAVIGATION_W * NAVIGATION_H];

static bool s_navigation_open = false;

static void navigation_close(void);   /* forward decl — navigation_on_release may close the drawer */

/* Navigation entries, in panel order. `show` swaps the base view and closes the
 * drawer; an entry with no screen yet would leave it NULL (highlight only). A
 * scheme changes a button's fill but cannot recolour an image, so the selected look
 * needs its own asset: each icon ships twice, zinc-300 for rest and white for
 * selected (both rendered from one SVG in assets/icon/lucide, so only the colour
 * differs). */
static const struct {
    uint32_t       string_id;
    const leImage *rest;
    const leImage *selected;
    void         (*show)(void);
} NAV_ENTRY[] = {
    { stringID_NAV_BUTTON_Dashboard,      &NAV_ICON_DASHBOARD, &NAV_ICON_DASHBOARD_SELECTED, UiManager_ShowDashboard },
    { stringID_NAV_BUTTON_Wiimotes,       &NAV_ICON_WIIMOTES,  &NAV_ICON_WIIMOTES_SELECTED,  UiManager_ShowWiimotes  },
    { stringID_NAV_BUTTON_Bus_Statistics, &NAV_ICON_BUS,       &NAV_ICON_BUS_SELECTED,       UiManager_ShowStats     },
    { stringID_NAV_BUTTON_System_Info,    &NAV_ICON_SYSTEM_INFO, &NAV_ICON_SYSTEM_INFO_SELECTED, UiManager_ShowSystem },
};

#define NAVIGATION_COUNT  (sizeof NAV_ENTRY / sizeof NAV_ENTRY[0])

/* ── widget pool ────────────────────────────────────────────────────────────
 * Static storage, constructed in place — no allocator (see the project's static
 * allocation rule). Sized to the built drawer. */
#define WGT_MAX   4u                     /* 3 hairlines + the status dot */
#define LBL_MAX   4u                     /* title, version, STATUS, connection */

static leWidget       s_wgt[WGT_MAX];
static unsigned       s_nwgt;
static leLabelWidget  s_lbl[LBL_MAX];
static leTableString  s_lbl_str[LBL_MAX];
static unsigned       s_nlbl;
static leButtonWidget s_row[NAVIGATION_COUNT];
static leTableString  s_row_cap[NAVIGATION_COUNT];

/* Index of the entry currently painted selected; -1 before the first highlight. */
static int s_navigation_active = -1;

/* Row index of a button, or -1 if it isn't one of ours. */
static int navigation_index(const leButtonWidget *btn)
{
    unsigned int i;

    for (i = 0u; i < NAVIGATION_COUNT; i++)
    {
        if (&s_row[i] == btn) { return (int)i; }
    }
    return -1;
}

/* A 1px hairline — the mockup's border-r / border-b / border-t in zinc-700. */
static void add_rule(int x, int y, int w, int h)
{
    configASSERT(s_nwgt < WGT_MAX);

    leWidget *p = &s_wgt[s_nwgt++];
    leWidget_Constructor(p);
    p->fn->setPosition(p, x, y);
    p->fn->setSize(p, w, h);
    p->fn->setScheme(p, &SCHEME_FILL_ZINC_700);
    p->fn->setBackgroundType(p, LE_WIDGET_BACKGROUND_FILL);
    p->fn->setBorderType(p, LE_WIDGET_BORDER_NONE);
    Marvin_PANEL_NAVIGATION->fn->addChild(Marvin_PANEL_NAVIGATION, p);
}

/* A design-string label. The string carries its own font via the design's binding,
 * which is the whole reason these are table strings (see the file header). */
static void add_label(int x, int y, int w, int h, uint32_t string_id,
                      const leScheme *scheme)
{
    configASSERT(s_nlbl < LBL_MAX);

    leLabelWidget *l = &s_lbl[s_nlbl];
    leLabelWidget_Constructor(l);
    l->fn->setPosition(l, x, y);
    l->fn->setSize(l, w, h);
    l->fn->setScheme(l, scheme);
    l->fn->setBackgroundType(l, LE_WIDGET_BACKGROUND_NONE);
    l->fn->setHAlignment(l, LE_HALIGN_LEFT);
    l->fn->setVAlignment(l, LE_VALIGN_MIDDLE);

    leTableString_Constructor(&s_lbl_str[s_nlbl], string_id);
    l->fn->setString(l, (leString *)&s_lbl_str[s_nlbl]);
    s_nlbl++;

    Marvin_PANEL_NAVIGATION->fn->addChild(Marvin_PANEL_NAVIGATION, (leWidget *)l);
}

/* Apply one entry's selected/unselected look.
 *
 * The explicit invalidate is required: the image setters raise no damage, and the
 * damage setScheme raises does not cover every pixel the icon draw touched. A small
 * block at the icon's top-left kept its previous paint — showing stale blue on a row
 * that had been selected, or uninitialized DDR (the .region_nocache surfaces are
 * NOLOAD and never zeroed) on one that had not. Same rect either way; only the
 * leftover content differed. */
static void navigation_paint_entry(unsigned int i, bool on)
{
    leButtonWidget *b = &s_row[i];

    b->fn->setScheme(b, on ? &SCHEME_NAV_BUTTON_SELECTED
                           : &SCHEME_NAV_BUTTON_UNSELECTED);

    const leImage *icon = on ? NAV_ENTRY[i].selected : NAV_ENTRY[i].rest;
    b->fn->setPressedImage(b, (leImage *)icon);
    b->fn->setReleasedImage(b, (leImage *)icon);

    b->fn->invalidate(b);
}

/* Single-active highlight. Only the two entries whose state actually changes are
 * repainted — the one losing selection and the one gaining it — rather than every
 * row, so a selection costs two button repaints.
 *
 * The first call is the exception and paints every entry, so the look never depends
 * on what the builder happened to leave the rows in. */
static void navigation_highlight(const leButtonWidget *active)
{
    unsigned int i;
    int next = navigation_index(active);

    if (s_navigation_active < 0)
    {
        for (i = 0u; i < NAVIGATION_COUNT; i++)
        {
            navigation_paint_entry(i, (int)i == next);
        }
    }
    else if (next != s_navigation_active)
    {
        navigation_paint_entry((unsigned int)s_navigation_active, false);
        if (next >= 0) { navigation_paint_entry((unsigned int)next, true); }
    }

    s_navigation_active = next;
}

/* Released-event sink for every navigation entry. Switch the highlight, then swap
 * the base view and close the drawer for the entries that own one. */
static void navigation_on_release(leButtonWidget *btn)
{
    int i = navigation_index(btn);

    navigation_highlight(btn);

    if (i >= 0 && NAV_ENTRY[i].show != NULL)
    {
        NAV_ENTRY[i].show();
        navigation_close();
    }
}

/* Build the drawer: header, one row per entry, status footer. Rows carry their
 * caption from the design string table and their icon from NAV_ENTRY; the
 * selected/unselected look is applied by the first navigation_highlight. */
static void navigation_build(void)
{
    unsigned int i;

    /* border-r: the drawer's own right edge, so it reads as a panel over whatever
     * base view sits behind it. */
    add_rule((int)NAVIGATION_W - 1, 0, 1, (int)NAVIGATION_H);

    add_label(PAD, HDR_TITLE_Y, (int)NAVIGATION_W - 2 * PAD, HDR_TITLE_H,
              stringID_NAV_NAVIGATION, &SCHEME_TEXT_WHITE);
    add_label(PAD, HDR_SUB_Y, (int)NAVIGATION_W - 2 * PAD, HDR_SUB_H,
              stringID_NAV_Marvin_v1_0_0, &SCHEME_TEXT_ZINC_500);
    add_rule(0, HDR_H - 1, (int)NAVIGATION_W, 1);                  /* border-b */

    for (i = 0u; i < NAVIGATION_COUNT; i++)
    {
        leButtonWidget *b = &s_row[i];

        leButtonWidget_Constructor(b);
        b->fn->setPosition(b, ROW_X, ROW_Y0 + (int)i * ROW_PITCH);
        b->fn->setSize(b, (int)ROW_W, ROW_H);
        b->fn->setScheme(b, &SCHEME_NAV_BUTTON_UNSELECTED);
        b->fn->setBackgroundType(b, LE_WIDGET_BACKGROUND_FILL);
        b->fn->setBorderType(b, LE_WIDGET_BORDER_NONE);
        /* Rounded corners: the widget cornerRadius is honored by the classic skin's
         * background draw but isn't exposed in MGS, so it's set here; ButtonAA
         * smooths what the skin steps. */
        b->fn->setCornerRadius(b, ROW_R);
        b->fn->setHAlignment(b, LE_HALIGN_LEFT);
        b->fn->setMargins(b, ICON_GAP, 4, 4, 4);
        b->fn->setImageMargin(b, ICON_GAP);
        b->fn->setPressedOffset(b, 0);

        leTableString_Constructor(&s_row_cap[i], NAV_ENTRY[i].string_id);
        b->fn->setString(b, (leString *)&s_row_cap[i]);

        b->fn->setReleasedEventCallback(b, navigation_on_release);
        ButtonAA_Enable(b);

        Marvin_PANEL_NAVIGATION->fn->addChild(Marvin_PANEL_NAVIGATION, (leWidget *)b);
    }

    add_rule(0, FTR_Y, (int)NAVIGATION_W, 1);                      /* border-t */

    configASSERT(s_nwgt < WGT_MAX);
    leWidget *dot = &s_wgt[s_nwgt++];
    leWidget_Constructor(dot);
    dot->fn->setPosition(dot, PAD, FTR_ROW_Y + (FTR_ROW_H - DOT_D) / 2);
    dot->fn->setSize(dot, DOT_D, DOT_D);
    dot->fn->setScheme(dot, &SCHEME_FILL_GREEN_500);
    dot->fn->setBackgroundType(dot, LE_WIDGET_BACKGROUND_FILL);
    dot->fn->setBorderType(dot, LE_WIDGET_BORDER_NONE);
    PanelAA_EnableDot(dot);
    Marvin_PANEL_NAVIGATION->fn->addChild(Marvin_PANEL_NAVIGATION, dot);

    add_label(FTR_TXT_X, FTR_ROW_Y, FTR_TXT_W, 16,               /* text-xs */
              stringID_NAV_STATUS, &SCHEME_TEXT_ZINC_500);
    add_label(FTR_TXT_X, FTR_ROW_Y + 16, FTR_TXT_W, 20,          /* text-sm */
              stringID_NAV_ConnectionStatus, &SCHEME_TEXT_WHITE);

    /* The first entry (Dashboard) is active at startup — set the highlight only
     * (calling the release sink here would close the not-yet-open drawer). */
    navigation_highlight(&s_row[0]);
}

/* When false the drawer jumps to its target instead of animating. The canvas Move
 * FX is the one thing the drawer uses that no other screen does, so this makes it
 * an A/B test at runtime (`nav slide on|off`) rather than a rebuild. */
static bool s_navigation_slide = true;

void ScreenNavigation_SetSlide(bool on)
{
    s_navigation_slide = on;
}

bool ScreenNavigation_GetSlide(void)
{
    return s_navigation_slide;
}

bool ScreenNavigation_GetIconRect(unsigned int row, uint16_t *x, uint16_t *y,
                                  uint16_t *size)
{
    if (row >= NAVIGATION_COUNT) { return false; }

    /* The icon sits at the row's left margin, vertically centred in its height. */
    if (x != NULL)    { *x = (uint16_t)(ROW_X + ICON_GAP); }
    if (y != NULL)    { *y = (uint16_t)(ROW_Y0 + (int)row * ROW_PITCH + (ROW_H - NAV_ICON_D) / 2); }
    if (size != NULL) { *size = NAV_ICON_D; }
    return true;
}

/* Animate the drawer to target_x from wherever it currently sits. Cancel any
 * in-flight slide first: gfxcStartEffectMove is a no-op while a move is running
 * (it only starts from status IDLE), so without the stop a mid-slide reversal
 * would be silently dropped — the original move would run to completion and fire
 * navigation_fx_done with a now-stale s_navigation_open, hiding the drawer and
 * wedging the toggle. Reading the live position makes the reversal retarget smoothly. */
static void navigation_slide_to(int target_x)
{
    int x, y;

    gfxcStopEffect(CANVAS_NAVIGATION, GFXC_FX_MOVE);

    if (!s_navigation_slide)
    {
        /* Jump straight there. With no move to complete, navigation_fx_done never
         * runs, so the close ending it owns — disable the layer to clear the
         * residual edge sliver, then re-arm base-view input — has to happen here.
         *
         * Discriminate on the TARGET, not s_navigation_open: navigation_open calls
         * this *before* setting that flag, so testing the flag would treat an open
         * as a close and hide the drawer the instant it was shown. The animated path
         * is immune because fx_done runs long after the caller has updated it. */
        gfxcSetWindowPosition(CANVAS_NAVIGATION, target_x, 0);
        gfxcCanvasUpdate(CANVAS_NAVIGATION);

        if (target_x != 0)
        {
            UiManager_HideNavLayer();
            UiManager_SetBaseViewPickable(true);
        }
        return;
    }

    gfxcGetWindowPosition(CANVAS_NAVIGATION, &x, &y);
    gfxcStartEffectMove(CANVAS_NAVIGATION, GFXC_FX_MOVE_DEC, x, 0, target_x, 0, NAVIGATION_SLIDE_DELTA);
}

static void navigation_open(void)
{
    /* The drawer's surface is painted once at boot and never changes on show/hide,
     * so open is a pure layer bind + slide — no repaint. The compositor binds the
     * drawer canvas to OVR2 (above the video frame on OVR1, so an open drawer covers
     * the frame's left edge) and sets its RGB565 mode; it and the album-art strip
     * share OVR2, so the drawer is unbound when closed. Caller ensures album art
     * isn't holding OVR2. */
    UiManager_ShowNavLayer();
    navigation_slide_to(0);
    /* Modal: gate the base view beneath (dashboard, wiimotes or bus) so nothing
     * behind the drawer reacts — and, since those views sit on higher Legato layers
     * than the drawer, gating them off is what lets the drawer receive touches at
     * all. The drawer covers the hamburger, so close is via a drawer entry, not the
     * hamburger — gating loses no affordance. */
    UiManager_SetBaseViewPickable(false);
    s_navigation_open = true;
}

static void navigation_close(void)
{
    /* Slide out to NAVIGATION_CLOSED_X (off-screen), then navigation_fx_done
     * disables the layer. NAVIGATION_CLOSED_X avoids the -NAVIGATION_W window-clip
     * row-wrap (see its definition); the layer update busy-waits for the vsync latch,
     * so the final frame is displayed before the hide lands — keeping it in-bounds
     * makes that frame a harmless edge sliver instead of the panel's left columns. */
    navigation_slide_to(NAVIGATION_CLOSED_X);
    s_navigation_open = false;
    /* Dashboard input is re-armed by navigation_fx_done once the slide completes,
     * NOT here: the drawer still owns OVR2 mid-slide, so re-arming now would let a
     * header tap open the song-select modal (which grabs OVR2 for album art) before
     * fx_done hides the drawer's canvas — the hide would then turn OVR2 off under
     * the cover strip. */
}

/* Move-effect completion callback. At the off-screen end of a close slide the
 * window clip leaves a degenerate sliver of the drawer composited at screen
 * left; hiding the canvas (disabling its layer) once the slide finishes removes it cleanly. Only
 * acts on a completed close — an open leaves the layer shown, and a re-open
 * mid-close restarts the move (so this won't fire for the abandoned close).
 * Re-arming dashboard input happens here too, only after OVR2 is freed, so no
 * mid-slide tap can open a modal onto the layer this hide is about to disable. */
static void navigation_fx_done(unsigned int canvasID, GFXC_FX_TYPE effect,
                               GFXC_FX_STATUS status, void *parm)
{
    (void)canvasID;
    (void)parm;

    if (effect == GFXC_FX_MOVE && status == GFXC_FX_DONE && !s_navigation_open)
    {
        UiManager_HideNavLayer();
        UiManager_SetBaseViewPickable(true);
    }
}

/* Open/close the drawer. Public so any base-view titlebar hamburger can toggle it
 * (the shared ui/titlebar component). */
void ScreenNavigation_ToggleDrawer(void)
{
    if (s_navigation_open) { navigation_close(); } else { navigation_open(); }
}


void ScreenNavigation_InitSurface(void)
{
    UiSurface_Set(CANVAS_NAVIGATION, NAVIGATION_W, NAVIGATION_H, GFX_COLOR_MODE_RGB_565, s_fb_navigation);
}

/* Per-panel setup for the navigation drawer (Marvin layer-screen 1,
 * CANVAS_NAVIGATION). MGS supplies only the empty zinc-900 panel on Legato layer 1;
 * here we build its contents, set the canvas window (parked off-screen / closed) and
 * the move-FX callback. The canvas is bound to a hardware layer by ui_manager. Keep
 * the panel visible+enabled so Legato renders it continuously, and start closed: the
 * touch pick rect follows the canvas window position (LE_DRIVER_LAYER_MODE), so a
 * closed (off-screen) drawer can't intercept dashboard touches, and the off-screen
 * X seeds the slide-in. */
void ScreenNavigation_Setup(void)
{
    Marvin_PANEL_NAVIGATION->fn->setEnabled(Marvin_PANEL_NAVIGATION, LE_TRUE);
    Marvin_PANEL_NAVIGATION->fn->setVisible(Marvin_PANEL_NAVIGATION, LE_TRUE);

    gfxcSetWindowSize(CANVAS_NAVIGATION, NAVIGATION_W, NAVIGATION_H);
    gfxcSetWindowPosition(CANVAS_NAVIGATION, NAVIGATION_CLOSED_X, 0);
    gfxcSetEffectsCallback(CANVAS_NAVIGATION, navigation_fx_done, NULL);

    navigation_build();

    /* Every base view's hamburger comes from the shared ui/titlebar component, which
     * wires it to ScreenNavigation_ToggleDrawer itself — nothing to wire here. */
}
