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
#include "perf_log/perf_log.h"

#define TP_TASK_STACK_WORDS    768u
#define TP_TASK_PRIORITY       5u

/* Musical scheduling constants — ported from the fret-tuner Python defaults.
 * The observation lead (how early the detector sees a note) is NOT here: the
 * detector stamps detector_state_t.strike_at_ms and the pipeline schedules in
 * that strike-line time base (spec §4.4). */
#define TP_FRET_EARLY_MS       50u   /* press the fret this early vs the strum */
#define TP_STRUM_PULSE_MS      40u   /* strum-bit assert width on the wire */
#define TP_CHORD_WINDOW_MS     30u   /* press-aggregation window */

#define TP_FIFO_CAP            32u

/* Background tick: bound on between-frame latency for strum-pulse and
 * note-assert deadlines that fall between detector publishes. 5 ms keeps
 * jitter well under our 16 ms detector cadence and below human-perceptible
 * timing error. */
#define TP_TICK_MS             2u

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
static uint32_t s_release_at_ms[FRET_COUNT];
static uint8_t  s_release_pending_mask;

static uint8_t  s_output_mask;
static uint32_t s_now_ms;
/* Strike-line clock: the detector-stamped strike time of the current frame,
 * advanced 1:1 with s_now_ms between frames. All output deadlines (assert,
 * strum, release) are scheduled in this base; s_now_ms drives only input
 * aggregation and the emit-anchored strum pulse. */
static uint32_t s_strike_at_ms;
static uint64_t s_last_frame_us;

/* Default OFF: on boot the CV detector is watching a menu, not a note highway, so
 * leaving the pipeline live would actuate spurious frets. Enable it (console
 * `timing on`) once a song is starting. */
static volatile bool s_pipeline_enabled = false;

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

/* Iterator helpers for the release-suppression check below: is this fret
 * needed again by an outstanding entry whose deadline falls at or before
 * `by_ms`? Bounding by the scheduled release time releases the fret between
 * separate notes (clean per-note command) and holds it only across genuinely
 * back-to-back notes, where releasing+repressing would just churn. */
static bool note_q_needs_by(uint8_t bit, uint32_t by_ms)
{
    for (uint8_t i = 0u; i < s_note_count; i++)
    {
        uint8_t idx = (uint8_t)((s_note_head + i) % TP_FIFO_CAP);
        const pending_note_t *n = &s_note_q[idx];
        if ((n->fret_mask & bit) && (int32_t)(n->assert_at_ms - by_ms) <= 0)
        {
            return true;
        }
    }
    return false;
}

static bool strum_q_needs_by(uint8_t bit, uint32_t by_ms)
{
    for (uint8_t i = 0u; i < s_strum_count; i++)
    {
        uint8_t idx = (uint8_t)((s_strum_head + i) % TP_FIFO_CAP);
        const pending_strum_t *s = &s_strum_q[idx];
        if ((s->fret_mask & bit) && (int32_t)(s->strum_at_ms - by_ms) <= 0)
        {
            return true;
        }
    }
    return false;
}

static void publish_mask(uint8_t mask)
{
    s_output_mask = mask;
    if (!s_pipeline_enabled)
    {
        return;
    }
    FretboardLink_Send(mask, (uint8_t)PERF_ACTUATOR_PRODUCER_TIMING);
}

/* Edge derivation:
 *   - presses_mask: rising edge of pressed
 *   - releases_mask: falling edge of pressed */
static void derive_edges(const detector_state_t *state,
                         uint8_t *presses_mask, uint8_t *releases_mask,
                         uint8_t *pressed_mask)
{
    uint8_t pm = 0u;
    uint8_t rm = 0u;
    uint8_t cm = 0u;

    for (uint8_t i = 0u; i < FRET_COUNT; i++)
    {
        bool pressed = state->fret[i].pressed != 0u;
        if (pressed) { cm |= s_fret_bit[i]; }

        bool was_pressed = (s_prev_pressed_mask & s_fret_bit[i]) != 0u;
        if (pressed && !was_pressed) { pm |= s_fret_bit[i]; }
        if (!pressed && was_pressed) { rm |= s_fret_bit[i]; }
    }

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

    uint32_t assert_at = s_strike_at_ms - TP_FRET_EARLY_MS;
    uint32_t strum_at  = s_strike_at_ms;

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

static void process_notes(uint32_t fire_now)
{
    while (!note_q_empty())
    {
        const pending_note_t *n = &s_note_q[s_note_head];
        if ((int32_t)(fire_now - n->assert_at_ms) < 0) { break; }

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

static void process_strums(uint32_t fire_now)
{
    while (!strum_q_empty())
    {
        const pending_strum_t *s = &s_strum_q[s_strum_head];
        if ((int32_t)(fire_now - s->strum_at_ms) < 0) { break; }

        s_strum_active        = true;
        s_strum_direction     = !s_strum_direction;
        s_strum_release_at_ms = s_now_ms + TP_STRUM_PULSE_MS;

        strum_q_pop();
    }
}

static void process_releases(uint8_t live_pressed_mask, uint32_t fire_now)
{
    for (uint8_t i = 0u; i < FRET_COUNT; i++)
    {
        uint8_t bit = s_fret_bit[i];
        if ((s_release_pending_mask & bit) == 0u) { continue; }
        if ((int32_t)(fire_now - s_release_at_ms[i]) < 0) { continue; }
        if (live_pressed_mask & bit) { continue; }
        /* Hold only if re-needed at/before this release (back-to-back); a need
         * further out releases now for a clean per-note command. */
        if (note_q_needs_by(bit, s_release_at_ms[i]) ||
            strum_q_needs_by(bit, s_release_at_ms[i])) { continue; }

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

    /* Emit strike-line deadlines FRETBOARD_ACTUATOR_ADVANCE_MS early so the
     * actuator's mechanical latency lands the effect on the strike line. The
     * strum pulse release stays on s_now_ms — it's anchored to the (already
     * advanced) emit instant, so its wire pulse width is preserved. 0 for the
     * open-drain guitar node, making this behavior-preserving there. */
    uint32_t fire_now = s_now_ms + FRETBOARD_ACTUATOR_ADVANCE_MS;

    if (s_strum_active && (int32_t)(s_now_ms - s_strum_release_at_ms) >= 0)
    {
        s_strum_active = false;
    }

    process_notes(fire_now);
    process_strums(fire_now);
    process_releases(live_pressed_mask, fire_now);

    uint8_t mask = s_frets_active;
    if (s_strum_active)
    {
        mask |= s_strum_direction ? TIMING_BIT_STRUM_UP : TIMING_BIT_STRUM_DOWN;
    }
    publish_mask(mask);
}

/* Build the per-frame TIMING snapshot from current pipeline state. Called
 * once per process_frame after advance() has settled all queues and the
 * publish_mask. Deadlines for empty queues are zeroed; host inspects the
 * count fields before treating *_at_ms as meaningful. */
static void fill_timing_snapshot(perf_timing_snapshot_t *snap)
{
    memset(snap, 0, sizeof(*snap));
    snap->now_ms = s_now_ms;

    snap->chord_open   = s_chord_open ? 1u : 0u;
    snap->chord_mask   = s_chord_open ? s_chord_mask : 0u;
    snap->chord_age_ms = s_chord_open
                       ? (uint16_t)(s_now_ms - s_chord_start_ms)
                       : 0u;

    snap->note_q_count = s_note_count;
    if (s_note_count > 0u)
    {
        const pending_note_t *head = &s_note_q[s_note_head];
        snap->note_head_mask  = head->fret_mask;
        snap->note_head_at_ms = head->assert_at_ms;
        uint8_t tail_union = 0u;
        for (uint8_t i = 0u; i < s_note_count; i++)
        {
            uint8_t idx = (uint8_t)((s_note_head + i) % TP_FIFO_CAP);
            tail_union |= s_note_q[idx].fret_mask;
        }
        snap->note_tail_mask = tail_union;
    }

    snap->strum_q_count = s_strum_count;
    if (s_strum_count > 0u)
    {
        const pending_strum_t *head = &s_strum_q[s_strum_head];
        snap->strum_head_mask  = head->fret_mask;
        snap->strum_head_at_ms = head->strum_at_ms;
    }
    /* strum_dir toggles each fired strum; report the *next* direction
     * (what would fire if the front of strum_q reaches its deadline now). */
    snap->strum_dir_next = s_strum_direction ? 2u /*up*/ : 1u /*down*/;

    snap->frets_active         = s_frets_active;
    snap->strum_active         = s_strum_active ? 1u : 0u;
    snap->release_pending_mask = s_release_pending_mask;
    snap->publish_mask         = s_output_mask;
    snap->strum_release_at_ms  = s_strum_active ? s_strum_release_at_ms : 0u;

    if (s_release_pending_mask != 0u)
    {
        uint32_t min_at = 0xFFFFFFFFu;
        for (uint8_t i = 0u; i < FRET_COUNT; i++)
        {
            if ((s_release_pending_mask & s_fret_bit[i]) == 0u) { continue; }
            if (s_release_at_ms[i] < min_at) { min_at = s_release_at_ms[i]; }
        }
        snap->release_min_at_ms = (min_at == 0xFFFFFFFFu) ? 0u : min_at;
    }
}

static void process_frame(const detector_state_t *state)
{
    /* Detector frame timestamps drive the clock so this is replay-
     * deterministic. timestamp_us is monotonic per spec §4.2.3. */
    s_now_ms = (uint32_t)(state->timestamp_us / 1000ull);
    s_strike_at_ms = state->strike_at_ms;
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
            s_release_at_ms[i]      = s_strike_at_ms;
            s_release_pending_mask |= s_fret_bit[i];
        }
    }

    advance(pressed_mask);

    PerfLog_EmitStamp(PERF_STAGE_TP_TICK, state->frame_epoch,
                      (uint32_t)s_output_mask);

    perf_timing_snapshot_t snap;
    fill_timing_snapshot(&snap);
    PerfLog_EmitTiming(state->frame_epoch, &snap);
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
            /* No new frame; advance both clocks in lockstep so deadlines that
             * fell between frames (strum pulses, release timers) still fire and
             * the strike-line base stays referenced to now. */
            s_now_ms += TP_TICK_MS;
            s_strike_at_ms += TP_TICK_MS;
            advance(s_prev_pressed_mask);
        }
    }
}

void TimingPipeline_Initialize(void)
{
    TaskHandle_t h = xTaskCreateStatic(timing_pipeline_task,
                                       "Timing",
                                       TP_TASK_STACK_WORDS,
                                       NULL,
                                       TP_TASK_PRIORITY,
                                       s_task_stack,
                                       &s_task_tcb);
    PerfLog_RegisterTaskForHighwater(PERF_TASK_TIMING, h);
}

void TimingPipeline_SetEnabled(bool enabled)
{
    if (enabled == s_pipeline_enabled)
    {
        return;
    }
    s_pipeline_enabled = enabled;
    if (!enabled)
    {
        /* Release the wire on disable so no frets stay held (the actuator's own
         * heartbeat would otherwise keep re-sending the last mask). */
        FretboardLink_Send(0u, (uint8_t)PERF_ACTUATOR_PRODUCER_TIMING);
    }
}

bool TimingPipeline_IsEnabled(void)
{
    return s_pipeline_enabled;
}
