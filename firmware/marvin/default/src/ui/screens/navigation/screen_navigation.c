#include "ui/screens/navigation/screen_navigation.h"

#include <stdbool.h>
#include <stdint.h>

#include "ui/ui_manager.h"   /* CANVAS_NAVIGATION */
#include "ui/widgets/button_aa/widget_button_aa.h"

#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* nav widgets + hamburger event */

/* The navigation drawer is layer 1 of the Marvin master screen, rendered into its
 * own canvas (CANVAS_NAVIGATION, defined in ui_manager.h). It is layer-agnostic —
 * ui_manager (the compositor) decides which hardware layer this canvas is shown
 * on; the drawer's slide works on the canvas window, independent of the layer. */
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

/* Rounded button corners. The widget cornerRadius is honored by the classic
 * skin's background draw but isn't exposed in MGS, so we set it in code. */
#define NAVIGATION_CORNER_RADIUS  12u

#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

static uint16_t FB_NOCACHE s_fb_navigation[NAVIGATION_W * NAVIGATION_H];

static bool s_navigation_open = false;

/* Navigation entries, in panel order. The selected one is the active screen. */
#define NAVIGATION_COUNT  7u

static leButtonWidget *navigation_button(unsigned int i)
{
    switch (i)
    {
        case 0:  return Marvin_BUTTON_NAV_DASHBOARD;
        case 1:  return Marvin_BUTTON_NAV_WIIMOTES;
        case 2:  return Marvin_BUTTON_NAV_LOGS;
        case 3:  return Marvin_BUTTON_NAV_PERFORMANCE;
        case 4:  return Marvin_BUTTON_NAV_SYSTEM_INFO;
        case 5:  return Marvin_BUTTON_NAV_DIAGNOSTICS;
        default: return Marvin_BUTTON_NAV_SETTINGS;
    }
}

static void navigation_close(void);   /* forward decl — navigation_on_release may close the drawer */

/* Single-active highlight: paint the active entry selected, the rest unselected. */
static void navigation_highlight(leButtonWidget *active)
{
    unsigned int i;

    for (i = 0u; i < NAVIGATION_COUNT; i++)
    {
        leButtonWidget *b = navigation_button(i);
        b->fn->setScheme(b, (b == active) ? &SCHEME_NAV_BUTTON_SELECTED
                                          : &SCHEME_NAV_BUTTON_UNSELECTED);
    }
}

/* Released-event sink for every navigation entry. Switch the highlight, then swap
 * the base view for the entries that own one (Dashboard, Wiimotes) and close the
 * drawer. The remaining entries have no screen yet — they just change the highlight
 * and leave the drawer open. */
static void navigation_on_release(leButtonWidget *btn)
{
    navigation_highlight(btn);

    if (btn == Marvin_BUTTON_NAV_DASHBOARD)
    {
        UiManager_ShowDashboard();
        navigation_close();
    }
    else if (btn == Marvin_BUTTON_NAV_WIIMOTES)
    {
        UiManager_ShowWiimotes();
        navigation_close();
    }
}

static void navigation_buttons_init(void)
{
    unsigned int i;

    /* Route every navigation-entry release through navigation_on_release
     * (runtime-registered here — the screen wires no button events itself) and
     * round the corners (not an MGS option). */
    for (i = 0u; i < NAVIGATION_COUNT; i++)
    {
        navigation_button(i)->fn->setReleasedEventCallback(navigation_button(i), navigation_on_release);
        navigation_button(i)->fn->setCornerRadius(navigation_button(i), NAVIGATION_CORNER_RADIUS);
        ButtonAA_Enable(navigation_button(i));
    }

    /* Dashboard is the active entry at startup — set the highlight only (calling
     * the release sink here would close the not-yet-open drawer). */
    navigation_highlight(Marvin_BUTTON_NAV_DASHBOARD);
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
    /* Modal: gate the base view beneath (dashboard or wiimotes) so nothing behind the
     * drawer reacts — and, since the wiimotes view sits on a higher Legato layer than
     * the drawer, gating it off is what lets the drawer receive touches at all. The
     * drawer covers the hamburger, so close is via a drawer entry, not the hamburger —
     * gating loses no affordance. */
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
 * (the shared ui/titlebar component, plus the MGS-authored dashboard/wiimotes
 * buttons until they migrate to that component). */
void ScreenNavigation_ToggleDrawer(void)
{
    if (s_navigation_open) { navigation_close(); } else { navigation_open(); }
}

/* The titlebar hamburger toggles the navigation drawer. */
static void hamburger_on_press(leButtonWidget *btn)
{
    (void)btn;
    ScreenNavigation_ToggleDrawer();
}

void ScreenNavigation_InitSurface(void)
{
    gfxcSetPixelBuffer(CANVAS_NAVIGATION, NAVIGATION_W, NAVIGATION_H, GFX_COLOR_MODE_RGB_565, s_fb_navigation);
}

/* Per-panel setup for the navigation drawer (Marvin layer-screen 1,
 * CANVAS_NAVIGATION). MGS has already built the panel onto Legato layer 1; here we
 * set the canvas window (parked off-screen / closed), the move-FX callback, and
 * wire the buttons. The canvas is bound to a hardware layer by ui_manager. Keep the
 * panel visible+enabled so Legato renders it continuously, and start closed: the
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

    navigation_buttons_init();

    /* Both base views carry a titlebar hamburger (dashboard header on layer 0, and
     * the wiimotes screen's own titlebar on layer 4); wire both to toggle the drawer. */
    Marvin_BUTTON_DASHBOARD_TITLEBAR_NAVIGATION->fn->setPressedEventCallback(
        Marvin_BUTTON_DASHBOARD_TITLEBAR_NAVIGATION, hamburger_on_press);
    Marvin_BUTTON_WIIMOTES_TITLEBAR_NAVIGATION->fn->setPressedEventCallback(
        Marvin_BUTTON_WIIMOTES_TITLEBAR_NAVIGATION, hamburger_on_press);
}
