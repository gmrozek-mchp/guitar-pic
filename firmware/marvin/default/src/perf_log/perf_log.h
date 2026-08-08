#ifndef PERF_LOG_H
#define PERF_LOG_H

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"

#include "perf_log_records.h"

/* Per-frame performance log. Producers post records onto two static
 * queues (small / patch); a low-priority drain task frames them
 * (SOF + length + payload + CRC-16/CCITT) and pushes the bytes out the
 * USB-device CDC sink. Drop-on-full, never block. The pipeline must
 * remain unaffected if perf-log vanishes.
 *
 * All emit calls take ≈O(memcpy) and never lock. The ISR variants use
 * xQueueSendFromISR; the task variants use xQueueSend with zero block
 * time. Choose by call site — passing the wrong one will not corrupt
 * state but may misuse the higher_priority_task_woken pathway. */

void PerfLog_Initialize(void);     /* call after Video_Initialize */
void PerfLog_Start(void);          /* call after vTaskStartScheduler */

bool PerfLog_IsRunning(void);

/* ─── Task-context emit ──────────────────────────────────────────────────── */

void PerfLog_EmitStamp(perf_stage_t stage, uint32_t frame_epoch, uint32_t aux);
void PerfLog_EmitDetector(uint32_t frame_epoch,
                          const uint16_t hold_dist[FRET_COUNT],
                          const uint16_t edge_dist[FRET_COUNT],
                          uint8_t  pressed_mask,
                          uint8_t  edge_active_mask);

/* Snapshot the producer (timing_pipeline) fills in once per frame, then
 * hands to PerfLog_EmitTiming. Field-for-field mirror of perf_rec_timing_t
 * minus the header — kept as a separate struct so producers don't have to
 * know about wire framing. */
typedef struct
{
    uint32_t now_ms;
    uint8_t  chord_open;
    uint8_t  chord_mask;
    uint16_t chord_age_ms;
    uint8_t  note_q_count;
    uint8_t  note_head_mask;
    uint8_t  note_tail_mask;
    uint32_t note_head_at_ms;
    uint8_t  strum_q_count;
    uint8_t  strum_head_mask;
    uint8_t  strum_dir_next;
    uint32_t strum_head_at_ms;
    uint8_t  frets_active;
    uint8_t  strum_active;
    uint8_t  release_pending_mask;
    uint8_t  publish_mask;
    uint32_t strum_release_at_ms;
    uint32_t release_min_at_ms;
} perf_timing_snapshot_t;

void PerfLog_EmitTiming(uint32_t frame_epoch, const perf_timing_snapshot_t *snap);

/* DETECTOR_CONFIG: producer fills in the perf_rec_detector_config_t struct
 * (sample coords, thresholds, color filter weights) and hands it over;
 * perf_log fills the header and queues. Emit on connect-up edge and on
 * change so the host always has a current copy for STRIP overlays. */
void PerfLog_EmitDetectorConfig(const perf_rec_detector_config_t *cfg);

/* ACTUATOR: emitted by fretboard_link on every Send call. producer_id
 * uses perf_actuator_producer_t; last_ack_* are the most-recent
 * CDC_WRITE_COMPLETE result and SYS_TIME counter snapshotted at Send. */
void PerfLog_EmitActuator(uint8_t  intended_mask,
                          uint8_t  asserted_mask,
                          uint8_t  strum_dir,
                          uint8_t  producer_id,
                          int32_t  last_ack_result,
                          uint64_t last_ack_ts_counter);

/* FRETBOARD_RAW: one record per parsed 18-byte fretboard data frame.
 * Producer (fretboard_link RX) supplies the 5×u16 ADC values, the fretboard's
 * own sample-sequence counter, the applied actuator bitmask, and marvin's CV
 * teacher command latched in that frame (commanded_mask — the atomic edge-ai
 * label; all carried on the wire), plus the most recent video frame_epoch from
 * Video_GetFrameInfo; perf_log fills the header. Default-disabled at boot. */
void PerfLog_EmitFretboardRaw(const uint16_t adc[FRET_COUNT],
                              uint32_t frame_epoch,
                              uint32_t fb_sample_seq,
                              uint8_t  applied_mask,
                              uint8_t  commanded_mask);

void PerfLog_EmitStripFromFrame(uint32_t frame_epoch,
                                perf_strip_kind_t kind,
                                const uint8_t *frame, uint32_t frame_stride,
                                uint16_t x, uint16_t y,
                                uint16_t w, uint16_t h);

/* Emit a strip whose pixels are already tightly packed (w*h*PERF_STRIP_BPP,
 * row-major, no inter-row stride). Lets a producer composite a strip — e.g.
 * paint the calibration rings onto the copy — before handing it over, without
 * touching the source capture frame. (x, y) are the strip's origin in the
 * source frame, for host-side placement. Same drop-on-pool-empty + STRIP mask
 * gating as PerfLog_EmitStripFromFrame. */
void PerfLog_EmitStripPacked(uint32_t frame_epoch,
                             perf_strip_kind_t kind,
                             uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h,
                             const uint8_t *pixels);

/* Request a one-shot full-frame snapshot. Sets a flag the drain task picks
 * up; the drain task copies the current video frame to a staging buffer and
 * streams it back as a top-to-bottom run of full-width SNAPSHOT strips (last
 * band flagged LAST). Ignored if a snapshot is already in flight. Safe to
 * call from any context (e.g. the CDC RX callback). */
void PerfLog_RequestSnapshot(void);

/* Request a one-shot dump of a Legato canvas surface (the UI framebuffer, as
 * opposed to the video frame PerfLog_RequestSnapshot captures). `canvas` is a
 * ui_manager CANVAS_* id; the rect is in surface pixels, clipped to the surface,
 * with w or h = 0 meaning "to the edge" — so an all-zero rect dumps the lot.
 * Streamed back as CANVAS strips, last band flagged LAST. Each canvas is its own
 * surface and no compositing is done, so overlays (drawer, dialogs, keyboard) and
 * the video layer will not appear in another canvas's dump. Safe from any
 * context; the drain task does the work. */
void PerfLog_RequestCanvasDump(uint8_t canvas, uint16_t x, uint16_t y,
                               uint16_t w, uint16_t h);

/* Outcome of the last canvas dump (PerfLog_Diag.canvas_reason). */
typedef enum
{
    PERF_CANVAS_IDLE       = 0,   /* none requested since boot            */
    PERF_CANVAS_REQUESTED  = 1,   /* latched by RX, drain hasn't run it   */
    PERF_CANVAS_RUNNING    = 2,   /* started emitting bands               */
    PERF_CANVAS_DONE       = 3,   /* all bands handed to the sink         */
    PERF_CANVAS_NO_SURFACE = 4,   /* canvas id has no assigned buffer     */
    PERF_CANVAS_BAD_MODE   = 5,   /* colour mode not convertible          */
    PERF_CANVAS_OUTSIDE    = 6,   /* origin outside the surface           */
    PERF_CANVAS_EMPTY      = 7,   /* rect empty after clipping            */
    PERF_CANVAS_TOO_WIDE   = 8,   /* one row exceeds a strip payload      */
} perf_canvas_reason_t;

/* Perf-log internals for the `perf` console command. The console is on its own
 * UART, so this still reports when the CDC record stream is dead — which is the
 * case worth diagnosing. `drain_stack_free_words` is the FreeRTOS high-water
 * mark: if it approaches zero the drain task is overflowing its stack, which
 * would explain the whole subsystem going quiet. */
typedef struct
{
    bool     running;
    uint32_t drain_stack_free_words;
    uint32_t drop_state;
    uint32_t drop_strip;
    uint32_t drop_sink;
    bool     sink_connected;
    uint32_t sink_credits;    /* 0 = sink stalled; nothing can reach the host */
    uint32_t sink_reclaims;   /* >0 = USB writes were abandoned and recovered */
    uint8_t  canvas_reason;   /* perf_canvas_reason_t */
    uint8_t  canvas_id;
    uint16_t canvas_bands;
    uint32_t canvas_epoch;
} perf_log_diag_t;

void PerfLog_GetDiag(perf_log_diag_t *out);

void PerfLog_EmitTaskHighwater(perf_task_id_t id, uint32_t words);
void PerfLog_EmitTaskRuntime(perf_task_id_t id,
                             perf_task_state_t state,
                             uint8_t priority,
                             uint32_t run_time_counter);

/* Each marvin task module hands its TaskHandle_t to perf_log after
 * xTaskCreateStatic. The 1 Hz drain task samples uxTaskGetStackHighWaterMark
 * for every registered handle and emits a PERF_REC_TASK_HIGHWATER record.
 * Unregistered slots are skipped. */
void PerfLog_RegisterTaskForHighwater(perf_task_id_t id, TaskHandle_t handle);

/* ─── ISR-context emit ───────────────────────────────────────────────────── */

void PerfLog_EmitStampFromISR(perf_stage_t stage,
                              uint32_t frame_epoch,
                              uint32_t aux,
                              BaseType_t *higher_priority_task_woken);

/* Called by the sink when bytes have to be discarded (USB stall, no
 * device attached). Argument is a count of bytes dropped, accumulated
 * into the next PERF_REC_DROP. */
void PerfLog_NoteSinkDrop(uint32_t bytes_dropped);

/* ─── Record-type filter (host-controlled) ───────────────────────────────────
 *
 * Bit n (n = perf_rec_type_t value) gates emission of record type n. Set
 * via the host→device PERF_CMD_SET_TYPE_MASK command; default is all-on.
 * SESSION is always emitted regardless of mask. Read lock-free at emit
 * time (32-bit aligned single-load is atomic on Cortex-A). */
void     PerfLog_SetEnabledMask(uint32_t mask);
uint32_t PerfLog_GetEnabledMask(void);

/* ─── Overlay sinks (host-controlled) ─────────────────────────────────────────
 *
 * PERF_OVERLAY_* bits select where the per-fret target rings are drawn. Set
 * via PERF_CMD_SET_OVERLAY; the CV producer reads the flags lock-free at strip-
 * emit time. Default at boot is PERF_OVERLAY_STRIP (rings on the viewer strips,
 * capture buffer untouched). */
void     PerfLog_SetOverlayFlags(uint32_t flags);
uint32_t PerfLog_GetOverlayFlags(void);

/* ─── Region streams (host-controlled) ────────────────────────────────────────
 *
 * Stream fixed sub-regions of each video frame back as one PERF_REC_STRIP per
 * region per frame. PERF_REGION_SLOTS slots each hold their own enable + rect,
 * and the slot fixes the strip kind emitted (perf_cmd_region_stream_t documents
 * the map). Started/stopped per slot via PERF_CMD_REGION_STREAM; the CV producer
 * calls PerfLog_EmitRegionIfEnabled once per frame, which reads the slot table
 * lock-free and emits (drop-on-pool-empty; the command is the gate, not the
 * STRIP type mask) for every slot enabled with a rect inside the frame. All
 * slots off at boot; an out-of-range slot is ignored. */
void PerfLog_SetRegionStream(uint8_t slot, bool enable, uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h);
void PerfLog_EmitRegionIfEnabled(uint32_t frame_epoch, const uint8_t *frame,
                                 uint32_t frame_stride,
                                 uint16_t frame_w, uint16_t frame_h);

#endif /* PERF_LOG_H */
