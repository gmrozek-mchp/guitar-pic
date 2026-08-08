#ifndef UI_GFX_TEXT_LUT_H
#define UI_GFX_TEXT_LUT_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/legato/legato.h"
#include "gfx/legato/font/legato_font.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One line of single-line, left-aligned text, drawn three ways so the cost of each can be
 * measured against the others on hardware. For text-heavy screens repainted often — the
 * activity log's table is ~1,300 glyphs a repaint and text is ~70% of its 71.9 ms.
 *
 * Why not just call the string renderer: two separate costs sit in that path, and they are
 * worth separating before either is optimised away.
 *
 * 1. `drawUString` measures before it draws. Per call it runs GetLineCount, GetRect,
 *    KerningRect and then GetLineRect per line — three or four full passes over the string
 *    computing glyph advances — before the first glyph is blitted. For single-line
 *    left-aligned monospace text that is all discardable. TEXT_PATH_WALK removes it and
 *    changes nothing else.
 *
 * 2. Every covered glyph pixel goes through `leRenderer_BlendPixel`, which reads the
 *    destination from DDR, converts it to RGBA8888, converts the *source colour* to
 *    RGBA8888 again (it is constant for the whole string, recomputed per pixel), blends
 *    four channels, converts the result back, and writes. Legato already ships the
 *    alternative — `leFont_DrawGlyph_Lookup` indexes a 256-entry table by the glyph's own
 *    coverage byte and does a plain write, no read and no arithmetic. TEXT_PATH_LUT uses
 *    it.
 *
 * The lookup path is NOT unambiguously cheaper and that is why it is switchable rather than
 * simply adopted: it writes every pixel of a glyph's ink box, including the transparent
 * ones, where the blend path touches only covered pixels. DejaVuSansMono_12 measures 61%
 * coverage (6,644 of 10,900 raster bytes non-zero), so per glyph the transaction count only
 * improves from 13,288 read+writes to 10,900 writes — 1.22×. Everything else it saves is
 * arithmetic. Whether that matters depends on whether the ~590–940 cycles/px this screen
 * measures is DDR-bound or ALU-bound, which is a question for the panel, not for reasoning.
 *
 * Restricted to 16-bit render colour modes (RGB565, which every marvin canvas that draws
 * text uses); anything else falls back to TEXT_PATH_WALK, since the lookup table's entry
 * width has to match the render buffer's. */

typedef enum
{
    TEXT_PATH_USTRING = 0,   /* leStringRenderer_DrawUString — Legato's own path */
    TEXT_PATH_WALK,          /* our glyph walk + Legato's per-pixel blend */
    TEXT_PATH_LUT,           /* our glyph walk + blend lookup table */
} text_path_t;

/* Draw `len` code points at (x, top), left aligned, where `top` is the top of the line box
 * (the baseline lands at top + font->baseline, matching Legato's string renderer).
 *
 * `bg_rgb888` is the solid colour the text is drawn over. It is only used by
 * TEXT_PATH_LUT, which needs it to build the ramp — so that path is only correct where the
 * background really is that flat colour under the whole glyph. Callers that fill their own
 * background (the log table) satisfy that by construction.
 *
 * Must be called from inside a paint: it draws through the Legato renderer and clips to the
 * current damage rect. */
void TextLut_DrawLine(const leFont *font, int x, int top,
                      const leChar *s, uint32_t len,
                      leColor fg_rgb888, leColor bg_rgb888,
                      text_path_t path);

const char *TextLut_PathName(text_path_t path);

/* Tables built since boot, and how many builds were refused because the cache was full.
 * A non-zero refusal count means a caller is using more colour pairs than TEXT_LUT_CACHE
 * holds and is silently falling back to the blend path. */
void TextLut_CacheStats(uint32_t *built, uint32_t *refused);

#ifdef __cplusplus
}
#endif

#endif /* UI_GFX_TEXT_LUT_H */
