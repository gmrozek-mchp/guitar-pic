#ifndef VIDEO_H
#define VIDEO_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

/* Video pipeline: HDMI source → TC358743 bridge → CSI-2 → CSI2DC → ISC →
 * DDR. This module is the capture *producer*: it owns its own FreeRTOS task
 * (tc358743, isc_capture, capture state machine) and fans completed frames out
 * to subscribers from the ISC IRQ via xQueueSendFromISR.
 *
 * Display (the HEO hardware layer that scans a captured frame to the LCD) is
 * owned by the compositor (ui_manager), NOT here — capture and display are fully
 * independent (the detector/gameplay consume frames whether or not HEO is shown).
 * The module notifies the compositor through two registered callbacks so the
 * compositor can own all HEO/BASE register writes while keeping the timing this
 * task/IRQ already has:
 *   - a display-reconcile callback, called each task tick with the current source
 *     validity + size, so the compositor can (re)bind/unbind HEO in task context;
 *   - a frame-latch callback, called from the ISC IRQ with the freshest completed
 *     buffer address, so the compositor can latch HEO to the newest ring slot.
 *
 * Defaults at Initialize: capture disabled, no callbacks. */

void Video_Initialize(void);

/* Capture chain control. ENABLE: arm ISC + bridge stream when source
 * is locked, re-arm on every lost/regain cycle. DISABLE: stop ISC +
 * stream and stay off regardless of lock state. */
void Video_CaptureEnable(void);
void Video_CaptureDisable(void);

/* Register the compositor's HEO hooks (see the module note above). Both are
 * optional (NULL = no display). Set before the video task starts.
 *   frame-latch: fires in ISC IRQ context with the just-completed buffer address.
 *   reconcile:   fires each task tick with (source_valid, src_w, src_h). */
void Video_SetFrameLatchCallback(void (*cb)(uint32_t buffer_addr));
void Video_SetDisplayReconcileCallback(void (*cb)(bool source_valid,
                                                  uint16_t src_w, uint16_t src_h));

/* Frame metadata delivered to subscribers on every captured frame. */
typedef struct
{
    void    *buffer;          /* pointer to most-recent complete frame in DDR */
    uint32_t frame_count;     /* monotonic since last ISC start */
    uint64_t timestamp_us;    /* capture time (frame-done IRQ), marvin-local.
                               * Anchor timing to when the frame completed, not
                               * when a consumer got around to processing it. */
    uint16_t width;
    uint16_t height;
    uint16_t bytes_per_pixel;
} Video_FrameInfo;

/* Subscribe a queue to frame-ready events. The video module sends a
 * Video_FrameInfo from the ISC frame-done IRQ via xQueueSendFromISR
 * each time a frame completes to DDR — fanned out to every subscribed
 * queue. Create the queue with:
 *   QueueHandle_t q = xQueueCreate(N, sizeof(Video_FrameInfo));
 *
 * Per-subscriber depth chooses the back-pressure policy. Depth 1 means
 * "process most recent frame, drop the rest" — when a frame arrives and
 * the queue is full, the IRQ's xQueueSendFromISR returns errQUEUE_FULL
 * and the frame is dropped for that subscriber. Deeper queues preserve
 * frames at the cost of memory and added latency. Each subscriber chooses
 * independently.
 *
 * The Video_FrameInfo.buffer pointer reflects the buffer that *just*
 * completed (one of the descriptor-ring slots). With NUM_BUFFERS > 2 a
 * subscriber can hold the pointer for multiple frame-times before the
 * ring laps it; with NUM_BUFFERS = 2 the buffer is overwritten on the
 * second-next frame.
 *
 * Returns false if the subscriber table is full. Calling with a queue
 * already in the table is a no-op success. Up to VIDEO_MAX_SUBSCRIBERS
 * concurrent subscribers (currently 4). */
bool Video_SubscribeFrames(QueueHandle_t q);

/* Remove a queue from the subscriber list. Returns false if not found. */
bool Video_UnsubscribeFrames(QueueHandle_t q);

/* Synchronous read of the current frame state. Doesn't block. May
 * report frame_count = 0 / buffer = NULL if capture has never armed. */
void Video_GetFrameInfo(Video_FrameInfo *info);

/* Detected active-picture rectangle within the captured frame.
 *
 * A component→HDMI source (the Wii bridge) frames its active raster with dead
 * black bars whose thickness varies by console/converter, so the active area is
 * detected at runtime — the union of the bright-pixel bounds over the first
 * frames after capture arms, locked once it clears a minimum size. Detection is
 * one-shot per capture arm (reset on re-arm / source-size change).
 *
 * Returns true with the locked rect (x,y = top-left offset, w,h = size, all in
 * source pixels) once detected; false until then, filling *x=*y=0 and *w/*h =
 * the full frame — a safe full-frame fallback so callers always get a usable
 * rect. Any NULL out-param is skipped. */
bool Video_GetActiveRect(uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *h);

#endif
