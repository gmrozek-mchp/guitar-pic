#ifndef UI_GFX_GLYPH_BLIT_H
#define UI_GFX_GLYPH_BLIT_H

#include <stdint.h>

#include "gfx/legato/font/legato_font.h"   /* leRasterFont */

#ifdef __cplusplus
extern "C" {
#endif

/* Draw text from a Legato raster font straight into a raw RGBA8888 buffer.
 *
 * Legato's own text path (leFont_DrawGlyph, the string renderer) blits through
 * leRenderer_BlendPixel, so it needs an active paint pass and targets whatever canvas
 * that pass is rendering — unusable for a buffer nobody has bound, and unusable before
 * the Legato screens exist at all. The font ASSET is fine either way:
 * leFont_GetGlyphInfo is a pure lookup in a const glyph table, and the glyph's 8bpp
 * coverage bitmap is directly addressable. So these functions read the asset and
 * composite the glyphs themselves.
 *
 * The buffer is the XLCDC RGBA_8888 layout: pixel word 0xRRGGBBAA. `stride` and `h` are
 * in pixels; drawing is clipped to the buffer. `top` is the top of the text line (the
 * baseline lands at top + font->baseline, as in Legato's string renderer) and `rgb` is
 * a plain 0xRRGGBB — output alpha is left opaque.
 *
 * ASCII only (codepoint == byte) and 8bpp (antialiased) fonts only; a 1bpp font draws
 * nothing. One blend per glyph pixel, so this is for text that changes rarely. */
void GlyphBlit_Text(uint32_t *fb, uint32_t stride, uint32_t h,
                    int32_t x, int32_t top, const char *s,
                    const leRasterFont *font, uint32_t rgb);

/* Advance width of `s` in px — for right-aligning or centring before drawing. */
uint32_t GlyphBlit_TextWidth(const leRasterFont *font, const char *s);

#ifdef __cplusplus
}
#endif

#endif /* UI_GFX_GLYPH_BLIT_H */
