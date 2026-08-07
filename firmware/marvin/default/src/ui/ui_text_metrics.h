#ifndef UI_TEXT_METRICS_H
#define UI_TEXT_METRICS_H

/* Where Legato actually puts a single line of label text, and how to align a bullet to it.
 *
 * leLabelWidget does NOT centre the font box in the widget rect. The chain is:
 *
 *   _leString_GetLineRect        rect.height = fontHeight
 *   leStringUtils_KerningRect    height -= fontHeight; height += fontBaseline
 *                                  -> for one line, height == fontBaseline (descender dropped)
 *   ArrangeRectangleRelative     LE_VALIGN_MIDDLE: rect_y = h/2 - height/2
 *   leStringRenderer_DrawString  glyph drawn at rect_y + (fontBaseline - glyph.bearingY),
 *                                  so rect_y is the glyph-box TOP
 *
 * Centring on fontHeight instead is wrong by half the descender — 3px at DejaVuSansMono_16 —
 * and it fails silently: Legato still draws the text correctly, so only the things aligned
 * from the bad arithmetic are off, with nothing visibly broken to point at.
 *
 * DOT_Y centres a d-wide bullet on the line's optical middle, which is NOT the row-box centre
 * (that reads ~3px high — it ignores the descender, as above) and not quite the x-height
 * middle either. The x-height middle is right for running lowercase, but the labels on these
 * screens are caps- and digit-heavy ("PIC32CM6408PL10048", "IDLE", "NO SIGNAL", "41,000
 * lines of..."), so their mass sits higher. The `- 1` puts the bullet on the mean of the
 * cap-height and x-height middles, within half a pixel at every call site here, and that is
 * the position confirmed to read level on the panel.
 *
 * Use these only for a bullet beside ONE line of text. A marker set against a multi-line
 * block (see the navigation footer) is centred on the block on purpose, not on a baseline. */

#define TEXT_BASELINE(h, base)      (((h) / 2) - ((base) / 2) + (base))
#define DOT_Y(h, base, xh, d)       (TEXT_BASELINE(h, base) - ((xh) + (d)) / 2 - 1)

/* Vertical metrics decoded from le_gen_fonts.c: BASE is the leRasterFont `baseline` field,
 * XH the x-height (bearingY of round lowercase — a, c, e, o, s…). Regenerate with
 *   python3 .claude/skills/mgs-legato-design/scripts/font_metrics.py <src> --box <h>
 * after any font change; MGS can resize a face without the name changing. */
#define MONO12_BASE    12
#define MONO12_XH       7
#define MONO14_BASE    14
#define MONO14_XH       8
#define MONO16_BASE    15
#define MONO16_XH       9
#define MONO18_BASE    17
#define MONO18_XH      10
#define MONO20_BASE    19
#define MONO20_XH      11

/* Bold matches regular on every vertical metric at 14/16/18/20/24. The 12px pair does NOT
 * (Bold_12 is height 15 / baseline 11 against 16 / 12), so it gets its own constants. */
#define MONO_B12_BASE  11
#define MONO_B12_XH     7
#define MONO_B14_BASE  MONO14_BASE
#define MONO_B14_XH    MONO14_XH

#endif /* UI_TEXT_METRICS_H */
