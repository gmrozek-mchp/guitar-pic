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
void PerfLog_EmitTiming(uint32_t frame_epoch,
                        uint8_t  publish_mask,
                        uint8_t  chord_window_fill,
                        uint8_t  fifo_depth,
                        uint8_t  strum_dir);
void PerfLog_EmitStripFromFrame(uint32_t frame_epoch,
                                perf_strip_kind_t kind,
                                const uint8_t *frame, uint32_t frame_stride,
                                uint16_t x, uint16_t y,
                                uint16_t w, uint16_t h);

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

#endif /* PERF_LOG_H */
