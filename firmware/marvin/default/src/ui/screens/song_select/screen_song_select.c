#include "ui/screens/song_select/screen_song_select.h"

#include "ui/ui_manager.h"   /* CANVAS_SONGSEL, BASE_W, BASE_H */

#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
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

void ScreenSongSelect_Setup(void)
{
    /* Center the dialog. The root is already on Legato layer 2 (built by MGS); the
     * canvas window positions that layer's pixels on the display. Force a full
     * repaint so the panel is complete in the buffer before the compositor shows
     * the canvas. */
    gfxcSetWindowSize(CANVAS_SONGSEL, SONGSEL_W, SONGSEL_H);
    gfxcSetWindowPosition(CANVAS_SONGSEL, SONGSEL_X, SONGSEL_Y);

    Marvin_PANEL_SONG_SELECT->fn->invalidate(Marvin_PANEL_SONG_SELECT);
}
