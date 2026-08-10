#include "ui/screens/role/screen_role.h"

#include <string.h>

#include "FreeRTOS.h"        /* configASSERT */

#include "ui/ui_manager.h"   /* CANVAS_ROLE, BASE_W/H, MODAL_R, UiManager_CloseRole */
#include "ui/widgets/button_aa/widget_button_aa.h"
#include "util/legato_utf8.h"

#include "ui/gfx/ui_surface.h"
#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/string/legato_fixedstring.h"
#include "gfx/legato/string/legato_tablestring.h"
#include "gfx/legato/widget/button/legato_widget_button.h"
#include "gfx/legato/widget/label/legato_widget_label.h"
#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* Marvin_PANEL_ROLE */

/* ── geometry ─────────────────────────────────────────────────────────────────
 * The mockup's RoleDialog: a 480px card, p-8 padding, a centred "ARE YOU <name>"
 * header, then two full-width buttons with gap-3 between them. Height is the sum of
 * those rather than a round number, so the card wraps its content the way the mockup's
 * flex column does. Child coordinates are panel-relative. */
#define ROLE_W    480
#define ROLE_H    268
/* X rounded down to the canvas window's 4px grid, for the reason screen_keyboard.c
 * documents: the BASE discard rect and the corner cut both derive from it. */
#define ROLE_X    CANVAS_X_ALIGN((BASE_W - ROLE_W) / 2u)   /* 400 */
#define ROLE_Y    ((int)((BASE_H - ROLE_H) / 2u))          /* 266 */

#define PAD       32                                   /* p-8 */
#define INNER_W   (ROLE_W - 2 * PAD)                   /* 416 */

#define ARE_Y     32                                   /* text-sm, tracking-widest */
#define ARE_H     18
#define NAME_Y    52                                   /* text-2xl bold */
#define NAME_H    32

#define BTN_H     58                                   /* py-5 around text-lg */
#define BTN_GAP   12                                   /* gap-3 */
#define BTN_R     12                                   /* rounded-xl */
#define BTN1_Y    (NAME_Y + NAME_H + 24)               /* gap-6 under the header → 108 */
#define BTN2_Y    (BTN1_Y + BTN_H + BTN_GAP)           /* 178 */

/* Close (X), top-right — same 44px square, literal caption and scheme the keyboard's
 * cancel uses, so the two halves of the prompt dismiss identically. */
#define X_D       44
#define X_XPOS    (ROLE_W - PAD - X_D)
#define X_YPOS    12

/* ── state ────────────────────────────────────────────────────────────────────*/

#define ROLE_NAME_MAX  32   /* keyboard's KBD_MAX; the name arrives from there */

static role_commit_fn s_commit;

/* The name is whatever was typed, so it owns a runtime string rather than a design one.
 * Invalidated explicitly on Prepare: setString alone does not clear the previous glyphs. */
static leChar         s_name_buf[ROLE_NAME_MAX + 1];
static leFixedString  s_name_str;
static leLabelWidget  s_name_lbl;

/* The X's caption. A single glyph is not translatable text, so it is a runtime string too
 * — the same call the keyboard's own X makes. */
static leChar         s_x_buf[2];
static leFixedString  s_x_str;

/* One shared release handler dispatches by looking the pressed widget up here, the
 * keyboard's idiom. CANCEL is a button like the others so there is a single exit path. */
typedef enum { ROLE_KEY_CLIENT, ROLE_KEY_EMPLOYEE, ROLE_KEY_CANCEL } role_key_t;

#define ROLE_KEYS  3

static leButtonWidget *s_btn[ROLE_KEYS];
static uint8_t         s_kind[ROLE_KEYS];
static unsigned        s_nbtn;

static leButtonWidget s_btn_pool[ROLE_KEYS];
static leTableString  s_btn_str[ROLE_KEYS];
static leLabelWidget  s_cap_pool[2];
static leTableString  s_cap_str[2];
static unsigned       s_ncap;

/* ── build helpers ────────────────────────────────────────────────────────────*/

/* Point a fixed string at its buffer and give it a font, which a runtime string needs
 * explicitly — there is no design binding to inherit one from. */
static void set_runtime_string(leFixedString *fs, leChar *buf, uint32_t cap,
                               const leFont *font)
{
    leFixedString_Constructor(fs, buf, cap);
    leString *s = (leString *)fs;
    s->fn->setFont(s, (leFont *)font);
}

static leLabelWidget *add_cap(int x, int y, int w, int h, uint32_t string_id,
                              const leScheme *scheme, leHAlignment ha)
{
    configASSERT(s_ncap < (sizeof s_cap_pool / sizeof s_cap_pool[0]));

    leLabelWidget *l = &s_cap_pool[s_ncap];
    leLabelWidget_Constructor(l);
    l->fn->setPosition(l, x, y);
    l->fn->setSize(l, w, h);
    l->fn->setScheme(l, scheme);
    l->fn->setBackgroundType(l, LE_WIDGET_BACKGROUND_NONE);
    l->fn->setHAlignment(l, ha);
    l->fn->setVAlignment(l, LE_VALIGN_MIDDLE);

    leTableString_Constructor(&s_cap_str[s_ncap], string_id);
    l->fn->setString(l, (leString *)&s_cap_str[s_ncap]);
    s_ncap++;

    Marvin_PANEL_ROLE->fn->addChild(Marvin_PANEL_ROLE, (leWidget *)l);
    return l;
}

static void role_on_release(leButtonWidget *btn);

/* A design-string button. `string_id` 0 leaves the caption unset — the X sets its own
 * runtime one, since a single glyph is not translatable text. */
static leButtonWidget *add_button(int x, int y, int w, int h, uint32_t string_id,
                                  const leScheme *scheme, uint32_t radius, role_key_t kind)
{
    configASSERT(s_nbtn < ROLE_KEYS);
    unsigned i = s_nbtn++;

    leButtonWidget *b = &s_btn_pool[i];
    leButtonWidget_Constructor(b);
    b->fn->setPosition(b, x, y);
    b->fn->setSize(b, w, h);
    b->fn->setScheme(b, scheme);
    b->fn->setBackgroundType(b, LE_WIDGET_BACKGROUND_FILL);
    b->fn->setBorderType(b, LE_WIDGET_BORDER_LINE);
    b->fn->setCornerRadius(b, radius);
    b->fn->setPressedOffset(b, 0);
    b->fn->setReleasedEventCallback(b, role_on_release);

    if (string_id != 0u)
    {
        leTableString_Constructor(&s_btn_str[i], string_id);
        b->fn->setString(b, (leString *)&s_btn_str[i]);
    }

    ButtonAA_Enable(b);
    Marvin_PANEL_ROLE->fn->addChild(Marvin_PANEL_ROLE, (leWidget *)b);

    s_btn[i]  = b;
    s_kind[i] = (uint8_t)kind;
    return b;
}

/* ── dispatch ─────────────────────────────────────────────────────────────────*/

/* Close first, then call back: the callback starts a run, and the dialog must already be
 * off the panel by then — the keyboard's ordering, and for the same reason. `s_commit` is
 * cleared before the call so a session can only ever fire once. */
static void role_close(bool answered, results_affil_t affil)
{
    role_commit_fn commit = s_commit;

    s_commit = NULL;
    UiManager_CloseRole();

    if (answered && commit != NULL) { commit(affil); }
}

static void role_on_release(leButtonWidget *btn)
{
    for (unsigned i = 0u; i < s_nbtn; i++)
    {
        if (s_btn[i] != btn) { continue; }

        switch ((role_key_t)s_kind[i])
        {
            case ROLE_KEY_CLIENT:   role_close(true,  RESULTS_AFFIL_CLIENT);   break;
            case ROLE_KEY_EMPLOYEE: role_close(true,  RESULTS_AFFIL_EMPLOYEE); break;
            case ROLE_KEY_CANCEL:   role_close(false, RESULTS_AFFIL_EMPLOYEE); break;
            default: break;
        }
        return;
    }
}

/* ── build ────────────────────────────────────────────────────────────────────*/

void ScreenRole_InitSurface(void)
{
    static uint16_t __attribute__((section(".region_nocache"), aligned(32)))
        s_fb[ROLE_W * ROLE_H];
    UiSurface_Set(CANVAS_ROLE, ROLE_W, ROLE_H, GFX_COLOR_MODE_RGB_565, s_fb);
}

void ScreenRole_Setup(void)
{
    gfxcSetWindowSize(CANVAS_ROLE, ROLE_W, ROLE_H);
    gfxcSetWindowPosition(CANVAS_ROLE, ROLE_X, ROLE_Y);

    /* The layer was cloned from the keyboard's, so it arrives carrying the keyboard's
     * 1060x560 design size. Resize it to the card: the root widget's rect is the clip
     * region, and one larger than the surface would let damage run off the end of it. */
    Marvin_PANEL_ROLE->fn->setSize(Marvin_PANEL_ROLE, ROLE_W, ROLE_H);

    /* Same modal card treatment the keyboard documents: fill it (a bare leWidget starts
     * unfilled), a 1px border the classic skin draws in the scheme's SHADOWDARK, and
     * MODAL_R corners that UiManager_CutModalCorners cuts and fills on open. Every child
     * sits inside PAD, well clear of both the border row and the corner boxes. */
    Marvin_PANEL_ROLE->fn->setBackgroundType(Marvin_PANEL_ROLE, LE_WIDGET_BACKGROUND_FILL);
    Marvin_PANEL_ROLE->fn->setBorderType(Marvin_PANEL_ROLE, LE_WIDGET_BORDER_LINE);
    Marvin_PANEL_ROLE->fn->setCornerRadius(Marvin_PANEL_ROLE, MODAL_R);

    (void)add_cap(PAD, ARE_Y, INNER_W, ARE_H, stringID_ROLE_ARE_YOU,
                  &SCHEME_TEXT_ZINC_500, LE_HALIGN_CENTER);

    leLabelWidget_Constructor(&s_name_lbl);
    s_name_lbl.fn->setPosition(&s_name_lbl, PAD, NAME_Y);
    s_name_lbl.fn->setSize(&s_name_lbl, INNER_W, NAME_H);
    s_name_lbl.fn->setScheme(&s_name_lbl, &SCHEME_TEXT_WHITE);
    s_name_lbl.fn->setBackgroundType(&s_name_lbl, LE_WIDGET_BACKGROUND_NONE);
    s_name_lbl.fn->setHAlignment(&s_name_lbl, LE_HALIGN_CENTER);
    s_name_lbl.fn->setVAlignment(&s_name_lbl, LE_VALIGN_MIDDLE);
    set_runtime_string(&s_name_str, s_name_buf, sizeof s_name_buf / sizeof s_name_buf[0],
                       (const leFont *)&DejaVuSansMonoBold_24);
    s_name_lbl.fn->setString(&s_name_lbl, (leString *)&s_name_str);
    Marvin_PANEL_ROLE->fn->addChild(Marvin_PANEL_ROLE, (leWidget *)&s_name_lbl);

    /* CLIENT / PARTNER first — the common case at a booth, and the order Greg asked for
     * (the mockup lists the employee first). Both rows share SCHEME_TOGGLE_OFF: the
     * mockup's cyan and red are *hover* states, and a touch panel has no hover. */
    (void)add_button(PAD, BTN1_Y, INNER_W, BTN_H, stringID_ROLE_CLIENT_PARTNER,
                     &SCHEME_TOGGLE_OFF, BTN_R, ROLE_KEY_CLIENT);
    (void)add_button(PAD, BTN2_Y, INNER_W, BTN_H, stringID_ROLE_EMPLOYEE,
                     &SCHEME_TOGGLE_OFF, BTN_R, ROLE_KEY_EMPLOYEE);

    leButtonWidget *x = add_button(X_XPOS, X_YPOS, X_D, X_D, 0u,
                                   &SCHEME_BUTTON_MODE, MODAL_R, ROLE_KEY_CANCEL);
    set_runtime_string(&s_x_str, s_x_buf, sizeof s_x_buf / sizeof s_x_buf[0],
                       (const leFont *)&DejaVuSansMonoBold_24);
    (void)lestring_set_utf8((leString *)&s_x_str, "X");
    x->fn->setString(x, (leString *)&s_x_str);

    ScreenRole_SetInput(false);
}

void ScreenRole_Prepare(const char *name, role_commit_fn commit)
{
    s_commit = commit;

    (void)lestring_set_utf8((leString *)&s_name_str,
                            (name != NULL && name[0] != '\0') ? name : "-");
    s_name_lbl.fn->invalidate(&s_name_lbl);
}

void ScreenRole_SetInput(bool on)
{
    if (on) { Marvin_PANEL_ROLE->flags |=  LE_WIDGET_ENABLED; }
    else    { Marvin_PANEL_ROLE->flags &= ~LE_WIDGET_ENABLED; }
}
