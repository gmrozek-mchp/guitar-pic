#include "ui/song_detail.h"

#include <stdio.h>

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
    (void)snprintf(buf + p, n - p, " TIER %d", tier);
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
        default: return &SCHEME_TEXT_GRAY_D4D4D8;   /* bonus / unknown → light gray */
    }
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
