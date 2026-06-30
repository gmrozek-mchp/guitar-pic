#include "ui/screens/song_select/screen_song_select.h"

#include "ui/ui_manager.h"   /* CANVAS_SONGSEL, BASE_W, BASE_H */

#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* song-select widgets */

/* The song/mode-select dialog is layer 2 of the Marvin screen; MGS sizes that
 * layer's root to the dialog (1100x660) and renders it into CANVAS_SONGSEL. The
 * canvas is smaller than the panel, so the canvas window places it centered on the
 * 1280x800 display. */
#define SONGSEL_W   1100u
#define SONGSEL_H    660u
#define SONGSEL_X   ((int)((BASE_W - SONGSEL_W) / 2u))   /* 90  */
#define SONGSEL_Y   ((int)((BASE_H - SONGSEL_H) / 2u))   /* 70  */

#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

static uint16_t FB_NOCACHE s_fb_songsel[SONGSEL_W * SONGSEL_H];

void ScreenSongSelect_InitSurface(void)
{
    gfxcSetPixelBuffer(CANVAS_SONGSEL, SONGSEL_W, SONGSEL_H, GFX_COLOR_MODE_RGB_565, s_fb_songsel);
}

/* ── difficulty + mode radio groups ──────────────────────────────────────────
 * The four DIFFICULTY buttons are one single-select group, the three MODE buttons
 * another. MGS builds them as plain push buttons all sharing one unselected scheme
 * (SCHEME_BUTTON_DIFFICULTY / SCHEME_BUTTON_MODE); the radio behaviour is driven
 * here by swapping schemes on release. Difficulty's selected look is per-button
 * (Easy→green … Expert→red); mode uses one shared selected scheme. The selection
 * persists in s_difficulty / s_mode — the source of truth the repaint reads. */
#define DIFFICULTY_COUNT    4u
#define MODE_COUNT          3u
#define DIFFICULTY_DEFAULT  0u   /* Easy      */
#define MODE_DEFAULT        0u   /* 1P robot  */

static unsigned int s_difficulty = DIFFICULTY_DEFAULT;
static unsigned int s_mode       = MODE_DEFAULT;

static leButtonWidget *difficulty_button(unsigned int i)
{
    switch (i)
    {
        case 0:  return Marvin_BUTTON_SONG_SELECT_EASY_0_0;
        case 1:  return Marvin_BUTTON_SONG_SELECT_MEDIUM_0_0;
        case 2:  return Marvin_BUTTON_SONG_SELECT_HARD_0_0;
        default: return Marvin_BUTTON_SONG_SELECT_EXPERT_0_0;
    }
}

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

static leButtonWidget *mode_button(unsigned int i)
{
    switch (i)
    {
        case 0:  return Marvin_BUTTON_SONG_SELECT_1P_ROBOT_0_0;
        case 1:  return Marvin_BUTTON_SONG_SELECT_1P_HUMAN_0_0;
        default: return Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN_0_0;
    }
}

/* Paint the active button in each group with its selected scheme, the rest with
 * the group's shared unselected scheme. */
static void difficulty_repaint(void)
{
    unsigned int i;
    for (i = 0u; i < DIFFICULTY_COUNT; i++)
    {
        leButtonWidget *b = difficulty_button(i);
        b->fn->setScheme(b, (i == s_difficulty) ? difficulty_selected_scheme(i)
                                                 : &SCHEME_BUTTON_DIFFICULTY);
    }
}

static void mode_repaint(void)
{
    unsigned int i;
    for (i = 0u; i < MODE_COUNT; i++)
    {
        leButtonWidget *b = mode_button(i);
        b->fn->setScheme(b, (i == s_mode) ? &SCHEME_BUTTON_MODE_SELECTED
                                          : &SCHEME_BUTTON_MODE);
    }
}

/* Released-event sinks: select the pressed entry and repaint its group. Re-picking
 * the active entry is a no-op (radio: no deselect). */
static void difficulty_on_release(leButtonWidget *btn)
{
    unsigned int i;
    for (i = 0u; i < DIFFICULTY_COUNT; i++)
    {
        if (btn == difficulty_button(i)) { s_difficulty = i; difficulty_repaint(); return; }
    }
}

static void mode_on_release(leButtonWidget *btn)
{
    unsigned int i;
    for (i = 0u; i < MODE_COUNT; i++)
    {
        if (btn == mode_button(i)) { s_mode = i; mode_repaint(); return; }
    }
}

static void radio_groups_init(void)
{
    unsigned int i;

    for (i = 0u; i < DIFFICULTY_COUNT; i++)
    {
        difficulty_button(i)->fn->setReleasedEventCallback(difficulty_button(i), difficulty_on_release);
    }
    for (i = 0u; i < MODE_COUNT; i++)
    {
        mode_button(i)->fn->setReleasedEventCallback(mode_button(i), mode_on_release);
    }

    difficulty_repaint();   /* apply the default selections */
    mode_repaint();
}

void ScreenSongSelect_Setup(void)
{
    /* Center the dialog. The root is already on Legato layer 2 (built by MGS); the
     * canvas window positions that layer's pixels on the display. Force a full
     * repaint so the panel is complete in the buffer before the compositor shows
     * the canvas. */
    gfxcSetWindowSize(CANVAS_SONGSEL, SONGSEL_W, SONGSEL_H);
    gfxcSetWindowPosition(CANVAS_SONGSEL, SONGSEL_X, SONGSEL_Y);

    radio_groups_init();

    Marvin_PANEL_SONG_SELECT->fn->invalidate(Marvin_PANEL_SONG_SELECT);
}
