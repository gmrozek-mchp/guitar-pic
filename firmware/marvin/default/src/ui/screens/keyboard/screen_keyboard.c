#include "ui/screens/keyboard/screen_keyboard.h"

#include <stdio.h>
#include <string.h>

#include "ui/ui_manager.h"   /* CANVAS_KEYBOARD, BASE_W, BASE_H, UiManager_CloseKeyboard */
#include "ui/widgets/button_aa/widget_button_aa.h"
#include "ui/widgets/panel_aa/widget_panel_aa.h"
#include "util/legato_utf8.h"

#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/string/legato_fixedstring.h"
#include "gfx/legato/widget/button/legato_widget_button.h"
#include "gfx/legato/widget/label/legato_widget_label.h"
#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"                  /* DejaVu fonts */
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* Marvin_PANEL_KEYBOARD */

/* ── geometry ─────────────────────────────────────────────────────────────────
 * The dialog is the 1060x560 layer-5 panel MGS authored, centered on the 1280x800
 * display by the canvas window. All child coordinates below are panel-relative. */
#define KBD_W   1060
#define KBD_H    560
#define KBD_X   ((int)((BASE_W - KBD_W) / 2u))   /* 110 */
#define KBD_Y   ((int)((BASE_H - KBD_H) / 2u))   /* 120 */

#define PAD        24
#define CONTENT_X  PAD
#define CONTENT_W  (KBD_W - 2 * PAD)   /* 1012 */

#define FIELD_Y    64
#define FIELD_H    92

#define KEY_H      84
#define KEY_GAP    10
#define ROW_GAP    12
#define ROW1_Y     176               /* rows at 176 / 272 / 368 / 464           */
#define ROW_Y(r)   (ROW1_Y + (r) * (KEY_H + ROW_GAP))
#define LETTER_W   92                /* 10*92 + 9*10 = 1010, centered in 1012    */
#define KEY_RADIUS 12

/* UTF-8 for the two glyphs added to DejaVuSansMonoBold_40 (check + backspace). */
#define GLYPH_CHECK "\xE2\x9C\x93"   /* U+2713 */
#define GLYPH_BKSP  "\xE2\x8C\xAB"   /* U+232B */

/* ── edit state ───────────────────────────────────────────────────────────────
 * KBD_MAX bounds the buffer; a session caps its own max at or below this. Keep in
 * step with results.c s_player[] / results_score_t.player[] so a name never
 * truncates on save/readback. */
#define KBD_MAX  32

static char               s_text[KBD_MAX + 1];
static uint32_t           s_len;
static uint32_t           s_maxlen = KBD_MAX;
static keyboard_commit_fn s_commit;

/* ── key table ────────────────────────────────────────────────────────────────
 * Each built key records its widget + what it does, so one shared release handler
 * dispatches by looking the pressed widget up here. */
typedef enum { KIND_CHAR, KIND_SPACE, KIND_BACKSPACE, KIND_CLEAR, KIND_OK, KIND_CANCEL } key_kind_t;

typedef struct { leButtonWidget *btn; uint8_t kind; char ch; } kbd_key_t;

#define MAX_KEYS  40

static kbd_key_t     s_keys[MAX_KEYS];
static unsigned int  s_nkeys;

/* Per-key caption strings (leChar code points; each carries its own font). */
static leChar        s_cap_buf[MAX_KEYS][8];
static leFixedString s_cap_str[MAX_KEYS];

/* Title / entry-text / counter label strings. */
static leChar        s_title_buf[32];
static leFixedString s_title_str;
static leChar        s_entry_buf[KBD_MAX + 2];
static leFixedString s_entry_str;
static leLabelWidget *s_entry_label;   /* explicit invalidate: an empty setString won't clear old glyphs */
static leChar        s_count_buf[12];
static leFixedString s_count_str;
static leLabelWidget *s_count_label;

/* ── label / key builders ─────────────────────────────────────────────────────*/

static void set_font(leFixedString *fs, const leFont *font)
{
    leString *s = (leString *)fs;
    s->fn->setFont(s, (leFont *)font);
}

static leLabelWidget *add_label(int x, int y, int w, int h, leFixedString *fs, leChar *buf,
                                uint32_t cap, const leFont *font, const leScheme *scheme,
                                leHAlignment ha, leBool opaque)
{
    leLabelWidget *l = leLabelWidget_New();
    if (l == NULL) { return NULL; }

    l->fn->setPosition(l, x, y);
    l->fn->setSize(l, w, h);
    l->fn->setScheme(l, scheme);
    /* Opaque (FILL, scheme BASE) for editable fields so a repaint erases old glyphs
     * — a transparent label can't clear itself when the text shrinks/empties, since
     * Legato won't repaint the opaque backdrop behind it. FILL's base must match the
     * backdrop the label sits on. Static labels stay transparent. */
    l->fn->setBackgroundType(l, opaque ? LE_WIDGET_BACKGROUND_FILL : LE_WIDGET_BACKGROUND_NONE);
    l->fn->setHAlignment(l, ha);
    l->fn->setVAlignment(l, LE_VALIGN_MIDDLE);

    leFixedString_Constructor(fs, buf, cap);
    set_font(fs, font);
    l->fn->setString(l, (leString *)fs);

    Marvin_PANEL_KEYBOARD->fn->addChild(Marvin_PANEL_KEYBOARD, (leWidget *)l);
    return l;
}

static void key_on_release(leButtonWidget *btn);

static void add_key(int x, int y, int w, int h, const char *label, const leFont *font,
                    const leScheme *scheme, key_kind_t kind, char ch)
{
    leButtonWidget *b;
    unsigned int    i;

    if (s_nkeys >= MAX_KEYS) { return; }
    b = leButtonWidget_New();
    if (b == NULL) { return; }
    i = s_nkeys;

    b->fn->setPosition(b, x, y);
    b->fn->setSize(b, w, h);
    b->fn->setScheme(b, scheme);
    b->fn->setBackgroundType(b, LE_WIDGET_BACKGROUND_FILL);
    b->fn->setBorderType(b, LE_WIDGET_BORDER_NONE);

    leFixedString_Constructor(&s_cap_str[i], s_cap_buf[i], sizeof s_cap_buf[i] / sizeof s_cap_buf[i][0]);
    set_font(&s_cap_str[i], font);
    (void)lestring_set_utf8((leString *)&s_cap_str[i], label);
    b->fn->setString(b, (leString *)&s_cap_str[i]);

    b->fn->setCornerRadius(b, KEY_RADIUS);
    ButtonAA_Enable(b);
    b->fn->setReleasedEventCallback(b, key_on_release);

    Marvin_PANEL_KEYBOARD->fn->addChild(Marvin_PANEL_KEYBOARD, (leWidget *)b);

    s_keys[i].btn = b;  s_keys[i].kind = (uint8_t)kind;  s_keys[i].ch = ch;
    s_nkeys++;
}

/* Lay one letter row (each key a KIND_CHAR of its letter), centered then shifted by
 * `x_off` px so the rows stagger like a real keyboard. */
static void add_letter_row(const char *letters, int y, int x_off)
{
    int n     = (int)strlen(letters);
    int total = n * LETTER_W + (n - 1) * KEY_GAP;
    int x     = CONTENT_X + (CONTENT_W - total) / 2 + x_off;
    int i;

    for (i = 0; i < n; i++)
    {
        char cap[2] = { letters[i], '\0' };
        add_key(x, y, LETTER_W, KEY_H, cap, (const leFont *)&DejaVuSansMonoBold_40,
                &SCHEME_BUTTON_MODE, KIND_CHAR, letters[i]);
        x += LETTER_W + KEY_GAP;
    }
}

/* ── entry-text refresh (repaints only the two labels) ────────────────────────*/

static void refresh_entry(void)
{
    /* leLabelWidget skips its paint entirely for an empty string, so clearing to ""
     * would leave the old glyphs until the next keypress. Render a single space when
     * empty: non-empty forces the opaque label to fill its whole rect (erasing the
     * old text) while the space itself draws blank. */
    (void)lestring_set_utf8((leString *)&s_entry_str, (s_text[0] != '\0') ? s_text : " ");
    if (s_entry_label != NULL) { s_entry_label->fn->invalidate(s_entry_label); }
}

static void refresh_count(void)
{
    char tmp[12];
    (void)snprintf(tmp, sizeof tmp, "%u/%u", (unsigned)s_len, (unsigned)s_maxlen);
    (void)lestring_set_utf8((leString *)&s_count_str, tmp);
    if (s_count_label != NULL) { s_count_label->fn->invalidate(s_count_label); }
}

static void append_char(char c)
{
    if (s_len >= s_maxlen) { return; }
    s_text[s_len++] = c;
    s_text[s_len]   = '\0';
    refresh_entry();
    refresh_count();
}

static void backspace(void)
{
    if (s_len == 0u) { return; }
    s_text[--s_len] = '\0';
    refresh_entry();
    refresh_count();
}

static void clear_all(void)
{
    if (s_len == 0u) { return; }
    s_len     = 0u;
    s_text[0] = '\0';
    refresh_entry();
    refresh_count();
}

static void commit_and_close(void)
{
    keyboard_commit_fn cb = s_commit;
    if (cb != NULL) { cb(s_text); }
    UiManager_CloseKeyboard();
}

static void key_on_release(leButtonWidget *btn)
{
    unsigned int i;

    for (i = 0u; i < s_nkeys; i++)
    {
        if (s_keys[i].btn != btn) { continue; }
        switch ((key_kind_t)s_keys[i].kind)
        {
            case KIND_CHAR:      append_char(s_keys[i].ch); break;
            case KIND_SPACE:     append_char(' ');          break;
            case KIND_BACKSPACE: backspace();               break;
            case KIND_CLEAR:     clear_all();               break;
            case KIND_OK:        commit_and_close();        break;
            case KIND_CANCEL:    UiManager_CloseKeyboard(); break;
            default: break;
        }
        return;
    }
}

/* ── build ────────────────────────────────────────────────────────────────────*/

void ScreenKeyboard_InitSurface(void)
{
    static uint16_t __attribute__((section(".region_nocache"), aligned(32)))
        s_fb[KBD_W * KBD_H];
    gfxcSetPixelBuffer(CANVAS_KEYBOARD, KBD_W, KBD_H, GFX_COLOR_MODE_RGB_565, s_fb);
}

void ScreenKeyboard_Setup(void)
{
    leWidget *box;

    /* Center the layer-5 canvas on the display; the surface is painted once at
     * boot (ui_manager paint_all_screens_once), so no invalidate here. */
    gfxcSetWindowSize(CANVAS_KEYBOARD, KBD_W, KBD_H);
    gfxcSetWindowPosition(CANVAS_KEYBOARD, KBD_X, KBD_Y);

    /* Opaque scrim: the MGS panel already carries SCHEME_PANEL_GRAY_18181B; make it
     * fill so the whole dialog is drawn (it starts as a plain unfilled leWidget). */
    Marvin_PANEL_KEYBOARD->fn->setBackgroundType(Marvin_PANEL_KEYBOARD, LE_WIDGET_BACKGROUND_FILL);

    /* Title (top-left). Text is set per-session in Prepare(). */
    (void)add_label(PAD, 16, 700, 32, &s_title_str, s_title_buf,
                    sizeof s_title_buf / sizeof s_title_buf[0],
                    (const leFont *)&DejaVuSansMono_20, &SCHEME_TEXT_GRAY_A1A1AA, LE_HALIGN_LEFT, LE_FALSE);

    /* Text-field box: a rounded, bordered child panel over the dialog gray. */
    box = leWidget_New();
    if (box != NULL)
    {
        box->fn->setPosition(box, CONTENT_X, FIELD_Y);
        box->fn->setSize(box, CONTENT_W, FIELD_H);
        box->fn->setScheme(box, &SCHEME_BUTTON_MODE);
        box->fn->setBackgroundType(box, LE_WIDGET_BACKGROUND_FILL);
        box->fn->setBorderType(box, LE_WIDGET_BORDER_LINE);
        box->fn->setCornerRadius(box, 10);
        PanelAA_Enable(box);
        Marvin_PANEL_KEYBOARD->fn->addChild(Marvin_PANEL_KEYBOARD, box);
    }

    /* Entry text (left) + counter (right), painted over the box. Their schemes'
     * BASE (SCHEME_BUTTON_MODE) matches the box fill so the glyph AA blends clean. */
    /* Inset 1px top+bottom so the opaque fill doesn't cover the box's top/bottom border. */
    s_entry_label = add_label(CONTENT_X + 22, FIELD_Y + 1, CONTENT_W - 190, FIELD_H - 2, &s_entry_str, s_entry_buf,
                    sizeof s_entry_buf / sizeof s_entry_buf[0],
                    (const leFont *)&DejaVuSansMonoBold_40, &SCHEME_BUTTON_MODE, LE_HALIGN_LEFT, LE_TRUE);
    s_count_label = add_label(CONTENT_X + CONTENT_W - 150, FIELD_Y + 1, 128, FIELD_H - 2, &s_count_str, s_count_buf,
                    sizeof s_count_buf / sizeof s_count_buf[0],
                    (const leFont *)&DejaVuSansMono_20, &SCHEME_BUTTON_MODE, LE_HALIGN_RIGHT, LE_TRUE);

    /* Close (X), top-right. Dismisses without committing. */
    add_key(KBD_W - PAD - 44, 12, 44, 44, "X", (const leFont *)&DejaVuSansMonoBold_24,
            &SCHEME_BUTTON_MODE, KIND_CANCEL, 0);

    /* Three QWERTY letter rows. The Z row shifts half a key (½·(key+gap)) left of
     * center so the rows stagger like a physical keyboard. */
    add_letter_row("QWERTYUIOP", ROW_Y(0), 0);
    add_letter_row("ASDFGHJKL",  ROW_Y(1), 0);
    add_letter_row("ZXCVBNM",    ROW_Y(2), -(LETTER_W + KEY_GAP) / 2);

    /* Bottom row: CLEAR | SPACE (wide) | backspace | OK (green). Widths (150/520/
     * 110/200) + three 10px gaps sum to 1010, centered like the letter rows
     * (start at CONTENT_X + 1 = 25). */
    add_key(25,  ROW_Y(3), 150, KEY_H, "CLEAR", (const leFont *)&DejaVuSansMonoBold_24,
            &SCHEME_BUTTON_MODE, KIND_CLEAR, 0);
    add_key(185, ROW_Y(3), 520, KEY_H, "SPACE", (const leFont *)&DejaVuSansMonoBold_24,
            &SCHEME_BUTTON_MODE, KIND_SPACE, 0);
    add_key(715, ROW_Y(3), 110, KEY_H, GLYPH_BKSP, (const leFont *)&DejaVuSansMonoBold_40,
            &SCHEME_BUTTON_MODE, KIND_BACKSPACE, 0);
    add_key(835, ROW_Y(3), 200, KEY_H, GLYPH_CHECK " OK", (const leFont *)&DejaVuSansMonoBold_40,
            &SCHEME_BUTTON_EASY, KIND_OK, 0);

    ScreenKeyboard_SetInput(false);   /* built but not shown */
}

void ScreenKeyboard_Prepare(const char *title, const char *initial, uint32_t maxlen,
                            keyboard_commit_fn commit)
{
    s_commit = commit;
    s_maxlen = (maxlen == 0u || maxlen > KBD_MAX) ? KBD_MAX : maxlen;

    s_len = 0u;
    if (initial != NULL)
    {
        while (initial[s_len] != '\0' && s_len < s_maxlen)
        {
            s_text[s_len] = initial[s_len];
            s_len++;
        }
    }
    s_text[s_len] = '\0';

    (void)lestring_set_utf8((leString *)&s_title_str, (title != NULL) ? title : "");
    refresh_entry();
    refresh_count();
}

void ScreenKeyboard_SetInput(bool on)
{
    if (on) { Marvin_PANEL_KEYBOARD->flags |=  LE_WIDGET_ENABLED; }
    else    { Marvin_PANEL_KEYBOARD->flags &= ~LE_WIDGET_ENABLED; }
}
