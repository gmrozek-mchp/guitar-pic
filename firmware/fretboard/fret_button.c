#include "fret_button.h"
#include "fret_detect.h"
#include "definitions.h"
#include "fret_scan.h"

#define SW0_DEBOUNCE_MS  30

/* ---- pending note FIFO ---- */

typedef struct {
    uint8_t  fret_mask;
    uint32_t assert_at;
} pending_note_t;

#define NOTE_Q_CAP 8
static pending_note_t note_q[NOTE_Q_CAP];
static uint8_t nq_head;
static uint8_t nq_count;

/* ---- pending strum FIFO ---- */

typedef struct {
    uint8_t  fret_mask;
    uint32_t strum_at;
} pending_strum_t;

#define STRUM_Q_CAP 8
static pending_strum_t strum_q[STRUM_Q_CAP];
static uint8_t sq_head;
static uint8_t sq_count;

/* ---- per-fret release scheduling ---- */

static uint32_t release_at[FRET_COUNT];
static bool     release_pending[FRET_COUNT];

/* ---- chord accumulation window ---- */

static bool     chord_open;
static uint32_t chord_start_ms;
static uint8_t  chord_mask;

/* ---- fret output tracking ---- */

static uint8_t frets_active;

/* ---- strum state ---- */

static uint32_t strum_release_at;
static bool     strum_active;
static bool     strum_direction;

/* ---- enable / SW0 ---- */

static bool     enabled;
static bool     sw0_prev;
static uint32_t sw0_change_ms;
static bool     sw0_pending;

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
    // button_release(FRET_ORANGE);
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
    // button_assert(FRET_ORANGE);
}

static void release_all(void)
{
    for (uint8_t i = 0; i < FRET_COUNT; i++) {
        button_release(i);
        release_pending[i] = false;
    }
    frets_active = 0;
    strum_release();
    nq_head = 0;
    nq_count = 0;
    sq_head = 0;
    sq_count = 0;
    chord_open = false;
    chord_mask = 0;
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

/* ---- chord commit ---- */

static void chord_commit(uint32_t now)
{
    if (nq_count >= NOTE_Q_CAP || sq_count >= STRUM_Q_CAP) {
        chord_open = false;
        chord_mask = 0;
        return;
    }

    uint32_t assert_at = now + STRUM_DELAY_MS - FRET_EARLY_MS;
    uint32_t strum_at  = now + STRUM_DELAY_MS;

    /* If previous strum pulse hasn't finished, push times forward so
       new frets don't interfere with the prior strum. */
    if (sq_count > 0) {
        uint8_t tail = (sq_head + sq_count - 1U) % STRUM_Q_CAP;
        uint32_t earliest = strum_q[tail].strum_at + STRUM_PULSE_MS;
        if ((int32_t)(assert_at - earliest) < 0) {
            assert_at = earliest;
            strum_at  = assert_at + FRET_EARLY_MS;
        }
    }

    uint8_t nslot = (nq_head + nq_count) % NOTE_Q_CAP;
    note_q[nslot] = (pending_note_t){
        .fret_mask  = chord_mask,
        .assert_at  = assert_at,
    };
    nq_count++;

    uint8_t sslot = (sq_head + sq_count) % STRUM_Q_CAP;
    strum_q[sslot] = (pending_strum_t){
        .fret_mask  = chord_mask,
        .strum_at   = strum_at,
    };
    sq_count++;

    chord_open = false;
    chord_mask = 0;
}

/* ---- process pending notes (FIFO order) ---- */

static void process_notes(uint32_t now)
{
    while (nq_count > 0) {
        pending_note_t *n = &note_q[nq_head];
        if ((int32_t)(now - n->assert_at) < 0)
            break;

        for (uint8_t i = 0; i < FRET_COUNT; i++) {
            bool need = (n->fret_mask >> i) & 1U;
            bool have = (frets_active >> i) & 1U;
            if (need && !have) {
                button_assert(i);
                frets_active |= (1U << i);
            } else if (!need && have) {
                button_release(i);
                frets_active &= ~(1U << i);
            }
        }

        nq_head = (nq_head + 1U) % NOTE_Q_CAP;
        nq_count--;
    }
}

/* ---- process pending strums (FIFO order) ---- */

static void process_strums(uint32_t now)
{
    while (sq_count > 0) {
        pending_strum_t *s = &strum_q[sq_head];
        if ((int32_t)(now - s->strum_at) < 0)
            break;

        strum_trigger(now);
        sq_head = (sq_head + 1U) % STRUM_Q_CAP;
        sq_count--;
    }
}

/* ---- process per-fret releases ---- */

static void process_releases(uint32_t now)
{
    for (uint8_t i = 0; i < FRET_COUNT; i++) {
        if (!release_pending[i])
            continue;
        if ((int32_t)(now - release_at[i]) < 0)
            continue;

        if (fret_is_pressed((fret_channel_t)i))
            continue;

        /* Don't release if any pending note or strum still needs this fret. */
        bool needed = false;
        for (uint8_t j = 0; j < nq_count && !needed; j++) {
            uint8_t slot = (nq_head + j) % NOTE_Q_CAP;
            if (note_q[slot].fret_mask & (1U << i))
                needed = true;
        }
        for (uint8_t j = 0; j < sq_count && !needed; j++) {
            uint8_t slot = (sq_head + j) % STRUM_Q_CAP;
            if (strum_q[slot].fret_mask & (1U << i))
                needed = true;
        }
        if (needed)
            continue;

        button_release(i);
        frets_active &= ~(1U << i);
        release_pending[i] = false;
    }
}

/* ---- main update ---- */

void fret_button_init(void)
{
    nq_head = 0;
    nq_count = 0;
    sq_head = 0;
    sq_count = 0;
    frets_active = 0;
    chord_open = false;
    chord_mask = 0;
    strum_active = false;
    strum_direction = false;
    sw0_prev = !SW0_Get();
    sw0_pending = false;

    for (uint8_t i = 0; i < FRET_COUNT; i++) {
        release_pending[i] = false;
        button_release(i);
    }
    strum_release();
    set_enabled(false);
}

void fret_button_update(uint32_t now)
{
    sw0_poll(now);

    if (!enabled)
        return;

    uint8_t presses  = fret_detect_new_presses();
    uint8_t releases = fret_detect_new_releases();

    /* New presses open / extend the chord window. */
    if (presses) {
        if (!chord_open) {
            chord_open = true;
            chord_start_ms = now;
            chord_mask = 0;
        }
        chord_mask |= presses;
    }

    /* Schedule delayed releases for each fret that went up. */
    for (uint8_t i = 0; i < FRET_COUNT; i++) {
        if (releases & (1U << i)) {
            release_at[i] = now + STRUM_DELAY_MS;
            release_pending[i] = true;
        }
    }

    if (chord_open && (now - chord_start_ms) >= CHORD_WINDOW_MS)
        chord_commit(now);

    if (strum_active && (int32_t)(now - strum_release_at) >= 0)
        strum_release();

    process_notes(now);
    process_strums(now);
    process_releases(now);
}

bool fret_button_is_enabled(void)
{
    return enabled;
}
