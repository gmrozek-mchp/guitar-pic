#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

/* Video pipeline: HDMI source → TC358743 bridge → CSI-2 → CSI2DC → ISC →
 * DDR → XLCDC HEO. The module owns its own FreeRTOS task; everything below
 * (tc358743, isc_capture, HEO bind/unbind, capture/display state machine)
 * runs inside that task except the frame-arrival notification which fires
 * from the ISC IRQ via xQueueSendFromISR.
 *
 * Capture and display are independent. CaptureEnable arms the bridge +
 * ISC chain when the source is locked; DisplayShow makes the HEO layer
 * visible at the configured window. You can run capture without display
 * (frames flow into DDR, BASE/UI fills the panel) or display without
 * capture (HEO shows whatever is in the framebuffer — possibly stale).
 *
 * Defaults at Initialize: capture disabled, display hidden, window
 * unset. Layout is app policy — DisplayShow is a no-op until the
 * caller has set a window with Video_SetWindow. */

void Video_Initialize(void);

/* Capture chain control. ENABLE: arm ISC + bridge stream when source
 * is locked, re-arm on every lost/regain cycle. DISABLE: stop ISC +
 * stream and stay off regardless of lock state. */
void Video_CaptureEnable(void);
void Video_CaptureDisable(void);

/* Display layer control. SHOW: HEO visible, BASE DISCEN cleared under
 * the HEO rect (saves DDR read bandwidth). HIDE: HEO disabled, BASE
 * owns full panel. Independent of capture state. */
void Video_DisplayShow(void);
void Video_DisplayHide(void);

/* Destination rect on the 1280×800 panel. dst_w/dst_h matching the
 * source frame size → 1:1, scaler bypassed. Otherwise HEO bilinear
 * scaler engages and stretches to fill the rect. The rect must fit
 * within the panel. Takes effect on the next display bind (next
 * DisplayShow call, or when capture/lock state changes). */
void Video_SetWindow(uint32_t x, uint32_t y, uint32_t dst_w, uint32_t dst_h);

/* Frame metadata delivered to subscribers on every captured frame. */
typedef struct
{
    void    *buffer;          /* pointer to most-recent complete frame in DDR */
    uint32_t frame_count;     /* monotonic since last ISC start */
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

#endif
