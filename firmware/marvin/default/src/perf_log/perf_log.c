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
#include "video/video.h"

/* ─── Configuration ──────────────────────────────────────────────────────── */

#define PL_DRAIN_STACK_WORDS    512u                 /* 2 KB */
#define PL_DRAIN_PRIORITY       3u

#define PL_STATE_QUEUE_DEPTH    1024u
#define PL_STRIP_QUEUE_DEPTH    4u

/* Strip slots live in a static pool; the strip queue and free list carry
 * just slot indices. Pool size = queue depth + 2 leaves room for one slot
 * being filled by the producer and one being processed by the drain at
 * the moment the queue is full, preserving the old drop-on-queue-full
 * semantics without ever stalling either side. */
#define PL_STRIP_POOL_SIZE      (PL_STRIP_QUEUE_DEPTH + 2u)

/* State-queue slot is sized to the largest small record. */
#define PL_STATE_SLOT_BYTES     (sizeof(perf_rec_state_slot_t))

#define PL_DROP_REPORT_PERIOD_MS   1000u
#define PL_DRAIN_RX_TIMEOUT_MS     20u

/* ─── Internal types ─────────────────────────────────────────────────────── */

/* Discriminated slot for the state queue. The drain task switches on
 * hdr.type and writes only the populated bytes. Sized to the largest
 * small record so all types fit in one slot — DETECTOR_CONFIG is by far
 * the largest at ~188 B; the others fit in 32-51 B. The slot grew with
 * v3; queue depth × slot size is still trivial against 240 MB DDR. */
typedef union
{
    perf_hdr_t                  hdr;
    perf_rec_session_t          session;
    perf_rec_stamp_t            stamp;
    perf_rec_detector_t         detector;
    perf_rec_timing_t           timing;
    perf_rec_drop_t             drop;
    perf_rec_task_highwater_t   hwm;
    perf_rec_task_runtime_t     runtime;
    perf_rec_detector_config_t  detector_config;
    perf_rec_actuator_t         actuator;
    perf_rec_fretboard_raw_t    fretboard_raw;
} perf_rec_state_slot_t;

/* ─── Static storage ─────────────────────────────────────────────────────── */

static QueueHandle_t s_state_q;
static StaticQueue_t s_state_q_buf;
static uint8_t       s_state_q_storage[PL_STATE_QUEUE_DEPTH * sizeof(perf_rec_state_slot_t)];

/* Strip pool: data lives here from producer fill through drain write to the
 * sink — no copy at queue ops. Both queues carry uint8_t slot indices. The
 * "in-flight" queue holds full slots awaiting drain; the "free" queue holds
 * available slots for the producer to claim. */
static perf_rec_strip_t s_strip_pool[PL_STRIP_POOL_SIZE];

static QueueHandle_t s_strip_q;
static StaticQueue_t s_strip_q_buf;
static uint8_t       s_strip_q_storage[PL_STRIP_QUEUE_DEPTH * sizeof(uint8_t)];

static QueueHandle_t s_strip_free_q;
static StaticQueue_t s_strip_free_q_buf;
static uint8_t       s_strip_free_q_storage[PL_STRIP_POOL_SIZE * sizeof(uint8_t)];

static StackType_t   s_drain_stack[PL_DRAIN_STACK_WORDS];
static StaticTask_t  s_drain_tcb;

#define PL_TASK_SLOT_COUNT  18u  /* one per perf_task_id_t — IDLE + 9 MCC slots + OTHER + FRETBOARD_RX */
static TaskHandle_t  s_task_handles[PL_TASK_SLOT_COUNT];

static volatile uint32_t s_drop_state;
static volatile uint32_t s_drop_strip;
static volatile uint32_t s_drop_sink;

static volatile bool s_running;

/* One-shot full-frame snapshot. The RX path latches s_snapshot_req; the drain
 * task copies the current frame into s_snapshot_staging (one coherent copy out
 * of the rotating nocache ring) and pages it out as full-width SNAPSHOT bands.
 * Staging is sized to the max capture (1280×720×3); the scratch strip record is
 * static because it's ~65 KB — far too large for the 2 KB drain stack. */
static volatile bool    s_snapshot_req;
static uint8_t          s_snapshot_staging[1280u * 720u * PERF_STRIP_BPP];
static perf_rec_strip_t s_snapshot_scratch;

/* Boot with only the cheap diagnostic types enabled: DROP (1 Hz),
 * TASK_HIGHWATER (~6/s), TASK_RUNTIME (~6/s). The host viewer enables
 * higher-rate types (STAMP, DETECTOR, TIMING, STRIP) via
 * PERF_CMD_SET_TYPE_MASK once it's ready to consume them. SESSION is
 * always emitted regardless of mask.
 *
 * Why default-off for the high-rate types — and STRIP especially:
 * before a host attaches and clears the wire, every emitted record
 * burns producer CPU (queue ops, framing, Fletcher) only to be dropped
 * at the sink for lack of DTR. STRIP at 60 Hz is the worst case
 * (multiple MB/s of pixel data thrown away) but STAMP isn't free either
 * (~700/s of state-queue traffic). */
static volatile uint32_t s_enabled_mask =
    (1u << PERF_REC_DROP) |
    (1u << PERF_REC_TASK_HIGHWATER) |
    (1u << PERF_REC_TASK_RUNTIME);

static inline bool type_enabled(uint8_t type)
{
    if (type == PERF_REC_SESSION) { return true; }
    return (s_enabled_mask & (1u << type)) != 0u;
}

/* Overlay sinks for the per-fret target rings (PERF_OVERLAY_* bits). Read
 * lock-free by the CV producer; set by PERF_CMD_SET_OVERLAY. Default: rings on
 * the viewer strips, capture buffer untouched. */
static volatile uint32_t s_overlay_flags = PERF_OVERLAY_STRIP;

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
        case PERF_REC_SESSION:         return (uint16_t)sizeof(perf_rec_session_t);
        case PERF_REC_STAMP:           return (uint16_t)sizeof(perf_rec_stamp_t);
        case PERF_REC_DETECTOR:        return (uint16_t)sizeof(perf_rec_detector_t);
        case PERF_REC_TIMING:          return (uint16_t)sizeof(perf_rec_timing_t);
        case PERF_REC_DROP:            return (uint16_t)sizeof(perf_rec_drop_t);
        case PERF_REC_TASK_HIGHWATER:  return (uint16_t)sizeof(perf_rec_task_highwater_t);
        case PERF_REC_TASK_RUNTIME:    return (uint16_t)sizeof(perf_rec_task_runtime_t);
        case PERF_REC_DETECTOR_CONFIG: return (uint16_t)sizeof(perf_rec_detector_config_t);
        case PERF_REC_ACTUATOR:        return (uint16_t)sizeof(perf_rec_actuator_t);
        case PERF_REC_FRETBOARD_RAW:   return (uint16_t)sizeof(perf_rec_fretboard_raw_t);
        default:                       return (uint16_t)sizeof(perf_hdr_t);
    }
}

/* Variable-length: queue slot is max-sized, but on the wire we send only
 * HDR + body + the actually-used pixel bytes. */
static uint16_t strip_record_size(const perf_rec_strip_t *r)
{
    uint32_t pix = (uint32_t)r->w * (uint32_t)r->h * PERF_STRIP_BPP;
    if (pix > PERF_STRIP_MAX_BYTES) { pix = PERF_STRIP_MAX_BYTES; }
    return (uint16_t)(PERF_STRIP_HDR_BYTES + pix);
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
    r.dropped_strip = s_drop_strip;
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

/* Map FreeRTOS eTaskState → wire enum. Same numeric ordering today, but
 * cast through this so a future RTOS-side enum reshuffle doesn't silently
 * skew the wire format. */
static perf_task_state_t map_task_state(eTaskState s)
{
    switch (s)
    {
        case eRunning:   return PERF_TASK_STATE_RUNNING;
        case eReady:     return PERF_TASK_STATE_READY;
        case eBlocked:   return PERF_TASK_STATE_BLOCKED;
        case eSuspended: return PERF_TASK_STATE_SUSPENDED;
        case eDeleted:   return PERF_TASK_STATE_DELETED;
        default:         return PERF_TASK_STATE_INVALID;
    }
}

/* Sized to comfortably exceed total live task count: marvin owns ~7
 * (CV / detector-drain / timing / fretboard-link / video / perf-drain /
 * idle) plus MCC-driven tasks (XLCDC, MAXTOUCH, LEGATO, SYS_INPUT, USB
 * host EHCI/OHCI/HUB/CDC) — call it ≤ 20 today, bump if we ever overflow. */
#define PL_RUNTIME_TASK_BUF  24u

static void sample_and_emit_runtimes(void)
{
    static TaskStatus_t buf[PL_RUNTIME_TASK_BUF];
    UBaseType_t n = uxTaskGetSystemState(buf, PL_RUNTIME_TASK_BUF, NULL);

    uint64_t sum_all = 0u, sum_registered = 0u;

    for (UBaseType_t k = 0u; k < n; k++)
    {
        const TaskStatus_t *t = &buf[k];
        sum_all += (uint64_t)t->ulRunTimeCounter;

        for (uint8_t i = 0u; i < PL_TASK_SLOT_COUNT; i++)
        {
            if (s_task_handles[i] != t->xHandle) { continue; }
            sum_registered += (uint64_t)t->ulRunTimeCounter;
            PerfLog_EmitTaskRuntime((perf_task_id_t)i,
                                    map_task_state(t->eCurrentState),
                                    (uint8_t)t->uxCurrentPriority,
                                    (uint32_t)t->ulRunTimeCounter);
            break;
        }
    }

    /* OTHER pseudo-slot: every cycle not accounted to a registered task
     * (including IDLE, which is registered). Lets the host close the
     * books — Σ per-window CPU% = 100. State/priority don't have a
     * single value here; emit READY/0 as harmless placeholders. */
    PerfLog_EmitTaskRuntime(PERF_TASK_OTHER,
                            PERF_TASK_STATE_READY,
                            0u,
                            (uint32_t)(sum_all - sum_registered));
}

/* MCC tasks are created in SYS_Tasks() right before vTaskStartScheduler,
 * not at SYS_Initialize time — so the handles aren't lookup-able from
 * APP_Initialize where PerfLog_Start runs. Same goes for the FreeRTOS
 * idle task: xTaskGetIdleTaskHandle() returns NULL until vTaskStartScheduler
 * creates it. The drain task runs after the scheduler is up and all
 * SYS_Tasks() xTaskCreate calls have landed; it does both lookups here.
 *
 * Names match the pcName argument in default/tasks.c xTaskCreate calls.
 * If MCC ever renames one (regen risk), xTaskGetHandle returns NULL and
 * that slot stays unregistered — its CPU runtime falls into OTHER, and
 * the host viewer sees a flat-zero series for that task as a hint. */
static const struct
{
    perf_task_id_t id;
    const char    *name;
} s_mcc_task_names[] =
{
    { PERF_TASK_LEGATO,        "LEGATO_Tasks"        },
    { PERF_TASK_XLCDC,         "XLCDC_Tasks"         },
    { PERF_TASK_MAXTOUCH,      "DRV_MAXTOUCH_Tasks"  },
    { PERF_TASK_SYS_INPUT,     "SYS_INPUT_Tasks"     },
    { PERF_TASK_USB_DEVICE,    "USB_DEVICE_TASKS"    },
    { PERF_TASK_USB_HOST,      "USB_HOST_TASKS"      },
    { PERF_TASK_DRV_USB_UDPHS, "DRV_USB_UDPHS_TASKS" },
    { PERF_TASK_DRV_USB_HOST,  "DRV_USB_HOST_TASKS"  },
    { PERF_TASK_APP,           "APP_Tasks"           },
};

static void register_post_scheduler_tasks(void)
{
    TaskHandle_t idle = xTaskGetIdleTaskHandle();
    if (idle != NULL)
    {
        PerfLog_RegisterTaskForHighwater(PERF_TASK_IDLE, idle);
    }
    else
    {
        LOG_WARN("PerfLog: idle task handle not available\r\n");
    }

    for (uint8_t i = 0u; i < sizeof(s_mcc_task_names) / sizeof(s_mcc_task_names[0]); i++)
    {
        TaskHandle_t h = xTaskGetHandle(s_mcc_task_names[i].name);
        if (h != NULL)
        {
            PerfLog_RegisterTaskForHighwater(s_mcc_task_names[i].id, h);
        }
        else
        {
            LOG_WARN("PerfLog: MCC task '%s' not found\r\n", s_mcc_task_names[i].name);
        }
    }
}

/* Stream the current video frame back as full-width SNAPSHOT bands. Runs in
 * the drain task: writes each band directly to the sink (bypassing the small
 * strip pool, which can't hold 16-45 bands), naturally paced by the wire. This
 * briefly monopolizes the drain (~0.4 s @480p, ~1 s @720p) — fine for an
 * on-demand debug capture. */
static void emit_snapshot(void)
{
    Video_FrameInfo vi;
    Video_GetFrameInfo(&vi);
    if (vi.buffer == NULL || vi.width == 0u || vi.height == 0u) { return; }
    if (vi.bytes_per_pixel != PERF_STRIP_BPP) { return; }

    const uint16_t w      = vi.width;
    const uint16_t h      = vi.height;
    const uint32_t stride = (uint32_t)w * PERF_STRIP_BPP;
    const uint32_t frame_bytes = stride * (uint32_t)h;
    if (frame_bytes > sizeof(s_snapshot_staging)) { return; }

    memcpy(s_snapshot_staging, vi.buffer, frame_bytes);

    uint32_t band_rows = PERF_STRIP_MAX_BYTES / stride;
    if (band_rows == 0u) { return; }              /* frame too wide for a strip */
    if (band_rows > h)   { band_rows = h; }

    for (uint16_t y = 0u; y < h; )
    {
        uint16_t bh = (uint16_t)(((uint32_t)(h - y) < band_rows) ? (h - y) : band_rows);

        perf_rec_strip_t *r = &s_snapshot_scratch;
        memset(r, 0, PERF_STRIP_HDR_BYTES);
        hdr_fill(&r->hdr, PERF_REC_STRIP, 0u, vi.frame_count);
        r->x     = 0u;
        r->y     = y;
        r->w     = w;
        r->h     = bh;
        r->kind  = (uint8_t)PERF_STRIP_SNAPSHOT;
        r->flags = ((uint32_t)y + bh >= h) ? PERF_STRIP_FLAG_LAST : 0u;
        memcpy(r->bgr, &s_snapshot_staging[(uint32_t)y * stride], (uint32_t)bh * stride);

        PerfLogSinkCdc_WriteFramed(r, strip_record_size(r));
        y = (uint16_t)(y + bh);
    }
}

static void perf_log_drain_task(void *param)
{
    (void)param;

    PerfLogSinkCdc_Initialize();
    register_post_scheduler_tasks();
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

        if (s_snapshot_req)
        {
            s_snapshot_req = false;
            emit_snapshot();
        }

        perf_rec_state_slot_t srec;
        if (xQueueReceive(s_state_q, &srec,
                          pdMS_TO_TICKS(PL_DRAIN_RX_TIMEOUT_MS)) == pdTRUE)
        {
            PerfLogSinkCdc_WriteFramed(&srec, record_size(&srec));
            /* Drain all currently queued state records before moving on.
             * Strips cost ~5 ms apiece on the wire, so a one-state-per-
             * iteration loop runs at ~50 Hz under load — well below the
             * ~700 stamps/s production rate. Same shape as the strip
             * drain below. */
            while (xQueueReceive(s_state_q, &srec, 0) == pdTRUE)
            {
                PerfLogSinkCdc_WriteFramed(&srec, record_size(&srec));
            }
        }

        /* Strip drain: dequeue slot index, write the slot's data via
         * pointer (no copy), return slot to the free pool. */
        uint8_t slot_idx;
        while (xQueueReceive(s_strip_q, &slot_idx, 0) == pdTRUE)
        {
            const perf_rec_strip_t *r = &s_strip_pool[slot_idx];
            PerfLogSinkCdc_WriteFramed(r, strip_record_size(r));
            (void)xQueueSend(s_strip_free_q, &slot_idx, 0);
        }

        if ((xTaskGetTickCount() - last_drop) >= pdMS_TO_TICKS(PL_DROP_REPORT_PERIOD_MS))
        {
            last_drop = xTaskGetTickCount();
            emit_drop_record();
            sample_and_emit_hwms();
            sample_and_emit_runtimes();
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

    s_strip_q = xQueueCreateStatic(PL_STRIP_QUEUE_DEPTH,
                                   sizeof(uint8_t),
                                   s_strip_q_storage,
                                   &s_strip_q_buf);
    configASSERT(s_strip_q != NULL);

    s_strip_free_q = xQueueCreateStatic(PL_STRIP_POOL_SIZE,
                                        sizeof(uint8_t),
                                        s_strip_free_q_storage,
                                        &s_strip_free_q_buf);
    configASSERT(s_strip_free_q != NULL);

    /* Seed the free list with all pool indices. */
    for (uint8_t i = 0u; i < PL_STRIP_POOL_SIZE; i++)
    {
        (void)xQueueSend(s_strip_free_q, &i, 0);
    }
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
    /* Idle handle isn't valid until vTaskStartScheduler creates the idle
     * task — registered from the drain task post-scheduler instead, see
     * register_post_scheduler_tasks. */
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
    if (!type_enabled(slot->hdr.type)) { return; }
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

void PerfLog_EmitTiming(uint32_t frame_epoch, const perf_timing_snapshot_t *s)
{
    if (s == NULL) { return; }
    perf_rec_state_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    hdr_fill(&slot.timing.hdr, PERF_REC_TIMING, 0u, frame_epoch);
    slot.timing.now_ms               = s->now_ms;
    slot.timing.chord_open           = s->chord_open;
    slot.timing.chord_mask           = s->chord_mask;
    slot.timing.chord_age_ms         = s->chord_age_ms;
    slot.timing.note_q_count         = s->note_q_count;
    slot.timing.note_head_mask       = s->note_head_mask;
    slot.timing.note_tail_mask       = s->note_tail_mask;
    slot.timing.note_head_at_ms      = s->note_head_at_ms;
    slot.timing.strum_q_count        = s->strum_q_count;
    slot.timing.strum_head_mask      = s->strum_head_mask;
    slot.timing.strum_dir_next       = s->strum_dir_next;
    slot.timing.strum_head_at_ms     = s->strum_head_at_ms;
    slot.timing.frets_active         = s->frets_active;
    slot.timing.strum_active         = s->strum_active;
    slot.timing.release_pending_mask = s->release_pending_mask;
    slot.timing.publish_mask         = s->publish_mask;
    slot.timing.strum_release_at_ms  = s->strum_release_at_ms;
    slot.timing.release_min_at_ms    = s->release_min_at_ms;
    send_state(&slot);
}

void PerfLog_EmitDetectorConfig(const perf_rec_detector_config_t *cfg)
{
    if (cfg == NULL) { return; }
    perf_rec_state_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    slot.detector_config = *cfg;
    hdr_fill(&slot.detector_config.hdr, PERF_REC_DETECTOR_CONFIG, 0u, 0u);
    send_state(&slot);
}

void PerfLog_EmitActuator(uint8_t  intended_mask,
                          uint8_t  asserted_mask,
                          uint8_t  strum_dir,
                          uint8_t  producer_id,
                          int32_t  last_ack_result,
                          uint64_t last_ack_ts_counter)
{
    perf_rec_state_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    hdr_fill(&slot.actuator.hdr, PERF_REC_ACTUATOR, 0u, 0u);
    slot.actuator.intended_mask       = intended_mask;
    slot.actuator.asserted_mask       = asserted_mask;
    slot.actuator.strum_dir           = strum_dir;
    slot.actuator.producer_id         = producer_id;
    slot.actuator.last_ack_result     = last_ack_result;
    slot.actuator.last_ack_ts_counter = last_ack_ts_counter;
    send_state(&slot);
}

void PerfLog_EmitFretboardRaw(const uint16_t adc[FRET_COUNT], uint32_t frame_epoch,
                              uint32_t fb_sample_seq, uint8_t applied_mask)
{
    if (adc == NULL) { return; }
    perf_rec_state_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    hdr_fill(&slot.fretboard_raw.hdr, PERF_REC_FRETBOARD_RAW, 0u, frame_epoch);
    memcpy(slot.fretboard_raw.adc, adc, sizeof(slot.fretboard_raw.adc));
    slot.fretboard_raw.fb_sample_seq = fb_sample_seq;
    slot.fretboard_raw.applied_mask  = applied_mask;
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

void PerfLog_EmitTaskRuntime(perf_task_id_t id,
                             perf_task_state_t state,
                             uint8_t priority,
                             uint32_t run_time_counter)
{
    perf_rec_state_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    hdr_fill(&slot.runtime.hdr, PERF_REC_TASK_RUNTIME, 0u, 0u);
    slot.runtime.task_id          = (uint8_t)id;
    slot.runtime.state            = (uint8_t)state;
    slot.runtime.priority         = priority;
    slot.runtime.run_time_counter = run_time_counter;
    send_state(&slot);
}

/* Claim a free strip pool slot and fill its header. Returns the slot pointer
 * (writing its index to *out_idx) or NULL when STRIP is masked off, the args
 * are invalid, or the pool is empty (drop counted). The caller fills r->bgr
 * then calls strip_slot_commit. No struct memcpy in the queue critical
 * section — only a 1-byte index transfer. */
static perf_rec_strip_t *strip_slot_claim(uint32_t frame_epoch, perf_strip_kind_t kind,
                                          uint16_t x, uint16_t y,
                                          uint16_t w, uint16_t h,
                                          uint8_t *out_idx)
{
    if (s_strip_q == NULL || s_strip_free_q == NULL) { return NULL; }
    if (!type_enabled(PERF_REC_STRIP)) { return NULL; }
    if (w == 0u || h == 0u) { return NULL; }
    if ((uint32_t)w * (uint32_t)h * PERF_STRIP_BPP > PERF_STRIP_MAX_BYTES) { return NULL; }

    uint8_t slot_idx;
    if (xQueueReceive(s_strip_free_q, &slot_idx, 0) != pdTRUE)
    {
        counter_add_task(&s_drop_strip, 1u);
        return NULL;
    }

    perf_rec_strip_t *r = &s_strip_pool[slot_idx];
    memset(r, 0, PERF_STRIP_HDR_BYTES);
    hdr_fill(&r->hdr, PERF_REC_STRIP, 0u, frame_epoch);
    r->x    = x;
    r->y    = y;
    r->w    = w;
    r->h    = h;
    r->kind = (uint8_t)kind;
    *out_idx = slot_idx;
    return r;
}

static void strip_slot_commit(uint8_t slot_idx)
{
    if (xQueueSend(s_strip_q, &slot_idx, 0) != pdTRUE)
    {
        /* In-flight queue full (pool sized depth+2 makes this rare; only
         * possible if the drain task is blocked while strip_q is at depth
         * and the producer also holds a slot). Return slot to free pool. */
        (void)xQueueSend(s_strip_free_q, &slot_idx, 0);
        counter_add_task(&s_drop_strip, 1u);
    }
}

/* Strip producer: claim a free slot, row-copy a w×h BGR888 region out of the
 * strided source frame directly into the slot, then enqueue the slot index.
 * Total pixel bytes (w*h*3) must fit the slot's bgr[] (PERF_STRIP_MAX_BYTES);
 * per-axis shape is unconstrained beyond that. Drop-on-pool-empty. */
void PerfLog_EmitStripFromFrame(uint32_t frame_epoch,
                                perf_strip_kind_t kind,
                                const uint8_t *frame, uint32_t frame_stride,
                                uint16_t x, uint16_t y,
                                uint16_t w, uint16_t h)
{
    if (frame == NULL) { return; }

    uint8_t slot_idx;
    perf_rec_strip_t *r = strip_slot_claim(frame_epoch, kind, x, y, w, h, &slot_idx);
    if (r == NULL) { return; }

    const uint32_t row_bytes = (uint32_t)w * PERF_STRIP_BPP;
    const uint8_t *src = frame + (uint32_t)y * frame_stride + (uint32_t)x * PERF_STRIP_BPP;
    uint8_t *dst = r->bgr;
    for (uint16_t row = 0u; row < h; row++)
    {
        memcpy(dst, src, row_bytes);
        src += frame_stride;
        dst += row_bytes;
    }

    strip_slot_commit(slot_idx);
}

void PerfLog_EmitStripPacked(uint32_t frame_epoch,
                             perf_strip_kind_t kind,
                             uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h,
                             const uint8_t *pixels)
{
    if (pixels == NULL) { return; }

    uint8_t slot_idx;
    perf_rec_strip_t *r = strip_slot_claim(frame_epoch, kind, x, y, w, h, &slot_idx);
    if (r == NULL) { return; }

    memcpy(r->bgr, pixels, (uint32_t)w * (uint32_t)h * PERF_STRIP_BPP);
    strip_slot_commit(slot_idx);
}

/* ─── Emit (ISR context) ─────────────────────────────────────────────────── */

void PerfLog_EmitStampFromISR(perf_stage_t stage,
                              uint32_t frame_epoch,
                              uint32_t aux,
                              BaseType_t *higher_priority_task_woken)
{
    if (s_state_q == NULL) { return; }
    if (!type_enabled(PERF_REC_STAMP)) { return; }
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

/* ─── Snapshot request ───────────────────────────────────────────────────── */

void PerfLog_RequestSnapshot(void)
{
    s_snapshot_req = true;
}

/* ─── Record-type filter ─────────────────────────────────────────────────── */

void PerfLog_SetEnabledMask(uint32_t mask)
{
    s_enabled_mask = mask;
    LOG_INFO("PerfLog: mask=0x%08lx\r\n", (unsigned long)mask);
}

uint32_t PerfLog_GetEnabledMask(void)
{
    return s_enabled_mask;
}

/* ─── Overlay sinks ──────────────────────────────────────────────────────── */

void PerfLog_SetOverlayFlags(uint32_t flags)
{
    s_overlay_flags = flags;
    LOG_INFO("PerfLog: overlay=0x%08lx\r\n", (unsigned long)flags);
}

uint32_t PerfLog_GetOverlayFlags(void)
{
    return s_overlay_flags;
}
