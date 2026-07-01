#include "ui/screens/dashboard/screen_dashboard.h"

#include "ui/ui_manager.h"   /* CANVAS_DASH, BASE_W, BASE_H, UiManager_OpenSongSelect */
#include "ui/widgets/button_aa/widget_button_aa.h"
#include "ui/widgets/panel_aa/widget_panel_aa.h"

#include "gfx/canvas/gfx_canvas_api.h"
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
}
