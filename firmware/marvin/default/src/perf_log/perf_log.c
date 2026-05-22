#include "perf_log.h"
#include "perf_log_records.h"
#include "perf_log_sink.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "definitions.h"
#include "log.h"

/* ─── Configuration ──────────────────────────────────────────────────────── */

#define PL_DRAIN_STACK_WORDS    512u                 /* 2 KB */
#define PL_DRAIN_PRIORITY       3u

#define PL_STATE_QUEUE_DEPTH    128u
#define PL_PATCH_QUEUE_DEPTH    8u

/* State-queue slot is sized to the largest small record. */
#define PL_STATE_SLOT_BYTES     (sizeof(perf_rec_state_slot_t))
#define PL_PATCH_SLOT_BYTES     (sizeof(perf_rec_patch_t))

#define PL_DROP_REPORT_PERIOD_MS   1000u
#define PL_DRAIN_RX_TIMEOUT_MS     20u

/* ─── Internal types ─────────────────────────────────────────────────────── */

/* Discriminated slot for the state queue. The drain task switches on
 * hdr.type and writes only the populated bytes. Sized to the largest
 * small record so all types fit in one slot. */
typedef union
{
    perf_hdr_t                 hdr;
    perf_rec_session_t         session;
    perf_rec_stamp_t           stamp;
    perf_rec_detector_t        detector;
    perf_rec_timing_t          timing;
    perf_rec_drop_t            drop;
    perf_rec_task_highwater_t  hwm;
} perf_rec_state_slot_t;

/* ─── Static storage ─────────────────────────────────────────────────────── */

static QueueHandle_t s_state_q;
static StaticQueue_t s_state_q_buf;
static uint8_t       s_state_q_storage[PL_STATE_QUEUE_DEPTH * sizeof(perf_rec_state_slot_t)];

static QueueHandle_t s_patch_q;
static StaticQueue_t s_patch_q_buf;
static uint8_t       s_patch_q_storage[PL_PATCH_QUEUE_DEPTH * sizeof(perf_rec_patch_t)];

static StackType_t   s_drain_stack[PL_DRAIN_STACK_WORDS];
static StaticTask_t  s_drain_tcb;

#define PL_TASK_SLOT_COUNT  6u   /* one per perf_task_id_t */
static TaskHandle_t  s_task_handles[PL_TASK_SLOT_COUNT];

static volatile uint32_t s_drop_state;
static volatile uint32_t s_drop_patch;
static volatile uint32_t s_drop_sink;

static volatile bool s_running;

/* ─── Header fill ────────────────────────────────────────────────────────── */

static inline void hdr_fill(perf_hdr_t *h, uint8_t type, uint8_t flags,
                            uint32_t frame_epoch)
{
    h->magic       = PERF_LOG_HDR_MAGIC;
    h->type        = type;
    h->flags       = flags;
    h->frame_epoch = frame_epoch;
    h->ts_counter  = SYS_TIME_Counter64Get();
}

/* ─── Record-size dispatch ───────────────────────────────────────────────── */

static uint16_t record_size(const perf_rec_state_slot_t *r)
{
    switch (r->hdr.type)
    {
        case PERF_REC_SESSION:        return (uint16_t)sizeof(perf_rec_session_t);
        case PERF_REC_STAMP:          return (uint16_t)sizeof(perf_rec_stamp_t);
        case PERF_REC_DETECTOR:       return (uint16_t)sizeof(perf_rec_detector_t);
        case PERF_REC_TIMING:         return (uint16_t)sizeof(perf_rec_timing_t);
        case PERF_REC_DROP:           return (uint16_t)sizeof(perf_rec_drop_t);
        case PERF_REC_TASK_HIGHWATER: return (uint16_t)sizeof(perf_rec_task_highwater_t);
        default:                      return (uint16_t)sizeof(perf_hdr_t);
    }
}

/* ─── Drain task ─────────────────────────────────────────────────────────── */

static void emit_session_record(void)
{
    perf_rec_session_t r;
    memset(&r, 0, sizeof(r));
    hdr_fill(&r.hdr, PERF_REC_SESSION, 0u, 0u);
    r.timer_freq_hz  = SYS_TIME_FrequencyGet();
    r.schema_version = (uint16_t)PERF_LOG_SCHEMA_VERSION;
    PerfLogSinkCdc_WriteFramed(&r, (uint16_t)sizeof(r));
}

static void emit_drop_record(void)
{
    perf_rec_drop_t r;
    memset(&r, 0, sizeof(r));
    hdr_fill(&r.hdr, PERF_REC_DROP, 0u, 0u);
    r.dropped_state = s_drop_state;
    r.dropped_patch = s_drop_patch;
    r.dropped_sink  = s_drop_sink;
    PerfLogSinkCdc_WriteFramed(&r, (uint16_t)sizeof(r));
}

static void sample_and_emit_hwms(void)
{
    for (uint8_t i = 0u; i < PL_TASK_SLOT_COUNT; i++)
    {
        TaskHandle_t h = s_task_handles[i];
        if (h == NULL) { continue; }
        uint32_t words = (uint32_t)uxTaskGetStackHighWaterMark(h);
        PerfLog_EmitTaskHighwater((perf_task_id_t)i, words);
    }
}

static void perf_log_drain_task(void *param)
{
    (void)param;

    PerfLogSinkCdc_Initialize();
    s_running = true;

    TickType_t last_drop = xTaskGetTickCount();
    bool prev_connected = false;

    for (;;)
    {
        bool now_connected = PerfLogSinkCdc_IsConnected();
        if (now_connected && !prev_connected)
        {
            emit_session_record();
        }
        prev_connected = now_connected;

        perf_rec_state_slot_t srec;
        if (xQueueReceive(s_state_q, &srec,
                          pdMS_TO_TICKS(PL_DRAIN_RX_TIMEOUT_MS)) == pdTRUE)
        {
            PerfLogSinkCdc_WriteFramed(&srec, record_size(&srec));
        }

        perf_rec_patch_t prec;
        while (xQueueReceive(s_patch_q, &prec, 0) == pdTRUE)
        {
            PerfLogSinkCdc_WriteFramed(&prec, (uint16_t)sizeof(prec));
        }

        if ((xTaskGetTickCount() - last_drop) >= pdMS_TO_TICKS(PL_DROP_REPORT_PERIOD_MS))
        {
            last_drop = xTaskGetTickCount();
            emit_drop_record();
            sample_and_emit_hwms();
        }
    }
}

/* ─── Init ───────────────────────────────────────────────────────────────── */

void PerfLog_Initialize(void)
{
    s_state_q = xQueueCreateStatic(PL_STATE_QUEUE_DEPTH,
                                   sizeof(perf_rec_state_slot_t),
                                   s_state_q_storage,
                                   &s_state_q_buf);
    configASSERT(s_state_q != NULL);

    s_patch_q = xQueueCreateStatic(PL_PATCH_QUEUE_DEPTH,
                                   sizeof(perf_rec_patch_t),
                                   s_patch_q_storage,
                                   &s_patch_q_buf);
    configASSERT(s_patch_q != NULL);
}

void PerfLog_Start(void)
{
    TaskHandle_t h = xTaskCreateStatic(perf_log_drain_task,
                                       "PerfDrain",
                                       PL_DRAIN_STACK_WORDS,
                                       NULL,
                                       PL_DRAIN_PRIORITY,
                                       s_drain_stack,
                                       &s_drain_tcb);
    PerfLog_RegisterTaskForHighwater(PERF_TASK_PERF_DRAIN, h);
}

void PerfLog_RegisterTaskForHighwater(perf_task_id_t id, TaskHandle_t handle)
{
    if ((unsigned)id >= PL_TASK_SLOT_COUNT) { return; }
    s_task_handles[id] = handle;
}

bool PerfLog_IsRunning(void)
{
    return s_running;
}

/* ─── Emit (task context) ────────────────────────────────────────────────── */

/* Drop counters are RMW from both task (send_state, EmitPatch,
 * NoteSinkDrop) and ISR (EmitStampFromISR) contexts. ARM926 single-
 * issue makes a single 32-bit aligned store atomic, but the increment
 * is LDR/ADD/STR — an ISR firing between LDR and STR loses an increment.
 * Critical sections (which globally disable IRQs on this port) keep
 * the RMW indivisible. The counters are diagnostic, so the very brief
 * IRQ-off window is fine. */
static inline void counter_add_task(volatile uint32_t *p, uint32_t v)
{
    taskENTER_CRITICAL();
    *p += v;
    taskEXIT_CRITICAL();
}

static inline void counter_add_isr(volatile uint32_t *p, uint32_t v)
{
    UBaseType_t s = taskENTER_CRITICAL_FROM_ISR();
    *p += v;
    taskEXIT_CRITICAL_FROM_ISR(s);
}

static inline void send_state(const perf_rec_state_slot_t *slot)
{
    if (s_state_q == NULL) { return; }
    if (xQueueSend(s_state_q, slot, 0) != pdTRUE)
    {
        counter_add_task(&s_drop_state, 1u);
    }
}

void PerfLog_EmitStamp(perf_stage_t stage, uint32_t frame_epoch, uint32_t aux)
{
    perf_rec_state_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    hdr_fill(&slot.stamp.hdr, PERF_REC_STAMP, 0u, frame_epoch);
    slot.stamp.stage_id = (uint8_t)stage;
    slot.stamp.aux      = aux;
    send_state(&slot);
}

void PerfLog_EmitDetector(uint32_t frame_epoch,
                          const uint16_t hold_dist[FRET_COUNT],
                          const uint16_t edge_dist[FRET_COUNT],
                          uint8_t  pressed_mask,
                          uint8_t  edge_active_mask)
{
    perf_rec_state_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    hdr_fill(&slot.detector.hdr, PERF_REC_DETECTOR, 0u, frame_epoch);
    memcpy(slot.detector.hold_dist, hold_dist, sizeof(slot.detector.hold_dist));
    memcpy(slot.detector.edge_dist, edge_dist, sizeof(slot.detector.edge_dist));
    slot.detector.pressed_mask     = pressed_mask;
    slot.detector.edge_active_mask = edge_active_mask;
    send_state(&slot);
}

void PerfLog_EmitTiming(uint32_t frame_epoch,
                        uint8_t  publish_mask,
                        uint8_t  chord_window_fill,
                        uint8_t  fifo_depth,
                        uint8_t  strum_dir)
{
    perf_rec_state_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    hdr_fill(&slot.timing.hdr, PERF_REC_TIMING, 0u, frame_epoch);
    slot.timing.publish_mask      = publish_mask;
    slot.timing.chord_window_fill = chord_window_fill;
    slot.timing.fifo_depth        = fifo_depth;
    slot.timing.strum_dir         = strum_dir;
    send_state(&slot);
}

void PerfLog_EmitTaskHighwater(perf_task_id_t id, uint32_t words)
{
    perf_rec_state_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    hdr_fill(&slot.hwm.hdr, PERF_REC_TASK_HIGHWATER, 0u, 0u);
    slot.hwm.task_id = (uint8_t)id;
    slot.hwm.words   = words;
    send_state(&slot);
}

void PerfLog_EmitPatch(uint32_t frame_epoch,
                       uint16_t frame_w, uint16_t frame_h,
                       const perf_patch_fret_t fret[FRET_COUNT])
{
    if (s_patch_q == NULL) { return; }
    perf_rec_patch_t r;
    memset(&r, 0, sizeof(r));
    hdr_fill(&r.hdr, PERF_REC_PATCH, 0u, frame_epoch);
    r.frame_w = frame_w;
    r.frame_h = frame_h;
    memcpy(r.fret, fret, sizeof(r.fret));
    if (xQueueSend(s_patch_q, &r, 0) != pdTRUE)
    {
        counter_add_task(&s_drop_patch, 1u);
    }
}

/* ─── Emit (ISR context) ─────────────────────────────────────────────────── */

void PerfLog_EmitStampFromISR(perf_stage_t stage,
                              uint32_t frame_epoch,
                              uint32_t aux,
                              BaseType_t *higher_priority_task_woken)
{
    if (s_state_q == NULL) { return; }
    perf_rec_state_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    hdr_fill(&slot.stamp.hdr, PERF_REC_STAMP, PERF_FLAG_FROM_ISR, frame_epoch);
    slot.stamp.stage_id = (uint8_t)stage;
    slot.stamp.aux      = aux;
    if (xQueueSendFromISR(s_state_q, &slot, higher_priority_task_woken) != pdTRUE)
    {
        counter_add_isr(&s_drop_state, 1u);
    }
}

/* ─── Sink-side drop accounting ──────────────────────────────────────────── */

void PerfLog_NoteSinkDrop(uint32_t bytes_dropped)
{
    counter_add_task(&s_drop_sink, bytes_dropped);
}
