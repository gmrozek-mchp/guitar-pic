#include "fret_button.h"
#include "fret_detect.h"
#include "definitions.h"

#define SW0_DEBOUNCE_MS  30

/* ---- event queue ---- */

typedef enum {
    EVT_PRESS,
    EVT_RELEASE,
    EVT_STRUM,
} event_type_t;

typedef struct {
    uint32_t fire_at;       /* ms tick when this fires */
    uint8_t  type;
    uint8_t  channel;
} event_t;

#define EVQ_CAP  32
static event_t evq[EVQ_CAP];
static uint8_t evq_count;

static bool evq_push(uint32_t fire_at, event_type_t type, uint8_t channel)
{
    if (evq_count >= EVQ_CAP)
        return false;
    evq[evq_count++] = (event_t){ fire_at, type, channel };
    return true;
}

/* ---- chord window ---- */

static bool     chord_open;
static uint32_t chord_start_ms;
static uint8_t  chord_channels;

/* ---- fret output tracking ---- */

static uint8_t frets_active;              /* bitmask of physically asserted frets */

#if FRET_ENABLE_HO_PO
/* Time of last scheduled strike (strum_at from previous chord_commit). */
static uint32_t last_strike_ms;
#endif

/* ---- strum state ---- */

static uint32_t strum_release_at;
static bool     strum_active;
static bool     strum_direction;

/* ---- enable / SW0 ---- */

static bool    enabled;
static bool    sw0_prev;
static uint32_t sw0_change_ms;
static bool     sw0_pending;

/* ---- per-channel previous detect state ---- */

static bool prev_detected[FRET_COUNT];

/* ---- button pin helpers ---- */

static void button_assert(uint8_t i)
{
    switch ((fret_channel_t)i) {
    case FRET_GREEN:  BUTTON_GREEN_Clear();  BUTTON_GREEN_OutputEnable();  break;
    case FRET_RED:    BUTTON_RED_Clear();    BUTTON_RED_OutputEnable();    break;
    case FRET_YELLOW: BUTTON_YELLOW_Clear(); BUTTON_YELLOW_OutputEnable(); break;
    case FRET_BLUE:   BUTTON_BLUE_Clear();   BUTTON_BLUE_OutputEnable();   break;
    case FRET_ORANGE: BUTTON_ORANGE_Clear(); BUTTON_ORANGE_OutputEnable(); break;
    default: break;
    }
}

static void button_release(uint8_t i)
{
    switch ((fret_channel_t)i) {
    case FRET_GREEN:  BUTTON_GREEN_InputEnable();  break;
    case FRET_RED:    BUTTON_RED_InputEnable();    break;
    case FRET_YELLOW: BUTTON_YELLOW_InputEnable(); break;
    case FRET_BLUE:   BUTTON_BLUE_InputEnable();   break;
    case FRET_ORANGE: BUTTON_ORANGE_InputEnable(); break;
    default: break;
    }
}

static void strum_release(void)
{
    BUTTON_STRUM_DOWN_InputEnable();
    BUTTON_STRUM_UP_InputEnable();
    strum_active = false;
    LED0_Set();
}

static void strum_trigger(uint32_t now)
{
    strum_release();
    if (strum_direction) {
        BUTTON_STRUM_UP_Clear();
        BUTTON_STRUM_UP_OutputEnable();
    } else {
        BUTTON_STRUM_DOWN_Clear();
        BUTTON_STRUM_DOWN_OutputEnable();
    }
    strum_direction = !strum_direction;
    strum_active = true;
    strum_release_at = now + STRUM_PULSE_MS;
    LED0_Clear();
}

static void release_all(void)
{
    for (uint8_t i = 0; i < FRET_COUNT; i++)
        button_release(i);
    frets_active = 0;
    strum_release();
    evq_count = 0;
    chord_open = false;
    chord_channels = 0;
#if FRET_ENABLE_HO_PO
    last_strike_ms = 0;
#endif
}

/* ---- enable / disable ---- */

static void set_enabled(bool en)
{
    enabled = en;
    if (en)
        LED0_Clear();
    else {
        LED0_Set();
        release_all();
    }
}

static void sw0_poll(uint32_t now)
{
    bool raw = !SW0_Get();
    if (raw == sw0_prev) {
        sw0_pending = false;
        return;
    }
    if (!sw0_pending) {
        sw0_pending = true;
        sw0_change_ms = now;
        return;
    }
    if ((now - sw0_change_ms) >= SW0_DEBOUNCE_MS) {
        sw0_prev = raw;
        sw0_pending = false;
        if (raw)
            set_enabled(!enabled);
    }
}

/* ---- chord management ---- */

static void chord_commit(uint32_t now)
{
    /* Strike time first — never compute (strike_at - FRET_EARLY) in one uint32 step:
       if STRUM_DELAY < FRET_EARLY it underflows, press_at becomes huge and strum order
       breaks. Strike is anchored to chord commit (window just closed) so frets/strum
       are scheduled relative to when the chord is known, not first touch. */
    uint32_t strike_at = now + STRUM_DELAY_MS;
    if ((int32_t)(strike_at - now) < 0)
        strike_at = now;

    uint32_t press_at;
    if (strike_at >= FRET_EARLY_MS)
        press_at = strike_at - FRET_EARLY_MS;
    else
        press_at = now;
    if ((int32_t)(press_at - now) < 0)
        press_at = now;

    /* ev_process_due runs earliest fire_at first; type order only ties equal times.
       If a prior chord's strum is still queued at S and this chord's press is P>S,
       strum fires first — pull P up to S so PRESS and STRUM share a tick (PRESS wins). */
    uint32_t latest_pending_strum = 0;
    bool     have_pending_strum = false;
    for (uint8_t j = 0; j < evq_count; j++) {
        if (evq[j].type != EVT_STRUM)
            continue;
        if (!have_pending_strum || evq[j].fire_at > latest_pending_strum) {
            latest_pending_strum = evq[j].fire_at;
            have_pending_strum = true;
        }
    }
    if (have_pending_strum && (int32_t)(press_at - latest_pending_strum) > 0)
        press_at = latest_pending_strum;
    if ((int32_t)(press_at - now) < 0)
        press_at = now;

    uint32_t strum_at = strike_at;
    if ((int32_t)(strum_at - (press_at + FRET_EARLY_MS)) < 0)
        strum_at = press_at + FRET_EARLY_MS;

#if FRET_ENABLE_HO_PO
    /* Strum unless HO/PO: fret(s) held and strike within window of previous. */
    bool need_strum;
    if (frets_active == 0) {
        need_strum = true;
    } else if (last_strike_ms == 0) {
        need_strum = true;
    } else {
        uint32_t gap = strum_at - last_strike_ms;
        need_strum = (gap > HO_PO_MAX_MS);
    }
#else
    bool need_strum = true;
#endif

    for (uint8_t i = 0; i < FRET_COUNT; i++) {
        if (chord_channels & (1U << i))
            evq_push(press_at, EVT_PRESS, i);
    }
    if (need_strum)
        evq_push(strum_at, EVT_STRUM, 0);

#if FRET_ENABLE_HO_PO
    last_strike_ms = strum_at;
#endif

    chord_open = false;
    chord_channels = 0;
}

/* Same fire_at → PRESS before STRUM before RELEASE. Earlier fire_at always first
   (fixes batch PRESS-then-STRUM running a strum before another chord’s press). */
static int ev_type_order(uint8_t type)
{
    switch (type) {
    case EVT_PRESS:   return 0;
    case EVT_STRUM:   return 1;
    case EVT_RELEASE: return 2;
    default:          return 3;
    }
}

static void ev_dispatch_one(uint32_t now, uint8_t idx)
{
    event_t *e = &evq[idx];
    switch (e->type) {
    case EVT_PRESS:
        button_assert(e->channel);
        frets_active |= (1U << e->channel);
        break;
    case EVT_STRUM:
        strum_trigger(now);
        break;
    case EVT_RELEASE: {
        uint8_t c = e->channel;
        if (!fret_is_pressed((fret_channel_t)c)) {
            button_release(c);
            frets_active &= ~(1U << c);
        }
        break;
    }
    default:
        break;
    }
    evq[idx] = evq[--evq_count];
}

static void ev_process_due(uint32_t now)
{
    for (;;) {
        int best = -1;
        uint32_t best_t = 0;
        int best_ord = 99;

        for (uint8_t j = 0; j < evq_count; j++) {
            if ((int32_t)(evq[j].fire_at - now) > 0)
                continue;
            int ord = ev_type_order(evq[j].type);
            if (best < 0
                || evq[j].fire_at < best_t
                || (evq[j].fire_at == best_t && ord < best_ord)) {
                best = (int)j;
                best_t = evq[j].fire_at;
                best_ord = ord;
            }
        }
        if (best < 0)
            break;
        ev_dispatch_one(now, (uint8_t)best);
    }
}

/* ---- main update ---- */

void fret_button_init(void)
{
    evq_count = 0;
    frets_active = 0;
#if FRET_ENABLE_HO_PO
    last_strike_ms = 0;
#endif
    chord_open = false;
    chord_channels = 0;
    strum_active = false;
    strum_direction = false;
    sw0_prev = !SW0_Get();
    sw0_pending = false;

    for (uint8_t i = 0; i < FRET_COUNT; i++) {
        prev_detected[i] = false;
        button_release(i);
    }
    strum_release();
    set_enabled(false);
}

void fret_button_update(void)
{
    uint32_t now = SYSTICK_GetTickCounter();

    sw0_poll(now);

    if (!enabled)
        return;

    /* Detect edges and build chords / schedule releases */
    for (uint8_t i = 0; i < FRET_COUNT; i++) {
        bool detected = fret_is_pressed((fret_channel_t)i);

        if (detected && !prev_detected[i]) {
            if (!chord_open) {
                chord_open = true;
                chord_start_ms = now;
                chord_channels = 0;
            }
            chord_channels |= (1U << i);
        }

        if (!detected && prev_detected[i])
            evq_push(now + STRUM_DELAY_MS, EVT_RELEASE, i);

        prev_detected[i] = detected;
    }

    /* Close chord window when it expires */
    if (chord_open && (now - chord_start_ms) >= CHORD_WINDOW_MS)
        chord_commit(now);

    ev_process_due(now);

    /* Strum pulse auto-release */
    if (strum_active && (int32_t)(now - strum_release_at) >= 0)
        strum_release();
}

bool fret_button_is_enabled(void)
{
    return enabled;
}
