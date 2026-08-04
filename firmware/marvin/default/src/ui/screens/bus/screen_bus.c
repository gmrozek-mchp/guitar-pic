#include "ui/screens/bus/screen_bus.h"

#include <stdint.h>

#include "ui/ui_manager.h"   /* CANVAS_BUS, BASE_W, BASE_H */

#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/string/legato_fixedstring.h"
#include "gfx/legato/widget/label/legato_widget_label.h"
#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"                  /* DejaVu fonts */
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* Marvin_PANEL_BUS */
#include "util/legato_utf8.h"

/* Bus surface, non-cached so the 2D engine and LCDC DMA read CPU-rendered pixels
 * coherently; 32-byte aligned. RGB565 to match the layer's color mode. */
#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

static uint16_t FB_NOCACHE s_fb[BASE_W * BASE_H];

/* Placeholder title, until the tiles + per-node table are built here. */
static leChar        s_title_buf[32];
static leFixedString s_title_str;

void ScreenBus_InitSurface(void)
{
    gfxcSetPixelBuffer(CANVAS_BUS, BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565, s_fb);
}

void ScreenBus_Setup(void)
{
    /* Full-screen at the origin. The root is on Legato layer 6 (built by MGS); the
     * canvas window positions that layer's pixels on the display. */
    gfxcSetWindowPosition(CANVAS_BUS, 0, 0);
    gfxcSetWindowSize(CANVAS_BUS, BASE_W, BASE_H);

    /* Fill the panel so the surface is fully drawn (MGS builds it as a plain widget). */
    Marvin_PANEL_BUS->fn->setBackgroundType(Marvin_PANEL_BUS, LE_WIDGET_BACKGROUND_FILL);

    /* Placeholder label so the scaffold is visibly the bus screen. */
    leLabelWidget *title = leLabelWidget_New();
    if (title != NULL)
    {
        title->fn->setPosition(title, 32, 28);
        title->fn->setSize(title, 800, 32);
        title->fn->setScheme(title, &SCHEME_TEXT_GRAY_A1A1AA);
        title->fn->setBackgroundType(title, LE_WIDGET_BACKGROUND_NONE);
        title->fn->setHAlignment(title, LE_HALIGN_LEFT);
        title->fn->setVAlignment(title, LE_VALIGN_MIDDLE);

        leFixedString_Constructor(&s_title_str, s_title_buf,
                                  sizeof(s_title_buf) / sizeof(s_title_buf[0]));
        ((leString *)&s_title_str)->fn->setFont((leString *)&s_title_str,
                                                (leFont *)&DejaVuSansMono_20);
        (void)lestring_set_utf8((leString *)&s_title_str, "10BASE-T1S BUS");
        title->fn->setString(title, (leString *)&s_title_str);

        Marvin_PANEL_BUS->fn->addChild(Marvin_PANEL_BUS, (leWidget *)title);
    }

    /* Start not shown → gate out of picking (see ScreenBus_SetInput). */
    ScreenBus_SetInput(false);
}

/* Gate the screen's whole subtree in/out of picking without repainting (clearing
 * LE_WIDGET_ENABLED on the background panel; leUtils_PickFromWidget descends only
 * into ENABLED children, the renderer never reads the flag). Same mechanism as the
 * wiimotes screen; ui_manager drives it on base-view show/hide + drawer-modal. */
void ScreenBus_SetInput(bool on)
{
    if (on) { Marvin_PANEL_BUS->flags |=  LE_WIDGET_ENABLED; }
    else    { Marvin_PANEL_BUS->flags &= ~LE_WIDGET_ENABLED; }
}

void ScreenBus_SetShown(bool shown)
{
    (void)shown;   /* placeholder — starts/stops the stats refresh in a follow-up */
}
