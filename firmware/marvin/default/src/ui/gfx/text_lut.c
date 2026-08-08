#include "ui/gfx/text_lut.h"

#include "gfx/legato/renderer/legato_renderer.h"
#include "gfx/legato/string/legato_string_renderer.h"
#include "gfx/legato/common/legato_color.h"

/* See text_lut.h for what this is for and why the lookup path is switchable. */

/* Distinct (foreground, background) pairs held. The log table uses seven — four level
 * colours plus time, source and message — over one background. */
#define TEXT_LUT_CACHE   8u

typedef struct
{
    leBlendLookupTable tbl;          /* what Legato is handed; .data points at ramp */
    uint16_t           ramp[256];    /* background..foreground, indexed by coverage */
    leColor            fg, bg;       /* RGB_888 keys */
    leColorMode        mode;         /* render mode the ramp was built for */
    bool               used;
} lut_entry_t;

static lut_entry_t s_cache[TEXT_LUT_CACHE];
static uint32_t    s_built;
static uint32_t    s_refused;

const char *TextLut_PathName(text_path_t path)
{
    switch (path)
    {
        case TEXT_PATH_USTRING: return "ustring";
        case TEXT_PATH_WALK:    return "walk";
        default:                return "lut";
    }
}

void TextLut_CacheStats(uint32_t *built, uint32_t *refused)
{
    if (built   != NULL) { *built   = s_built;   }
    if (refused != NULL) { *refused = s_refused; }
}

/* The ramp: coverage 0 is pure background, 255 pure foreground, interpolated per channel in
 * 888 and converted once into the render mode. Built at 8-bit precision rather than through
 * leColorLerp's 0..100 percent, so the 256 entries are distinct where the source colours
 * allow it.
 *
 * Interpolating in 888 and converting afterwards (rather than lerping the packed 565 values)
 * is what keeps the ramp free of the banding that quantising each step would introduce. */
static void build_ramp(lut_entry_t *e, leColorMode mode)
{
    const uint32_t fr = (e->fg >> 16) & 0xFFu, fg_ = (e->fg >> 8) & 0xFFu, fb = e->fg & 0xFFu;
    const uint32_t br = (e->bg >> 16) & 0xFFu, bg_ = (e->bg >> 8) & 0xFFu, bb = e->bg & 0xFFu;

    for (uint32_t a = 0u; a < 256u; a++)
    {
        uint32_t inv = 255u - a;
        uint32_t r   = (fr * a + br * inv) / 255u;
        uint32_t g   = (fg_ * a + bg_ * inv) / 255u;
        uint32_t b   = (fb * a + bb * inv) / 255u;

        e->ramp[a] = (uint16_t)leColorConvert(LE_COLOR_MODE_RGB_888, mode,
                                              (r << 16) | (g << 8) | b);
    }

    e->tbl.foreground = e->fg;
    e->tbl.background = e->bg;
    e->tbl.mode       = mode;
    e->tbl.data       = e->ramp;
    e->mode           = mode;
    s_built++;
}

/* The table for this colour pair in the current render mode, building it on first use.
 * NULL when the mode is not 16bpp (the ramp's entry width would not match what
 * leFont_DrawGlyphData_Lookup indexes) or when the cache is full. */
static const leBlendLookupTable *lut_for(leColor fg, leColor bg)
{
    leColorMode mode = leRenderer_CurrentColorMode();

    if (leColorInfoTable[mode].bpp != 16u) { return NULL; }

    lut_entry_t *free_slot = NULL;

    for (uint32_t i = 0u; i < TEXT_LUT_CACHE; i++)
    {
        lut_entry_t *e = &s_cache[i];

        if (!e->used) { if (free_slot == NULL) { free_slot = e; } continue; }
        if (e->fg == fg && e->bg == bg && e->mode == mode) { return &e->tbl; }
    }

    if (free_slot == NULL) { s_refused++; return NULL; }

    free_slot->fg   = fg;
    free_slot->bg   = bg;
    free_slot->used = true;
    build_ramp(free_slot, mode);

    return &free_slot->tbl;
}

/* Walk the code points and place each glyph exactly where drawUString would: pen at
 * x + bearingX, baseline at top + font->baseline, pen advanced by the glyph's advance.
 *
 * Spaces are skipped. drawUString means to skip them too but tests a `codePoint` variable it
 * never assigns inside its loop, so it actually draws them; skipping is both correct and
 * strictly cheaper, and it is invisible either way — a space carries no ink, and on the
 * lookup path its box would be painted in the background colour that is already there. */
static void walk_glyphs(const leFont *font, int x, int top,
                        const leChar *s, uint32_t len,
                        leColor fg, const leBlendLookupTable *tbl)
{
    const leRasterFont *ras = (const leRasterFont *)font;
    int pen = x;

    for (uint32_t i = 0u; i < len; i++)
    {
        leFontGlyph g = {0};

        if (leFont_GetGlyphInfo((leFont *)font, s[i], &g) != LE_SUCCESS) { continue; }

        if (s[i] != (leChar)' ')
        {
            int gx = pen + g.bearingX;
            int gy = top + ((int)ras->baseline - g.bearingY);

            if (tbl != NULL) { (void)leFont_DrawGlyph_Lookup((leFont *)font, &g, gx, gy, tbl); }
            else             { (void)leFont_DrawGlyph((leFont *)font, &g, gx, gy, fg, 255u); }
        }

        pen += g.advance;
    }
}

void TextLut_DrawLine(const leFont *font, int x, int top,
                      const leChar *s, uint32_t len,
                      leColor fg_rgb888, leColor bg_rgb888,
                      text_path_t path)
{
    if (font == NULL || s == NULL || len == 0u) { return; }

    leColor fg = leColorConvert(LE_COLOR_MODE_RGB_888, leRenderer_CurrentColorMode(),
                                fg_rgb888);

    if (path == TEXT_PATH_USTRING)
    {
        leUStringRenderRequest req;

        req.str    = (leChar *)s;
        req.length = len;
        req.font   = font;
        req.x      = x;
        req.y      = top;
        req.align  = LE_HALIGN_LEFT;
        req.color  = fg;
        req.alpha  = 255;
        req.lookupTable = NULL;   /* drawUString ignores it anyway — see text_lut.h */

        (void)leStringRenderer_DrawUString(&req);
        return;
    }

    const leBlendLookupTable *tbl = (path == TEXT_PATH_LUT)
                                  ? lut_for(fg_rgb888, bg_rgb888) : NULL;

    walk_glyphs(font, x, top, s, len, fg, tbl);
}
