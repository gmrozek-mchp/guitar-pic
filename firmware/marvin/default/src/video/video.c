#include "video.h"

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "definitions.h"
#include "log.h"
#include "tc358743.h"
#include "isc_capture.h"
#include "perf_log/perf_log.h"

#define VIDEO_TASK_STACK_WORDS  1024u
#define VIDEO_TASK_PRIORITY     4u
#define VIDEO_POLL_MS           20u

/* Capture pixels are RGB_888_PACKED, 3 bytes per pixel. Hard-coded to match
 * isc_capture's ISC_CAP_BPP. If that ever becomes runtime-configurable, push
 * it into a getter. */
#define VIDEO_BYTES_PER_PIXEL   3u

/* Public intent — set via the API, read by the task body. Single-writer
 * (each setter is called by one task), single-reader (video task), so
 * plain volatile is sufficient on a 32-bit MCU. */
static volatile bool s_capture_enabled;

/* Compositor display hooks (ui_manager owns the HEO layer). s_frame_latch_cb
 * fires in ISC IRQ context; s_reconcile_cb fires each video-task tick. Set once
 * before the task starts, then read-only, so no barrier needed. */
static void (*s_frame_latch_cb)(uint32_t buffer_addr);
static void (*s_reconcile_cb)(bool source_valid, uint16_t src_w, uint16_t src_h);

/* Frame subscriber array. Sized for note detector + recording + a couple
 * of independent vision tasks (menu reader, score reader, etc.). Slots are
 * single-aligned-pointer reads/writes so the IRQ can scan without taking
 * a lock; concurrent task-side writers are serialized by taskENTER_CRITICAL
 * during slot assignment. Empty slots are NULL. */
#define VIDEO_MAX_SUBSCRIBERS  4u
static QueueHandle_t s_subscribers[VIDEO_MAX_SUBSCRIBERS];

/* Most-recent capture buffer + dimensions. Updated by the ISC IRQ on every
 * completed frame; read by Video_GetFrameInfo. Aligned 32-bit so naked
 * reads/writes are atomic on Cortex-A5. */
static volatile uint32_t s_latest_buffer;
static volatile uint16_t s_src_w;
static volatile uint16_t s_src_h;

/* Capture-state flag. Task-only. */
static bool s_capture_armed;

/* ─── Frame-done ISR (runs in IRQ context) ─────────────────────────────── */

static void on_frame_done(uint32_t frame_count,
                          uint32_t buffer_addr,
                          uintptr_t ctx)
{
    (void)ctx;

    BaseType_t higher_priority_task_woken = pdFALSE;
    PerfLog_EmitStampFromISR(PERF_STAGE_ISC_IRQ, frame_count, 0u,
                             &higher_priority_task_woken);

    s_latest_buffer = buffer_addr;

    /* Hand the just-completed buffer to the compositor's display latch (if any)
     * so it can re-point HEO at the freshest ring slot — otherwise HEO would only
     * ever show frames written to slot 0. The compositor decides whether HEO is
     * actually bound; here we just notify. Runs in IRQ context (a single register
     * write that latches at next vsync). */
    if (s_frame_latch_cb != NULL) { s_frame_latch_cb(buffer_addr); }

    Video_FrameInfo info =
    {
        .buffer          = (void *)(uintptr_t)buffer_addr,
        .frame_count     = frame_count,
        .width           = s_src_w,
        .height          = s_src_h,
        .bytes_per_pixel = VIDEO_BYTES_PER_PIXEL,
    };

    uint32_t subscriber_mask = 0u;
    for (uint8_t i = 0u; i < VIDEO_MAX_SUBSCRIBERS; i++)
    {
        QueueHandle_t q = s_subscribers[i];
        if (q != NULL)
        {
            (void)xQueueSendFromISR(q, &info, &higher_priority_task_woken);
            subscriber_mask |= (1u << i);
        }
    }
    PerfLog_EmitStampFromISR(PERF_STAGE_VIDEO_PUBLISH, frame_count,
                             subscriber_mask, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/* ─── Capture state machine ────────────────────────────────────────────── */

static bool capture_arm(void)
{
    uint16_t w = 0, h = 0;
    if (!TC358743_GetDetectedFormat(&w, &h)) { return false; }

    if (!ISC_Capture_Configure(w, h)) { return false; }
    s_src_w = w;
    s_src_h = h;
    (void)TC358743_EnableStream(true);
    if (!ISC_Capture_Start())
    {
        (void)TC358743_EnableStream(false);
        return false;
    }
    LOG_INFO("VIDEO: capture ARMED %ux%u\r\n", w, h);
    return true;
}

static void capture_disarm(void)
{
    ISC_Capture_Stop();
    (void)TC358743_EnableStream(false);
    LOG_INFO("VIDEO: capture IDLE\r\n");
}

/* Each tick: align capture-chain state to intent + source lock, then hand the
 * current source validity/size to the compositor's display-reconcile hook (if
 * any) so it can (re)bind/unbind HEO in this task's context. The source size is
 * captured at arm time and persists across capture teardown, so the compositor
 * can keep HEO bound on a stale frame if it wants. */
static void reconcile(void)
{
    bool locked = TC358743_IsLocked();

    /* Capture: armed iff intent + lock. */
    bool want_capture = s_capture_enabled && locked;
    if (want_capture && !s_capture_armed)
    {
        if (capture_arm()) { s_capture_armed = true; }
    }
    else if (!want_capture && s_capture_armed)
    {
        capture_disarm();
        s_capture_armed = false;
    }

    if (s_reconcile_cb != NULL)
    {
        s_reconcile_cb((s_src_w != 0u) && (s_src_h != 0u), s_src_w, s_src_h);
    }
}

/* ─── Task body ────────────────────────────────────────────────────────── */

static void video_task(void *param)
{
    (void)param;

    /* One-time init in task context (scheduler running, so synchronous I²C
     * and SYS_TIME work). Brings up capture pipeline, TC358743 bridge, and
     * parks the panel in UI-only mode. The backlight is NOT enabled here — the
     * UI boot task lights it once the splash is on screen, so the panel never
     * shows a pre-splash/garbage frame. */
    ISC_Capture_Initialize();
    ISC_Capture_SetFrameCallback(on_frame_done, 0);
    TC358743_Initialize();

    for (;;)
    {
        TC358743_Tasks();
        reconcile();
        vTaskDelay(pdMS_TO_TICKS(VIDEO_POLL_MS));
    }
}

/* ─── Public API ───────────────────────────────────────────────────────── */

static StackType_t  s_task_stack[VIDEO_TASK_STACK_WORDS];
static StaticTask_t s_task_tcb;

void Video_Initialize(void)
{
    TaskHandle_t h = xTaskCreateStatic(video_task,
                                       "VideoTask",
                                       VIDEO_TASK_STACK_WORDS,
                                       NULL,
                                       VIDEO_TASK_PRIORITY,
                                       s_task_stack,
                                       &s_task_tcb);
    PerfLog_RegisterTaskForHighwater(PERF_TASK_VIDEO, h);
}

void Video_CaptureEnable(void)  { s_capture_enabled = true;  }
void Video_CaptureDisable(void) { s_capture_enabled = false; }

void Video_SetFrameLatchCallback(void (*cb)(uint32_t buffer_addr))
{
    s_frame_latch_cb = cb;
}

void Video_SetDisplayReconcileCallback(void (*cb)(bool source_valid,
                                                  uint16_t src_w, uint16_t src_h))
{
    s_reconcile_cb = cb;
}

bool Video_SubscribeFrames(QueueHandle_t q)
{
    if (q == NULL) { return false; }

    bool ok = false;
    taskENTER_CRITICAL();
    for (uint8_t i = 0u; i < VIDEO_MAX_SUBSCRIBERS; i++)
    {
        if (s_subscribers[i] == q) { ok = true; break; }     /* already in */
        if (s_subscribers[i] == NULL)
        {
            s_subscribers[i] = q;
            ok = true;
            break;
        }
    }
    taskEXIT_CRITICAL();

    if (!ok) { LOG_ERROR("VIDEO: subscriber table full (max %u)\r\n",
                         (unsigned)VIDEO_MAX_SUBSCRIBERS); }
    return ok;
}

bool Video_UnsubscribeFrames(QueueHandle_t q)
{
    if (q == NULL) { return false; }

    bool ok = false;
    taskENTER_CRITICAL();
    for (uint8_t i = 0u; i < VIDEO_MAX_SUBSCRIBERS; i++)
    {
        if (s_subscribers[i] == q)
        {
            s_subscribers[i] = NULL;
            ok = true;
            break;
        }
    }
    taskEXIT_CRITICAL();
    return ok;
}

void Video_GetFrameInfo(Video_FrameInfo *info)
{
    if (info == NULL) { return; }
    info->buffer          = (void *)(uintptr_t)s_latest_buffer;
    info->frame_count     = ISC_Capture_FrameCount();
    info->width           = s_src_w;
    info->height          = s_src_h;
    info->bytes_per_pixel = VIDEO_BYTES_PER_PIXEL;
}
