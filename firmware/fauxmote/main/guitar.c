#include <string.h>

#include "wiimote_ext.h"
#include "guitar.h"

/* Guitar Hero guitar extension. 6-byte report (buttons active-low, rest = 1):
 *   0: bits7-6 = 1 (GH3 Les Paul ident; 0 = GHWT), bits5-0 stick X
 *   1: bits7-6 = 1 (GH3 ident),                    bits5-0 stick Y
 *   2: touch bar (bits 4-0; GHWT only)  3: whammy (bits 4-0)
 *   4: bit6 strum-down, bit4 minus, bit2 plus (others 1)
 *   5: bit7 orange, bit6 red, bit5 blue, bit4 green, bit3 yellow, bit2 pedal,
 *      bit0 strum-up (bit1 = 1)
 * The GH3 ident bits matter: with them clear, GH3 treats us as a GHWT guitar and
 * ignores the strum bar. See firmware/fauxmote/docs/journal.md. */

static uint8_t s_regs[256];                    /* register bank (ID at 0xfa) */
static uint8_t s_sx = 0x20, s_sy = 0x20;       /* analog stick, centered */
static uint8_t s_tb;                           /* touch bar (GH3 unused) */
static uint8_t s_whammy = 0x10;                /* whammy bar (rest) */
static uint8_t s_btn4 = 0xFF, s_btn5 = 0xFF;   /* button bytes, active-low (rest 0xFF) */

static const struct { const char *name; uint8_t byte; uint8_t mask; } k_btn[] = {
    { "green",   5, 0x10 }, { "red",    5, 0x40 }, { "yellow", 5, 0x08 },
    { "blue",    5, 0x20 }, { "orange", 5, 0x80 }, { "pedal",  5, 0x04 },
    { "strumup", 5, 0x01 }, { "strumdown", 4, 0x40 },
    { "gplus",   4, 0x04 }, { "gminus", 4, 0x10 },
};

static void guitar_build_report(uint8_t *d)
{
    d[0] = 0xC0 | (s_sx & 0x3F);   /* bits7-6 = 1: identify as a GH3 Les Paul guitar */
    d[1] = 0xC0 | (s_sy & 0x3F);
    d[2] = s_tb;
    d[3] = s_whammy;
    d[4] = s_btn4;
    d[5] = s_btn5;
}

static bool guitar_set_button(const char *name, bool pressed)
{
    for (size_t i = 0; i < sizeof(k_btn) / sizeof(k_btn[0]); i++) {
        if (strcmp(name, k_btn[i].name) == 0) {
            uint8_t *b = (k_btn[i].byte == 4) ? &s_btn4 : &s_btn5;
            if (pressed) *b &= (uint8_t)~k_btn[i].mask;   /* active-low: pressed = clear */
            else *b |= k_btn[i].mask;
            return true;
        }
    }
    return false;
}

static void guitar_reset(void)
{
    s_btn4 = s_btn5 = 0xFF;
    s_whammy = 0x10;
}

void Guitar_SetWhammy(uint8_t value)
{
    s_whammy = value & 0x1F;
}

void Guitar_SetStick(uint8_t x, uint8_t y)
{
    s_sx = x & 0x3F;
    s_sy = y & 0x3F;
}

static const wiimote_extension_t s_guitar = {
    .name        = "guitar",
    .regs        = s_regs,
    .report_len  = 6,
    .build_report = guitar_build_report,
    .set_button  = guitar_set_button,
    .reset       = guitar_reset,
};

void Guitar_Init(void)
{
    memset(s_regs, 0, sizeof(s_regs));
    static const uint8_t k_id[6] = { 0x00, 0x00, 0xA4, 0x20, 0x01, 0x03 };  /* GH guitar */
    memcpy(&s_regs[0xFA], k_id, sizeof(k_id));
    Wiimote_RegisterExtension(&s_guitar);
}
