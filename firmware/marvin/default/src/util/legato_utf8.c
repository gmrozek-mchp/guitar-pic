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

    return str->fn->setFromChar(str, buf, n);
}
