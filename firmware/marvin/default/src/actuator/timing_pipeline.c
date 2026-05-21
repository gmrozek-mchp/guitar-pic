#include "timing_pipeline.h"
#include "fretboard_link.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "log.h"
#include "detector/detector.h"

#define TP_TASK_STACK_WORDS    768u
#define TP_TASK_PRIORITY       2u

/* Drives chord aggregation, strum scheduling, and pending release timing.
 * Matches the fret-tuner Python defaults so behavior carries 1:1. */
#define TP_STRUM_DELAY_MS      220u
#define TP_FRET_EARLY_MS       50u
#define TP_STRUM_PULSE_MS      50u
#define TP_CHORD_WINDOW_MS     20u

#define TP_FIFO_CAP            16u

/* Background tick: bound on between-frame latency for strum-pulse and
 * note-assert deadlines that fall between detector publishes. 5 ms keeps
 * jitter well under our 16 ms detector cadence and below human-perceptible
 * timing error. */
#define TP_TICK_MS             5u

typedef struct
{
    uint32_t assert_at_ms;
    uint8_t  fret_mask;
} pending_note_t;

typedef struct
{
    uint32_t strum_at_ms;
    uint8_t  fret_mask;
} pending_strum_t;

static StackType_t   s_task_stack[TP_TASK_STACK_WORDS];
static StaticTask_t  s_task_tcb;

/* Pipeline state — touched only by the timing task. */
static pending_note_t  s_note_q[TP_FIFO_CAP];
static uint8_t         s_note_head, s_note_tail, s_note_count;
static pending_strum_t s_strum_q[TP_FIFO_CAP];
static uint8_t         s_strum_head, s_strum_tail, s_strum_count;

static bool     s_chord_open;
static uint32_t s_chord_start_ms;
static uint8_t  s_chord_mask;

static uint8_t  s_frets_active;
static bool     s_strum_active;
static uint32_t s_strum_release_at_ms;
static bool     s_strum_direction;        /* toggles down/up each strum */

static uint8_t  s_prev_pressed_mask;
static uint16_t s_prev_press_count[FRET_COUNT];
static bool     s_press_count_seen;
static uint32_t s_release_at_ms[FRET_COUNT];
static uint8_t  s_release_pending_mask;

static uint8_t  s_output_mask;
static uint32_t s_now_ms;
static uint64_t s_last_frame_us;

static const uint8_t s_fret_bit[FRET_COUNT] =
{
    [FRET_GREEN]  = TIMING_BIT_GREEN,
    [FRET_RED]    = TIMING_BIT_RED,
    [FRET_YELLOW] = TIMING_BIT_YELLOW,
    [FRET_BLUE]   = TIMING_BIT_BLUE,
    [FRET_ORANGE] = TIMING_BIT_ORANGE,
};

static inline bool note_q_empty(void) { return s_note_count == 0u; }
static inline bool note_q_full(void)  { return s_note_count >= TP_FIFO_CAP; }
static inline bool strum_q_empty(void){ return s_strum_count == 0u; }
static inline bool strum_q_full(void) { return s_strum_count >= TP_FIFO_CAP; }

static void note_q_push(pending_note_t n)
{
    s_note_q[s_note_tail] = n;
    s_note_tail = (uint8_t)((s_note_tail + 1u) % TP_FIFO_CAP);
    s_note_count++;
}

static void note_q_pop(void)
{
    s_note_head = (uint8_t)((s_note_head + 1u) % TP_FIFO_CAP);
    s_note_count--;
}

static void strum_q_push(pending_strum_t s)
{
    s_strum_q[s_strum_tail] = s;
    s_strum_tail = (uint8_t)((s_strum_tail + 1u) % TP_FIFO_CAP);
    s_strum_count++;
}

static void strum_q_pop(void)
{
    s_strum_head = (uint8_t)((s_strum_head + 1u) % TP_FIFO_CAP);
    s_strum_count--;
}

/* Iterator helpers for the "is this fret needed by an outstanding entry?"
 * release-suppression check below. */
static bool note_q_any_needs(uint8_t bit)
{
    for (uint8_t i = 0u; i < s_note_count; i++)
    {
        uint8_t idx = (uint8_t)((s_note_head + i) % TP_FIFO_CAP);
        if (s_note_q[idx].fret_mask & bit) { return true; }
    }
    return false;
}

static bool strum_q_any_needs(uint8_t bit)
{
    for (uint8_t i = 0u; i < s_strum_count; i++)
    {
        uint8_t idx = (uint8_t)((s_strum_head + i) % TP_FIFO_CAP);
        if (s_strum_q[idx].fret_mask & bit) { return true; }
    }
    return false;
}

static void publish_mask(uint8_t mask)
{
    s_output_mask = mask;
    FretboardLink_Send(mask);
}

/* Edge derivation:
 *   - presses_mask: rising edge (press_count change preferred; pressed-edge
 *     fallback if the detector doesn't expose press_count for that fret)
 *   - releases_mask: falling edge of pressed
 * confidence is unused by the pipeline but is the natural future home for
 * a quality gate; left untouched here. */
static void derive_edges(const detector_state_t *state,
                         uint8_t *presses_mask, uint8_t *releases_mask,
                         uint8_t *pressed_mask)
{
    uint8_t pm = 0u;
    uint8_t rm = 0u;
    uint8_t cm = 0u;

    bool first = !s_press_count_seen;

    for (uint8_t i = 0u; i < FRET_COUNT; i++)
    {
        bool pressed = state->fret[i].pressed != 0u;
        if (pressed) { cm |= s_fret_bit[i]; }

        bool was_pressed = (s_prev_pressed_mask & s_fret_bit[i]) != 0u;
        uint16_t pc = state->fret[i].confidence;  /* placeholder slot */
        (void)pc;

        /* press_count is private to the detector; for now we infer rising
         * edge from pressed transitions only. If a detector later exposes
         * a press_count via a side channel, plug it in here. */
        if (pressed && !was_pressed) { pm |= s_fret_bit[i]; }
        if (!pressed && was_pressed) { rm |= s_fret_bit[i]; }

        s_prev_press_count[i] = state->fret[i].confidence;
    }

    if (first) { s_press_count_seen = true; }

    s_prev_pressed_mask = cm;
    *presses_mask  = pm;
    *releases_mask = rm;
    *pressed_mask  = cm;
}

static void chord_commit(void)
{
    if (note_q_full() || strum_q_full())
    {
        s_chord_open = false;
        s_chord_mask = 0u;
        return;
    }

    uint32_t assert_at = s_now_ms + TP_STRUM_DELAY_MS - TP_FRET_EARLY_MS;
    uint32_t strum_at  = s_now_ms + TP_STRUM_DELAY_MS;

    if (!strum_q_empty())
    {
        uint8_t  last_idx = (uint8_t)((s_strum_tail + TP_FIFO_CAP - 1u) % TP_FIFO_CAP);
        uint32_t earliest = s_strum_q[last_idx].strum_at_ms + TP_STRUM_PULSE_MS;
        if ((int32_t)(assert_at - earliest) < 0)
        {
            assert_at = earliest;
            strum_at  = assert_at + TP_FRET_EARLY_MS;
        }
    }

    pending_note_t  n = { .assert_at_ms = assert_at, .fret_mask = s_chord_mask };
    pending_strum_t s = { .strum_at_ms  = strum_at,  .fret_mask = s_chord_mask };
    note_q_push(n);
    strum_q_push(s);

    s_chord_open = false;
    s_chord_mask = 0u;
}

static void process_notes(void)
{
    while (!note_q_empty())
    {
        const pending_note_t *n = &s_note_q[s_note_head];
        if ((int32_t)(s_now_ms - n->assert_at_ms) < 0) { break; }

        for (uint8_t i = 0u; i < FRET_COUNT; i++)
        {
            uint8_t bit = s_fret_bit[i];
            bool need = (n->fret_mask & bit) != 0u;
            bool have = (s_frets_active & bit) != 0u;
            if (need && !have)      { s_frets_active |= bit; }
            else if (!need && have) { s_frets_active &= (uint8_t)~bit; }
        }
        note_q_pop();
    }
}

static void process_strums(void)
{
    while (!strum_q_empty())
    {
        const pending_strum_t *s = &s_strum_q[s_strum_head];
        if ((int32_t)(s_now_ms - s->strum_at_ms) < 0) { break; }

        s_strum_active        = true;
        s_strum_direction     = !s_strum_direction;
        s_strum_release_at_ms = s_now_ms + TP_STRUM_PULSE_MS;

        strum_q_pop();
    }
}

static void process_releases(uint8_t live_pressed_mask)
{
    for (uint8_t i = 0u; i < FRET_COUNT; i++)
    {
        uint8_t bit = s_fret_bit[i];
        if ((s_release_pending_mask & bit) == 0u) { continue; }
        if ((int32_t)(s_now_ms - s_release_at_ms[i]) < 0) { continue; }
        if (live_pressed_mask & bit) { continue; }
        if (note_q_any_needs(bit) || strum_q_any_needs(bit)) { continue; }

        s_frets_active        &= (uint8_t)~bit;
        s_release_pending_mask &= (uint8_t)~bit;
    }
}

static void advance(uint8_t live_pressed_mask)
{
    if (s_chord_open && (int32_t)(s_now_ms - s_chord_start_ms) >= (int32_t)TP_CHORD_WINDOW_MS)
    {
        chord_commit();
    }

    if (s_strum_active && (int32_t)(s_now_ms - s_strum_release_at_ms) >= 0)
    {
        s_strum_active = false;
    }

    process_notes();
    process_strums();
    process_releases(live_pressed_mask);

    uint8_t mask = s_frets_active;
    if (s_strum_active)
    {
        mask |= s_strum_direction ? TIMING_BIT_STRUM_UP : TIMING_BIT_STRUM_DOWN;
    }
    publish_mask(mask);
}

static void process_frame(const detector_state_t *state)
{
    /* Detector frame timestamps drive the clock so this is replay-
     * deterministic. timestamp_us is monotonic per spec §4.2.3. */
    s_now_ms = (uint32_t)(state->timestamp_us / 1000ull);
    s_last_frame_us = state->timestamp_us;

    uint8_t presses, releases, pressed_mask;
    derive_edges(state, &presses, &releases, &pressed_mask);

    if (presses)
    {
        if (!s_chord_open)
        {
            s_chord_open     = true;
            s_chord_start_ms = s_now_ms;
            s_chord_mask     = 0u;
        }
        s_chord_mask |= presses;
    }

    for (uint8_t i = 0u; i < FRET_COUNT; i++)
    {
        if (releases & s_fret_bit[i])
        {
            s_release_at_ms[i]      = s_now_ms + TP_STRUM_DELAY_MS;
            s_release_pending_mask |= s_fret_bit[i];
        }
    }

    advance(pressed_mask);
}

static void timing_pipeline_task(void *param)
{
    (void)param;

    QueueHandle_t bus = Detector_BusQueue();
    configASSERT(bus != NULL);

    LOG_INFO("TIMING: pipeline started\r\n");

    for (;;)
    {
        detector_state_t state;
        if (xQueueReceive(bus, &state, pdMS_TO_TICKS(TP_TICK_MS)) == pdTRUE)
        {
            if ((detector_id_t)state.detector_id != Detector_GetActive())
            {
                continue;
            }
            process_frame(&state);
        }
        else
        {
            /* No new frame; advance the clock so deadlines that fell
             * between frames (strum pulses, release timers) still fire. */
            s_now_ms += TP_TICK_MS;
            advance(s_prev_pressed_mask);
        }
    }
}

void TimingPipeline_Initialize(void)
{
    (void)xTaskCreateStatic(timing_pipeline_task,
                            "Timing",
                            TP_TASK_STACK_WORDS,
                            NULL,
                            TP_TASK_PRIORITY,
                            s_task_stack,
                            &s_task_tcb);
}
