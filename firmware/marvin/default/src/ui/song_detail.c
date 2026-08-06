#include "ui/song_detail.h"

#include <stdio.h>
#include <string.h>

int SongDetail_Tier(const char *difficulty)
{
    const char *d = difficulty;
    if (d != NULL && d[0] >= '1' && d[0] <= '8' && d[1] == '\0') { return d[0] - '0'; }
    return 0;
}

void SongDetail_TierText(int tier, char *buf, size_t n)
{
    size_t p = 0;
    if (tier <= 0) { (void)snprintf(buf, n, "BONUS"); return; }
    for (int i = 0; i < tier && p + 3u < n; i++)
    {
        buf[p++] = (char)0xE2; buf[p++] = (char)0x98; buf[p++] = (char)0x85;   /* U+2605 ★ */
    }
    if (p < n) { buf[p] = '\0'; } else if (n > 0u) { buf[n - 1u] = '\0'; }
}

void SongDetail_TierTextLong(int tier, char *buf, size_t n)
{
    size_t p;

    SongDetail_TierText(tier, buf, n);
    if (tier <= 0) { return; }   /* "BONUS" names itself */

    p = strlen(buf);
    (void)snprintf(buf + p, (n > p) ? (n - p) : 0u, "  TIER %d", tier);
}

const leScheme *SongDetail_TierScheme(int tier)
{
    switch (tier)
    {
        case 1:  return &SCHEME_TEXT_TIER_1;
        case 2:  return &SCHEME_TEXT_TIER_2;
        case 3:  return &SCHEME_TEXT_TIER_3;
        case 4:  return &SCHEME_TEXT_TIER_4;
        case 5:  return &SCHEME_TEXT_TIER_5;
        case 6:  return &SCHEME_TEXT_TIER_6;
        case 7:  return &SCHEME_TEXT_TIER_7;
        case 8:  return &SCHEME_TEXT_TIER_8;
        default: return &SCHEME_TEXT_ZINC_300;   /* bonus / unknown → light gray */
    }
}

leColor SongDetail_TierColor(int tier)
{
    return leScheme_GetColor(SongDetail_TierScheme(tier), LE_SCHM_TEXT, LE_COLOR_MODE_RGB_888);
}

void SongDetail_Duration(uint16_t length_s, char *buf, size_t n)
{
    if (length_s != 0u)
    {
        (void)snprintf(buf, n, "%u:%02u",
                       (unsigned)(length_s / 60u), (unsigned)(length_s % 60u));
    }
    else { (void)snprintf(buf, n, "-"); }
}
