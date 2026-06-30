#ifndef MARVIN_UTIL_LEGATO_UTF8_H
#define MARVIN_UTIL_LEGATO_UTF8_H

#include <stdint.h>

#include "gfx/legato/legato.h"   /* leChar, leString, leResult */

/* UTF-8 helpers for Legato strings. Legato's setFromCStr and the C-string
 * renderer are byte-oriented: each input byte becomes one leChar/glyph, so a
 * multibyte UTF-8 sequence renders as several glyphs. These decode UTF-8 into
 * leChar code points first. leChar is uint16_t, so code points are limited to
 * the BMP (U+0000..U+FFFF); anything higher stops the decode. */

/* Decode utf8 into out[] as code points. Writes at most cap code points and
 * returns the count written. Stops at the NUL, on a malformed byte, or when
 * cap is reached. */
uint32_t utf8_to_lechar(const char *utf8, leChar *out, uint32_t cap);

/* Set any leString (fixed or dynamic) from a UTF-8 C string, decoding
 * multibyte sequences into single code points. Use instead of
 * str->fn->setFromCStr for text that may contain non-ASCII characters. */
leResult lestring_set_utf8(leString *str, const char *utf8);

#endif
