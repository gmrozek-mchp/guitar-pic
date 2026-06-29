#include "ui/screens/nav/screen_nav.h"

#include <stdbool.h>
#include <stdint.h>

#include "ui/ui_manager.h"   /* CANVAS_NAV */
#include "ui/widgets/button_aa/widget_button_aa.h"

#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* nav widgets + hamburger event */

/* The nav drawer is layer 1 of the Marvin master screen, rendered into its own
 * canvas (CANVAS_NAV, defined in ui_manager.h). The nav is layer-agnostic —
 * ui_manager (the compositor) decides which hardware layer this canvas is shown
 * on; the drawer's slide works on the canvas window, independent of the layer. */
#define NAV_W   320u
#define NAV_H   800u

/* gfxcStartEffectMove delta — slide feel (DEC = ease-out). Tune to taste. */
#define NAV_SLIDE_DELTA  6u

/* Off-screen-left X for the closed drawer / slide-out target.
 *
 * A canvas layer can't truly sit off-screen — the window-clip (gfx_canvas.c
 * _gfxcCanvasUpdate) emulates a negative X by shifting the source pointer right
 * and shrinking the window. But it first aligns X *down* to a multiple of
 * CANVAS_WIN_X_ALIGN (`x &= ~0x3` for non-32bpp surfaces), and once |X| reaches
 * the surface width the shifted pointer wraps to the next row, flashing the
 * panel's left columns through. So the furthest the drawer can validly sit is
 * the largest aligned X with |X| < NAV_W. We slide to there; nav_fx_done then
 * disables the layer, clearing the residual edge sliver. Derived from NAV_W so
 * it tracks the surface width, not a hard-coded pixel count. */
#define CANVAS_WIN_X_ALIGN  4u   /* mirrors the clip's `x &= ~0x3` */
#define NAV_CLOSED_X        (-(int)((NAV_W - 1u) & ~(CANVAS_WIN_X_ALIGN - 1u)))

/* Rounded button corners. The widget cornerRadius is honored by the classic
 * skin's background draw but isn't exposed in MGS, so we set it in code. */
#define NAV_CORNER_RADIUS  12u

#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

static uint16_t FB_NOCACHE s_fb_nav[NAV_W * NAV_H];

static bool s_nav_open = false;

/* Navigation entries, in panel order. The selected one is the active screen. */
#define NAV_COUNT  6u

static leButtonWidget *nav_button(unsigned int i)
{
    switch (i)
    {
        case 0:  return Marvin_BUTTON_NAV_DASHBOARD_0;
        case 1:  return Marvin_BUTTON_NAV_LOGS_0;
        case 2:  return Marvin_BUTTON_NAV_PERFORMANCE_0;
        case 3:  return Marvin_BUTTON_NAV_SYSTEM_INFO_0;
        case 4:  return Marvin_BUTTON_NAV_DIAGNOSTICS_0;
        default: return Marvin_BUTTON_NAV_SETTINGS_0;
    }
}

static void nav_close(void);   /* forward decl — nav_on_release may close the drawer */

/* Single-active highlight: paint the active entry selected, the rest unselected. */
static void nav_highlight(leButtonWidget *active)
{
    unsigned int i;

    for (i = 0u; i < NAV_COUNT; i++)
    {
        leButtonWidget *b = nav_button(i);
        b->fn->setScheme(b, (b == active) ? &SCHEME_NAV_BUTTON_SELECTED
                                          : &SCHEME_NAV_BUTTON_UNSELECTED);
    }
}

/* Released-event sink for every nav entry. Switch the highlight; Dashboard means
 * "back to the main view" so it also closes the drawer, while the other entries
 * just change the selection and stay open. The active screen-switch will hook
 * here once the per-screen canvas model lands. */
static void nav_on_release(leButtonWidget *btn)
{
    nav_highlight(btn);

    if (btn == Marvin_BUTTON_NAV_DASHBOARD_0)
    {
        nav_close();
    }
}

static void nav_buttons_init(void)
{
    unsigned int i;

    /* Route every nav-entry release through nav_on_release (runtime-registered
     * here — the Navigation screen wires no button events itself) and round the
     * corners (not an MGS option). */
    for (i = 0u; i < NAV_COUNT; i++)
    {
        nav_button(i)->fn->setReleasedEventCallback(nav_button(i), nav_on_release);
        nav_button(i)->fn->setCornerRadius(nav_button(i), NAV_CORNER_RADIUS);
        ButtonAA_Enable(nav_button(i));
    }

    /* Dashboard is the active entry at startup — set the highlight only (calling
     * the release sink here would close the not-yet-open drawer). */
    nav_highlight(Marvin_BUTTON_NAV_DASHBOARD_0);
}

/* Animate the drawer to target_x from wherever it currently sits. Cancel any
 * in-flight slide first: gfxcStartEffectMove is a no-op while a move is running
 * (it only starts from status IDLE), so without the stop a mid-slide reversal
 * would be silently dropped — the original move would run to completion and fire
 * nav_fx_done with a now-stale s_nav_open, hiding the drawer and wedging the
 * toggle. Reading the live position makes the reversal retarget smoothly. */
static void nav_slide_to(int target_x)
{
    int x, y;

    gfxcStopEffect(CANVAS_NAV, GFXC_FX_MOVE);
    gfxcGetWindowPosition(CANVAS_NAV, &x, &y);
    gfxcStartEffectMove(CANVAS_NAV, GFXC_FX_MOVE_DEC, x, 0, target_x, 0, NAV_SLIDE_DELTA);
}

static void nav_open(void)
{
    /* Force a full repaint of the panel into the canvas before revealing it — the
     * paint queued at startup (parked off-screen) doesn't fully land in the
     * buffer, so without this the drawer slides in partially drawn until touch
     * damage fills it in. The repaint lands over the next frames as it slides.
     * Show first so the move is visible (the FX engine enables the layer from
     * canvas.active). */
    Marvin_PANEL_NAVIGATION->fn->invalidate(Marvin_PANEL_NAVIGATION);
    gfxcShowCanvas(CANVAS_NAV);
    nav_slide_to(0);
    s_nav_open = true;
}

static void nav_close(void)
{
    /* Slide out to NAV_CLOSED_X (off-screen), then nav_fx_done disables the
     * layer. NAV_CLOSED_X avoids the -NAV_W window-clip row-wrap (see its
     * definition); the layer update busy-waits for the vsync latch, so the final
     * frame is displayed before the hide lands — keeping it in-bounds makes that
     * frame a harmless edge sliver instead of the panel's left columns. */
    nav_slide_to(NAV_CLOSED_X);
    s_nav_open = false;
}

/* Move-effect completion callback. At the off-screen end of a close slide the
 * window clip leaves a degenerate sliver of the drawer composited at screen
 * left; hiding the canvas (disabling its layer) once the slide finishes removes it cleanly. Only
 * acts on a completed close — an open leaves the layer shown, and a re-open
 * mid-close restarts the move (so this won't fire for the abandoned close). */
static void nav_fx_done(unsigned int canvasID, GFXC_FX_TYPE effect,
                        GFXC_FX_STATUS status, void *parm)
{
    (void)canvasID;
    (void)parm;

    if (effect == GFXC_FX_MOVE && status == GFXC_FX_DONE && !s_nav_open)
    {
        gfxcHideCanvas(CANVAS_NAV);
        gfxcCanvasUpdate(CANVAS_NAV);
    }
}

void Nav_InitSurface(void)
{
    gfxcSetPixelBuffer(CANVAS_NAV, NAV_W, NAV_H, GFX_COLOR_MODE_RGB_565, s_fb_nav);
}

/* Per-panel setup for the nav drawer (Marvin layer-screen 1, CANVAS_NAV). MGS has
 * already built the panel onto Legato layer 1; here we set the canvas window
 * (parked off-screen / closed), the move-FX callback, and wire the buttons. The
 * canvas is bound to a hardware layer by ui_manager. Keep the panel visible+enabled
 * so Legato renders it continuously, and start closed: the touch pick rect follows
 * the canvas window position (LE_DRIVER_LAYER_MODE), so a closed (off-screen) nav
 * can't intercept dashboard touches, and the off-screen X seeds the slide-in. */
void Nav_Setup(void)
{
    Marvin_PANEL_NAVIGATION->fn->setEnabled(Marvin_PANEL_NAVIGATION, LE_TRUE);
    Marvin_PANEL_NAVIGATION->fn->setVisible(Marvin_PANEL_NAVIGATION, LE_TRUE);

    gfxcSetWindowSize(CANVAS_NAV, NAV_W, NAV_H);
    gfxcSetWindowPosition(CANVAS_NAV, NAV_CLOSED_X, 0);
    gfxcSetEffectsCallback(CANVAS_NAV, nav_fx_done, NULL);

    nav_buttons_init();
}

/* Hamburger on the dashboard (BASE) toggles the nav drawer. */
void event_Marvin_BUTTON_SYSYEM_NAVIGATION_OnPressed(leButtonWidget* btn)
{
    (void)btn;
    if (s_nav_open) { nav_close(); } else { nav_open(); }
}
