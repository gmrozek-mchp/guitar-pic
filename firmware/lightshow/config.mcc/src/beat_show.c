#include "beat_show.h"

#include <string.h>

#include "definitions.h"   /* SYSTICK_GetTickCounter */
#include "neopixel.h"

/* Effect logic ported from the source project's WS2812 show
 * (firmware/beatbox/config.mcc.bak/main.c), adapted from a single 70-px strip
 * with 0-1000/0-10000 feature ranges to the two 33-px strands here driven from
 * the 0-255 LightshowFrame fields. */

#define NUM  NEOPIXEL_COUNT   /* pixels per strand */

/* Auto-cycle: hold each effect ~20 s at the ~23.4 Hz frame rate. */
#define CYCLE_FRAMES   (470u)

/* Clear the strip if no beat frame arrives for this long (music/bus quiet). */
#define IDLE_CLEAR_MS  (750u)

/* Local phase oscillator: with the tempo layer deferred, payload[7] (phase) is
 * reserved 0, so the comet's sweep is driven locally — advance each frame,
 * restart on a bass beat so it stays roughly beat-synced. Superseded by the
 * wire phase once beatbox sends it (non-zero). */
#define PHASE_STEP     (60u)   /* units of 0..1000 per frame */

/* Latest decoded frame + a flag set by OnFrame, consumed by Tasks. Both run in
 * main-loop context (the RX callback fires synchronously inside TC6_Service),
 * so no locking is needed. */
static volatile bool    s_new_frame;
static uint8_t          s_seq, s_energy, s_bass, s_treble, s_kick, s_flags;
static uint8_t          s_wire_phase;
static uint32_t         s_frame_count;
static uint32_t         s_last_frame_ms;
static bool             s_idle;         /* strip currently blanked */

/* Effect state. */
static uint8_t          s_effect;       /* 0..BEAT_SHOW_EFFECTS-1 */
static bool             s_auto = true;
static uint16_t         s_cycle_frames;
static uint16_t         s_phase;        /* local oscillator, 0..1000 */

/* Hue (0-767) + brightness (0-255) -> RGB. 0 = red, 256 = green, 512 = blue. */
static void hue_rgb(uint16_t h, uint8_t val, uint8_t *r, uint8_t *g, uint8_t *b)
{
    h %= 768u;
    if (h < 256u)      { *r = (uint8_t)(255u - h); *g = (uint8_t)h;         *b = 0u; }
    else if (h < 512u) { *r = 0u; *g = (uint8_t)(511u - h); *b = (uint8_t)(h - 256u); }
    else               { *r = (uint8_t)(h - 512u); *g = 0u; *b = (uint8_t)(767u - h); }
    *r = (uint8_t)((uint16_t)*r * val / 255u);
    *g = (uint8_t)((uint16_t)*g * val / 255u);
    *b = (uint8_t)((uint16_t)*b * val / 255u);
}

static void set_all(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint8_t s = 0u; s < NEOPIXEL_STRANDS; s++) {
        for (uint16_t i = 0u; i < NUM; i++) {
            NeoPixel_SetPixel(s, i, r, g, b);
        }
    }
}

/* Effect 0 - Beat Flash: beat -> full-strip flash that decays, bass pre-glow,
 * hue drifts each frame. */
static void ef_pulse(uint8_t bass_beat, uint8_t full_beat, uint8_t bass)
{
    static uint16_t bright, hue;
    uint16_t glow, total;
    uint8_t r, g, b;

    if ((bass_beat > 0u) || (full_beat > 0u)) { bright = 255u; }
    else if (bright > 0u)                     { bright = (uint16_t)((bright * 205u) >> 8); }

    glow = (uint16_t)((uint32_t)bass * 40u / 255u);
    total = bright + glow;
    if (total > 255u) { total = 255u; }

    hue = (uint16_t)((hue + 2u) % 768u);
    hue_rgb(hue, (uint8_t)total, &r, &g, &b);
    set_all(r, g, b);
}

/* Effect 1 - Dual Comet: two comets driven by the phase oscillator, warm on
 * strand 0 and cool on strand 1 (mirrored); beats flash the dim background. */
static void ef_comet(uint16_t phase, uint8_t bass_beat, uint8_t energy)
{
    static uint16_t beat_flash;
    uint16_t i, pos, posb;
    uint8_t t, bg;

    if (bass_beat > 0u)        { beat_flash = 200u; }
    else if (beat_flash > 0u)  { beat_flash = (uint16_t)((beat_flash * 210u) >> 8); }

    bg = (uint8_t)((uint32_t)energy * 5u / 255u + (beat_flash >> 3));
    for (i = 0u; i < NUM; i++) {
        NeoPixel_SetPixel(0u, i, bg, (uint8_t)(bg >> 1), 0u);              /* warm bed */
        NeoPixel_SetPixel(1u, i, 0u, (uint8_t)(bg >> 1), bg);             /* cool bed */
    }

    pos = (uint16_t)((uint32_t)phase * (NUM - 1u) / 1000u);
    for (i = 0u; i < 8u; i++) {
        if (pos >= i) {
            t = (uint8_t)(200u * (8u - i) / 8u);
            NeoPixel_SetPixel(0u, pos - i, t, (uint8_t)(t * 3u / 4u), (uint8_t)(t / 4u));
        }
    }
    NeoPixel_SetPixel(0u, pos, 255u, 220u, 80u);   /* warm yellow-white head */

    posb = (uint16_t)((NUM - 1u) - pos);
    for (i = 0u; i < 8u; i++) {
        if ((posb + i) < NUM) {
            t = (uint8_t)(200u * (8u - i) / 8u);
            NeoPixel_SetPixel(1u, posb + i, (uint8_t)(t / 4u), (uint8_t)(t * 3u / 4u), t);
        }
    }
    NeoPixel_SetPixel(1u, posb, 80u, 220u, 255u);  /* cool blue-white head */
}

/* Effect 2 - Split Energy: strand 0 fills warm amber with bass, strand 1 fills
 * cool blue with treble; band beats add a decaying flash. */
static void ef_split(uint8_t bass, uint8_t treble, uint8_t bass_beat, uint8_t full_beat)
{
    static uint16_t bf, tf;
    uint16_t i, total;
    uint8_t fill_b, fill_t, br;

    if (bass_beat > 0u) { bf = 240u; } else if (bf > 0u) { bf = (uint16_t)((bf * 210u) >> 8); }
    if (full_beat > 0u) { tf = 240u; } else if (tf > 0u) { tf = (uint16_t)((tf * 210u) >> 8); }

    fill_b = (uint8_t)((uint32_t)bass   * NUM / 255u);
    fill_t = (uint8_t)((uint32_t)treble * NUM / 255u);

    for (i = 0u; i < NUM; i++) {
        br = (i < fill_b) ? (uint8_t)(15u + (uint32_t)bass * 35u / 255u) : 0u;
        total = (uint16_t)br + (bf >> 2);
        if (total > 255u) { total = 255u; }
        br = (uint8_t)total;
        NeoPixel_SetPixel(0u, i, br, (uint8_t)(br >> 3), 0u);

        br = (i < fill_t) ? (uint8_t)(15u + (uint32_t)treble * 35u / 255u) : 0u;
        total = (uint16_t)br + (tf >> 2);
        if (total > 255u) { total = 255u; }
        br = (uint8_t)total;
        NeoPixel_SetPixel(1u, i, 0u, (uint8_t)(br >> 3), br);
    }
}

void BeatShow_Initialize(void)
{
    s_effect = 0u;
    s_auto = true;
    s_last_frame_ms = SYSTICK_GetTickCounter();
    NeoPixel_Clear();
    (void)NeoPixel_Show();
    s_idle = true;
}

void BeatShow_OnFrame(const uint8_t *payload, uint16_t len)
{
    if ((payload == NULL) || (len < BEAT_SHOW_FRAME_LEN)) {
        return;
    }
    s_seq        = payload[0];
    s_energy     = payload[1];
    s_bass       = payload[2];
    s_treble     = payload[3];
    s_kick       = payload[4];
    s_flags      = payload[5];
    s_wire_phase = payload[7];
    s_frame_count++;
    s_new_frame = true;
}

void BeatShow_Tasks(void)
{
    if (!s_new_frame) {
        /* No frames for a while: blank the strip once so it doesn't freeze on
         * the last lit frame when the music/bus goes quiet. */
        uint32_t now = SYSTICK_GetTickCounter();
        if (!s_idle && ((now - s_last_frame_ms) >= IDLE_CLEAR_MS)) {
            NeoPixel_Clear();
            (void)NeoPixel_Show();
            s_idle = true;
        }
        return;
    }
    s_new_frame = false;
    s_last_frame_ms = SYSTICK_GetTickCounter();
    s_idle = false;

    /* Beat strengths from the flags: 2 = strong (BIG set), 1 = onset. */
    uint8_t bass_beat = (s_flags & BEAT_FLAG_BASS) ? ((s_flags & BEAT_FLAG_BIG) ? 2u : 1u) : 0u;
    uint8_t full_beat = (s_flags & BEAT_FLAG_MID)  ? ((s_flags & BEAT_FLAG_BIG) ? 2u : 1u) : 0u;

    /* Comet phase: prefer the wire phase once beatbox sends it (0-255 -> 0-1000);
     * until then run the local oscillator, restarting on a bass beat. */
    if (s_wire_phase != 0u) {
        s_phase = (uint16_t)((uint32_t)s_wire_phase * 1000u / 255u);
    } else if (bass_beat > 0u) {
        s_phase = 0u;
    } else {
        s_phase += PHASE_STEP;
        if (s_phase > 1000u) { s_phase = 1000u; }
    }

    switch (s_effect) {
        case 0u:  ef_pulse(bass_beat, full_beat, s_bass);        break;
        case 1u:  ef_comet(s_phase, bass_beat, s_energy);        break;
        default:  ef_split(s_bass, s_treble, bass_beat, full_beat); break;
    }

    /* Show() is non-blocking; if a frame is still latching, keep the staged
     * pixels and let the next tick push them. */
    (void)NeoPixel_Show();

    if (s_auto && (++s_cycle_frames >= CYCLE_FRAMES)) {
        s_cycle_frames = 0u;
        s_effect = (uint8_t)((s_effect + 1u) % BEAT_SHOW_EFFECTS);
    }
}

void BeatShow_SetEffect(uint8_t idx)
{
    s_auto = false;
    s_effect = (uint8_t)(idx % BEAT_SHOW_EFFECTS);
    s_cycle_frames = 0u;
}

void BeatShow_SetAuto(void)
{
    s_auto = true;
    s_cycle_frames = 0u;
}

uint8_t BeatShow_Effect(void)   { return s_effect; }
bool    BeatShow_IsAuto(void)   { return s_auto; }

uint32_t BeatShow_FrameCount(void) { return s_frame_count; }

void BeatShow_GetLast(uint8_t *seq, uint8_t *energy, uint8_t *bass,
                      uint8_t *treble, uint8_t *kick, uint8_t *flags)
{
    if (seq    != NULL) { *seq    = s_seq; }
    if (energy != NULL) { *energy = s_energy; }
    if (bass   != NULL) { *bass   = s_bass; }
    if (treble != NULL) { *treble = s_treble; }
    if (kick   != NULL) { *kick   = s_kick; }
    if (flags  != NULL) { *flags  = s_flags; }
}
