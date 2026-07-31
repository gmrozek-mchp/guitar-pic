#include "publish.h"

#include <stddef.h>

static LightshowFrame s_show;
static volatile bool  s_show_ready;

/* Scale an auto-ranged 0-1000 feature to 0-255. int is 16-bit on dsPIC33A, so
 * the multiply is done in 32-bit to avoid overflow. */
static uint8_t scale_1000(uint16_t v)
{
    if (v >= 1000u)
    {
        return 255u;
    }
    return (uint8_t)(((uint32_t)v * 255u) / 1000u);
}

/* Scale the absolute 0-10000 envelope to 0-255. */
static uint8_t scale_env(uint16_t v)
{
    if (v >= 10000u)
    {
        return 255u;
    }
    return (uint8_t)(((uint32_t)v * 255u) / 10000u);
}

void Publish_Initialize(void)
{
    LightshowFrame z = { 0 };
    s_show = z;
    s_show_ready = false;
}

void Publish_Update(const BeatFrame *f)
{
    if (f == NULL)
    {
        return;
    }

    LightshowFrame s;
    s.seq    = (uint8_t)(s_show.seq + 1u);
    s.energy = scale_env(f->raw_env);
    s.bass   = scale_1000(f->flux_bass);
    s.treble = scale_1000(f->flux_full);
    s.kick   = (f->kick_beat > 0u) ? scale_1000(f->kick_strength) : 0u;

    uint8_t flags = 0u;
    if (f->bass_beat > 0u) { flags |= PUB_FLAG_BASS_BEAT; }
    if (f->full_beat > 0u) { flags |= PUB_FLAG_MID_BEAT; }
    if (f->kick_beat > 0u) { flags |= PUB_FLAG_KICK; }
    if ((f->bass_beat > 1u) || (f->full_beat > 1u) || (f->kick_beat > 1u))
    {
        flags |= PUB_FLAG_BIG_BEAT;
    }
    if (f->bass_dominant) { flags |= PUB_FLAG_BASS_DOM; }
    s.flags = flags;

    s.tempo = 0u;   /* reserved until the tempo layer lands */
    s.phase = 0u;

    s_show = s;
    s_show_ready = true;
}

void Publish_GetLightshowFrame(LightshowFrame *out)
{
    if (out != NULL)
    {
        *out = s_show;
    }
}

bool Publish_HasLightshowFrame(void)
{
    if (s_show_ready)
    {
        s_show_ready = false;
        return true;
    }
    return false;
}
