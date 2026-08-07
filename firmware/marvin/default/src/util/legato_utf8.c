#include "util/legato_utf8.h"

#include <string.h>

#include "gfx/legato/string/legato_stringutils.h"   /* leDecodeCodePoint */
#include "gfx/legato/string/legato_stringtable.h"    /* LE_STRING_ENCODING_UTF8 */

uint32_t utf8_to_lechar(const char *utf8, leChar *out, uint32_t cap)
{
    uint8_t *p;
    uint32_t rem;
    uint32_t n = 0;

    if (utf8 == NULL || out == NULL || cap == 0)
    {
        return 0;
    }

    p   = (uint8_t *)utf8;
    rem = (uint32_t)strlen(utf8);

    while (rem > 0 && n < cap)
    {
        uint32_t cp;
        uint32_t used;

        if (leDecodeCodePoint(LE_STRING_ENCODING_UTF8, p, rem, &cp, &used) != LE_SUCCESS
            || used == 0 || used > rem)
        {
            break;
        }

        out[n++] = (leChar)cp;   /* truncates to the BMP */
        p   += used;
        rem -= used;
    }

    return n;
}

leResult lestring_set_utf8(leString *str, const char *utf8)
{
    leChar   buf[128];
    uint32_t n;

    if (str == NULL)
    {
        return LE_FAILURE;
    }

    n = utf8_to_lechar(utf8, buf, sizeof(buf) / sizeof(buf[0]));

    /* Empty goes through clear(), not setFromChar: leFixedString_SetFromChar returns
     * LE_SUCCESS early on size 0 without touching length, so assigning "" would leave
     * the previous text in the buffer. clear() zeroes the length and damages the old
     * rect. Also covers a decode that produced nothing, where stale text would be
     * equally wrong. */
    if (n == 0)
    {
        if (str->fn->length(str) == 0u) { return LE_SUCCESS; }   /* already empty */

        str->fn->clear(str);
        return LE_SUCCESS;
    }

    /* Skip an identical write. leFixedString_SetFromChar does NOT compare — it preinvalidates
     * and invalidates unconditionally — so a screen that rewrites every field on a timer
     * repaints every label even when nothing moved. The bus screen's node table is ~60 labels
     * refreshed at 1 Hz, and on a quiet bus most of those numbers are the same second to
     * second. Cheap to check: the comparison is over at most `n` leChars, against a repaint. */
    if (str->fn->length(str) == n)
    {
        uint32_t i;

        for (i = 0u; i < n; i++)
        {
            if (str->fn->charAt(str, i) != buf[i]) { break; }
        }

        if (i == n) { return LE_SUCCESS; }
    }

    return str->fn->setFromChar(str, buf, n);
}
