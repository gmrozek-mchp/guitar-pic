#include "ui/screens/wiimotes/screen_wiimotes.h"

#include <stdint.h>
#include <stdbool.h>

#include "ui/ui_manager.h"   /* CANVAS_WIIMOTES, BASE_W, BASE_H */

#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* wiimotes widgets */

#include "net/fauxmote/fauxmote_link.h"
#include "net/fauxmote/mf_proto.h"

/* Wiimotes surface, non-cached so the 2D engine and LCDC DMA read CPU-rendered
 * pixels coherently; 32-byte aligned. RGB565 to match the layer's color mode. */
#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

static uint16_t FB_NOCACHE s_fb[BASE_W * BASE_H];

/* Manual-override control state, sent directly to fauxmote. Frets/strum/± form the
 * GUITAR slice (whammy fixed at rest — no slider handle yet); D-pad + core buttons
 * form the WIIMOTE nav slice (stick centered). Momentary: a button holds its bit
 * only while pressed. While this screen is shown it owns fauxmote (override on), so
 * the gameplay mirror can't fight these values. */
static uint8_t s_g_mask;    /* MF_G_*  */
static uint8_t s_g_aux;     /* MF_AUX_* */
static uint8_t s_nav_core;  /* MF_W_A/B/ONE/TWO/HOME */
static uint8_t s_nav_dpad;  /* MF_W_UP/DOWN/LEFT/RIGHT */

/* Which slice/field a control button drives. */
typedef enum { FLD_GMASK, FLD_GAUX, FLD_NCORE, FLD_NDPAD } ctrl_field_t;

typedef struct
{
    leButtonWidget **slot;   /* address of the MGS-assigned widget global */
    ctrl_field_t     field;
    uint8_t          bit;
} ctrl_map_t;

/* Every control button on the screen, mapped to its fauxmote bit. The widget
 * globals are assigned by screenShow_Marvin, so the table holds their addresses and
 * dereferences at dispatch time. */
static const ctrl_map_t s_ctrls[] =
{
    { &Marvin_BUTTON_WIIMOTES_ROBOT_FRET_GREEN,  FLD_GMASK, MF_G_GREEN      },
    { &Marvin_BUTTON_WIIMOTES_ROBOT_FRET_RED,    FLD_GMASK, MF_G_RED        },
    { &Marvin_BUTTON_WIIMOTES_ROBOT_FRET_YELLOW, FLD_GMASK, MF_G_YELLOW     },
    { &Marvin_BUTTON_WIIMOTES_ROBOT_FRET_BLUE,   FLD_GMASK, MF_G_BLUE       },
    { &Marvin_BUTTON_WIIMOTES_ROBOT_FRET_ORANGE, FLD_GMASK, MF_G_ORANGE     },
    { &Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP,    FLD_GMASK, MF_G_STRUM_UP   },
    { &Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN,  FLD_GMASK, MF_G_STRUM_DOWN },
    { &Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_PLUS,  FLD_GAUX, MF_AUX_PLUS     },
    { &Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_MINUS, FLD_GAUX, MF_AUX_MINUS    },
    { &Marvin_BUTTON_WIIMOTES_ROBOT_A,    FLD_NCORE, MF_W_A    },
    { &Marvin_BUTTON_WIIMOTES_ROBOT_B,    FLD_NCORE, MF_W_B    },
    { &Marvin_BUTTON_WIIMOTES_ROBOT_ONE,  FLD_NCORE, MF_W_ONE  },
    { &Marvin_BUTTON_WIIMOTES_ROBOT_TWO,  FLD_NCORE, MF_W_TWO  },
    { &Marvin_BUTTON_WIIMOTES_ROBOT_HOME, FLD_NCORE, MF_W_HOME },
    { &Marvin_BUTTON_WIIMOTES_ROBOT_UP,    FLD_NDPAD, MF_W_UP    },
    { &Marvin_BUTTON_WIIMOTES_ROBOT_DOWN,  FLD_NDPAD, MF_W_DOWN  },
    { &Marvin_BUTTON_WIIMOTES_ROBOT_LEFT,  FLD_NDPAD, MF_W_LEFT  },
    { &Marvin_BUTTON_WIIMOTES_ROBOT_RIGHT, FLD_NDPAD, MF_W_RIGHT },
};

#define CTRL_COUNT  (sizeof(s_ctrls) / sizeof(s_ctrls[0]))

static void send_guitar(void)
{
    Fauxmote_SendGuitar(s_g_mask, MF_WHAMMY_REST, s_g_aux);
}

static void send_nav(void)
{
    Fauxmote_SendNav(s_nav_core, s_nav_dpad, MF_STICK_CENTER, MF_STICK_CENTER);
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

    for (i = 0u; i < CTRL_COUNT; i++)
    {
        if (*s_ctrls[i].slot == btn) { control_apply(&s_ctrls[i], pressed); return; }
    }
}

static void control_on_press(leButtonWidget *btn)   { control_dispatch(btn, true); }
static void control_on_release(leButtonWidget *btn) { control_dispatch(btn, false); }

/* Register momentary press/release on every control button (runtime-wired here — the
 * MGS screen wires no button events). */
static void controls_init(void)
{
    unsigned int i;

    for (i = 0u; i < CTRL_COUNT; i++)
    {
        leButtonWidget *b = *s_ctrls[i].slot;
        b->fn->setPressedEventCallback(b, control_on_press);
        b->fn->setReleasedEventCallback(b, control_on_release);
    }
}

void ScreenWiimotes_InitSurface(void)
{
    gfxcSetPixelBuffer(CANVAS_WIIMOTES, BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565, s_fb);
}

void ScreenWiimotes_Setup(void)
{
    /* Full-screen at the origin. The root is on Legato layer 4 (built by MGS); the
     * canvas window positions that layer's pixels on the display. */
    gfxcSetWindowPosition(CANVAS_WIIMOTES, 0, 0);
    gfxcSetWindowSize(CANVAS_WIIMOTES, BASE_W, BASE_H);

    controls_init();

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

/* Take/relinquish the fauxmote link as this base view is shown/hidden. While shown
 * the screen owns the GUITAR slice (override blocks the gameplay mirror) and drives
 * fauxmote directly from its buttons. On hide, release every input to safe defaults
 * *before* dropping the override so a dead touch never leaves a note held, then hand
 * fauxmote back to the gameplay mirror. */
void ScreenWiimotes_SetShown(bool shown)
{
    s_g_mask   = 0u;
    s_g_aux    = 0u;
    s_nav_core = 0u;
    s_nav_dpad = 0u;

    if (shown)
    {
        Fauxmote_SetOverride(true);
        send_guitar();
        send_nav();
    }
    else
    {
        send_guitar();
        send_nav();
        Fauxmote_SetOverride(false);
    }
}
