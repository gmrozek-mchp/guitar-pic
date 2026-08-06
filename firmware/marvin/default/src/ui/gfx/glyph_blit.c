#include "ui/gfx/glyph_blit.h"

#include <stdbool.h>

#include "gfx/legato/core/legato_stream.h"   /* LE_STREAM_LOCATION_ID_INTERNAL */

/* Blend `rgb` at coverage `a` (0..255) over one RGBA8888 pixel word (0xRRGGBBAA),
 * leaving it opaque. */
static uint32_t blend_px(uint32_t dst, uint32_t rgb, uint32_t a)
{
    uint32_t inv = 255u - a;
    uint32_t r = ((((rgb >> 16) & 0xFFu) * a) + (((dst >> 24) & 0xFFu) * inv) + 127u) / 255u;
    uint32_t g = ((((rgb >>  8) & 0xFFu) * a) + (((dst >> 16) & 0xFFu) * inv) + 127u) / 255u;
    uint32_t b = ((((rgb      ) & 0xFFu) * a) + (((dst >>  8) & 0xFFu) * inv) + 127u) / 255u;
    return (r << 24) | (g << 16) | (b << 8) | 0xFFu;
}

/* Base of a glyph's raster data. Matches leFont_DrawGlyph: the offset is from the
 * font's stream address, which for a generated asset is the const table in flash. */
static const uint8_t *glyph_data(const leRasterFont *font, const leFontGlyph *g)
{
    if (font->base.header.location != LE_STREAM_LOCATION_ID_INTERNAL) { return NULL; }
    return (const uint8_t *)font->base.header.address + g->dataOffset;
}

/* Glyph metrics, or false if the codepoint is absent (GetGlyphInfo then reports a
 * synthetic unknown/space glyph with no raster data, which we simply skip). */
static bool glyph_info(const leRasterFont *font, char c, leFontGlyph *g)
{
    *g = (leFontGlyph){ 0 };
    return leFont_GetGlyphInfo(&font->base, (uint32_t)(uint8_t)c, g) == LE_SUCCESS;
}

uint32_t GlyphBlit_TextWidth(const leRasterFont *font, const char *s)
{
    uint32_t w = 0u;
    for (; *s != '\0'; s++)
    {
        leFontGlyph g;
        if (glyph_info(font, *s, &g)) { w += (uint32_t)g.advance; }
    }
    return w;
}

void GlyphBlit_Text(uint32_t *fb, uint32_t stride, uint32_t h,
                    int32_t x, int32_t top, const char *s,
                    const leRasterFont *font, uint32_t rgb)
{
    if ((fb == NULL) || (font == NULL) || (font->bpp != LE_FONT_BPP_8)) { return; }

    for (; *s != '\0'; s++)
    {
        leFontGlyph g;
        if (!glyph_info(font, *s, &g)) { continue; }

        const uint8_t *data = glyph_data(font, &g);
        if ((data != NULL) && (g.dataRowWidth > 0u))
        {
            int32_t gx = x + g.bearingX;
            int32_t gy = top + (int32_t)font->baseline - g.bearingY;

            for (int32_t row = 0; row < g.height; row++)
            {
                int32_t py = gy + row;
                if ((py < 0) || (py >= (int32_t)h)) { continue; }

                const uint8_t *cov = data + ((uint32_t)row * g.dataRowWidth);
                uint32_t      *dst = &fb[(uint32_t)py * stride];

                for (int32_t col = 0; col < g.width; col++)
                {
                    int32_t px = gx + col;
                    if ((px < 0) || (px >= (int32_t)stride) || (cov[col] == 0u)) { continue; }
                    dst[px] = blend_px(dst[px], rgb, cov[col]);
                }
            }
        }
        x += g.advance;
    }
}
