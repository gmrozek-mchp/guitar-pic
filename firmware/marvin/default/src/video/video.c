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

/* Stall-watchdog window: armed + source locked but no new frame for this long
 * means the CSI-2 D-PHY RX fell out of HS lock (see capture_watchdog). */
#define VIDEO_STALL_TIMEOUT_MS  500u

/* Capture pixels are RGB_888_PACKED, 3 bytes per pixel. Hard-coded to match
 * isc_capture's ISC_CAP_BPP. If that ever becomes runtime-configurable, push
 * it into a getter. */
#define VIDEO_BYTES_PER_PIXEL   3u

/* Active-area detection (see Video_GetActiveRect). Luma = max(B,G,R) so it is
 * agnostic to the BGR byte order. The dead border sits at a low pedestal (~13)
 * while active content spikes past ~145; ACTIVE_LUMA_THR splits them with wide
 * margin and no dependence on the exact pedestal. Edges are the union of the
 * per-frame bright-pixel bounds over ACTIVE_ACCUM_FRAMES *bright* frames (dark
 * frames find no content and are skipped, so a loading screen at capture start
 * just defers the lock), then accepted only if ≥ the minimum plausible size. */
#define VIDEO_ACTIVE_LUMA_THR     40u
#define VIDEO_ACTIVE_MIN_W       700u
#define VIDEO_ACTIVE_MIN_H       420u
#define VIDEO_ACTIVE_SAMPLE_STEP   4u   /* subsample stride when profiling a line */
#define VIDEO_ACTIVE_ACCUM_FRAMES 16u   /* bright frames to union before locking */
#define VIDEO_ACTIVE_SCAN_MS     100u   /* re-scan for the active rect at most ~10 Hz while unlatched (not every 20 ms tick) */
#define VIDEO_ACTIVE_DARK_STEP    64u   /* coarse grid stride for the cheap "any light?" pre-check */

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

/* Detected active-picture rect (see Video_GetActiveRect). s_active_* are the
 * locked result (task writes on lock, any task reads); s_acc_* accumulate the
 * edge union pre-lock (video-task only). */
static volatile uint16_t s_active_x, s_active_y, s_active_w, s_active_h;
static volatile bool     s_active_valid;
static uint16_t          s_acc_top, s_acc_bot, s_acc_left, s_acc_right;
static uint8_t           s_acc_count;

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
        .timestamp_us    = (uint64_t)xTaskGetTickCountFromISR()
                         * (1000000u / configTICK_RATE_HZ),
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

/* ─── Active-area detection (video-task ctx) ───────────────────────────── */

static uint8_t luma3(const uint8_t *p)
{
    uint8_t m = p[0];
    if (p[1] > m) { m = p[1]; }
    if (p[2] > m) { m = p[2]; }
    return m;
}

/* Find the bright-content bounds of one frame by scanning inward from each edge
 * until a sampled pixel clears the threshold. Each scan is bounded to the deepest
 * border the min-size gate allows (a border thicker than that can't yield an
 * accepted rect anyway) — so the common case stops after a few lines and the
 * worst case (a fully dark frame) is still cheap and bounded. Returns false when
 * any edge isn't found within its bound (blank/dark/too-bordered frame → skipped,
 * not counted). */
static bool frame_edges(const uint8_t *buf, uint16_t w, uint16_t h,
                        uint16_t *top, uint16_t *bot, uint16_t *left, uint16_t *right)
{
    const uint32_t stride = (uint32_t)w * VIDEO_BYTES_PER_PIXEL;
    const uint16_t step   = VIDEO_ACTIVE_SAMPLE_STEP;
    const uint16_t ybord  = (h > VIDEO_ACTIVE_MIN_H) ? (uint16_t)(h - VIDEO_ACTIVE_MIN_H) : 0u;
    const uint16_t xbord  = (w > VIDEO_ACTIVE_MIN_W) ? (uint16_t)(w - VIDEO_ACTIVE_MIN_W) : 0u;
    bool ft = false, fb = false, fl = false, fr = false;

    for (uint16_t y = 0u; y <= ybord && !ft; y++)
    {
        const uint8_t *row = buf + (uint32_t)y * stride;
        for (uint16_t x = 0u; x < w; x += step)
            if (luma3(row + (uint32_t)x * VIDEO_BYTES_PER_PIXEL) > VIDEO_ACTIVE_LUMA_THR)
                { *top = y; ft = true; break; }
    }
    if (!ft) { return false; }

    for (uint16_t y = 0u; y <= ybord && !fb; y++)
    {
        uint16_t yy = (uint16_t)(h - 1u - y);
        const uint8_t *row = buf + (uint32_t)yy * stride;
        for (uint16_t x = 0u; x < w; x += step)
            if (luma3(row + (uint32_t)x * VIDEO_BYTES_PER_PIXEL) > VIDEO_ACTIVE_LUMA_THR)
                { *bot = yy; fb = true; break; }
    }
    if (!fb) { return false; }

    for (uint16_t x = 0u; x <= xbord && !fl; x++)
        for (uint16_t y = 0u; y < h; y += step)
            if (luma3(buf + (uint32_t)y * stride + (uint32_t)x * VIDEO_BYTES_PER_PIXEL) > VIDEO_ACTIVE_LUMA_THR)
                { *left = x; fl = true; break; }
    if (!fl) { return false; }

    for (uint16_t x = 0u; x <= xbord && !fr; x++)
    {
        uint16_t xx = (uint16_t)(w - 1u - x);
        for (uint16_t y = 0u; y < h; y += step)
            if (luma3(buf + (uint32_t)y * stride + (uint32_t)xx * VIDEO_BYTES_PER_PIXEL) > VIDEO_ACTIVE_LUMA_THR)
                { *right = xx; fr = true; break; }
    }

    return ft && fb && fl && fr;
}

static void active_reset(void)
{
    s_active_valid = false;
    s_acc_count    = 0u;
}

/* One detection step per tick until locked: union this frame's bright bounds
 * into the accumulator; after enough bright frames, lock the union if it clears
 * the minimum plausible size, else reset and keep trying (fallback stays
 * full-frame meanwhile). Reads the latest complete ring slot (non-cached DDR,
 * so coherent; a 4-slot ring means it isn't overwritten mid-scan). */
/* Cheap whole-frame "is anything lit?" gate: coarse grid, returns on the first
 * bright sample. Lets detect_active skip the (much heavier) border edge scan
 * while the source is dark/asleep — detection resumes the moment it brightens. */
static bool frame_has_light(const uint8_t *buf, uint16_t w, uint16_t h)
{
    const uint32_t stride = (uint32_t)w * VIDEO_BYTES_PER_PIXEL;
    for (uint16_t y = 0u; y < h; y += VIDEO_ACTIVE_DARK_STEP)
    {
        const uint8_t *row = buf + (uint32_t)y * stride;
        for (uint16_t x = 0u; x < w; x += VIDEO_ACTIVE_DARK_STEP)
            if (luma3(row + (uint32_t)x * VIDEO_BYTES_PER_PIXEL) > VIDEO_ACTIVE_LUMA_THR)
                { return true; }
    }
    return false;
}

static void detect_active(void)
{
    if (s_active_valid) { return; }

    /* Don't re-scan every 20 ms video tick — cap the search to ~VIDEO_ACTIVE_SCAN_MS
     * while unlatched. This never gives up: it keeps trying (cheaply) so it still
     * frames the picture whenever the source brightens (e.g. the Wii waking). */
    static TickType_t last_scan;
    TickType_t now = xTaskGetTickCount();
    if ((TickType_t)(now - last_scan) < pdMS_TO_TICKS(VIDEO_ACTIVE_SCAN_MS)) { return; }
    last_scan = now;

    const uint8_t *buf = (const uint8_t *)(uintptr_t)s_latest_buffer;
    uint16_t w = s_src_w, h = s_src_h;
    if (buf == NULL || w == 0u || h == 0u) { return; }

    /* Source dark/asleep → nothing to detect; skip the heavy border scan and wait. */
    if (!frame_has_light(buf, w, h)) { return; }

    uint16_t t, b, l, r;
    if (!frame_edges(buf, w, h, &t, &b, &l, &r)) { return; }   /* border dark — skip */

    if (s_acc_count == 0u)
    {
        s_acc_top = t; s_acc_bot = b; s_acc_left = l; s_acc_right = r;
    }
    else
    {
        if (t < s_acc_top)   { s_acc_top   = t; }
        if (b > s_acc_bot)   { s_acc_bot   = b; }
        if (l < s_acc_left)  { s_acc_left  = l; }
        if (r > s_acc_right) { s_acc_right = r; }
    }
    if (++s_acc_count < VIDEO_ACTIVE_ACCUM_FRAMES) { return; }

    uint16_t aw = s_acc_right - s_acc_left + 1u;
    uint16_t ah = s_acc_bot   - s_acc_top  + 1u;
    if (aw >= VIDEO_ACTIVE_MIN_W && ah >= VIDEO_ACTIVE_MIN_H)
    {
        s_active_x = s_acc_left; s_active_y = s_acc_top;
        s_active_w = aw;         s_active_h = ah;
        s_active_valid = true;
        LOG_INFO("VIDEO: active area %ux%u @(%u,%u) in %ux%u frame\r\n",
                 (unsigned)aw, (unsigned)ah, (unsigned)s_acc_left, (unsigned)s_acc_top,
                 (unsigned)w, (unsigned)h);
    }
    else
    {
        static uint8_t warn_budget = 4u;
        if (warn_budget > 0u)
        {
            LOG_WARN("VIDEO: active-area detect %ux%u below min %ux%u; retrying\r\n",
                     (unsigned)aw, (unsigned)ah,
                     (unsigned)VIDEO_ACTIVE_MIN_W, (unsigned)VIDEO_ACTIVE_MIN_H);
            warn_budget--;
        }
        s_acc_count = 0u;   /* discard and re-accumulate */
    }
}

/* ─── Capture state machine ────────────────────────────────────────────── */

static bool capture_arm(void)
{
    uint16_t w = 0, h = 0;
    if (!TC358743_GetDetectedFormat(&w, &h)) { return false; }

    if (!ISC_Capture_Configure(w, h)) { return false; }
    s_src_w = w;
    s_src_h = h;
    active_reset();   /* fresh source — re-detect the active area */
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

    /* Detect the active picture rect once frames are flowing, before handing the
     * source to the compositor so it can pick up the crop the same tick. */
    if (s_capture_armed) { detect_active(); }

    if (s_reconcile_cb != NULL)
    {
        s_reconcile_cb((s_src_w != 0u) && (s_src_h != 0u), s_src_w, s_src_h);
    }
}

/* The CSI-2 D-PHY RX can be knocked out of HS lock by a heavy GFX burst (e.g. a
 * UI transition) while the TC358743 still reports the source locked, so the ISC
 * frame counter stops advancing but reconcile() never re-arms. Detect that stall
 * — armed + source locked but no new frame for VIDEO_STALL_TIMEOUT_MS — and cycle
 * the capture chain, which re-inits the D-PHY and re-locks cleanly once the bus
 * is quiet. The frame-count reference is reset after each re-arm (ISC_Capture_-
 * Configure zeroes the counter), so a stuck source retries at most once per
 * window rather than every tick. */
static void capture_watchdog(void)
{
    static uint32_t  last_count;
    static TickType_t last_advance_tick;

    /* Not actively capturing: hold the reference fresh so a subsequent arm gets
     * a full timeout window before it can look stalled. */
    if (!s_capture_armed || !s_capture_enabled || !TC358743_IsLocked())
    {
        last_count        = ISC_Capture_FrameCount();
        last_advance_tick = xTaskGetTickCount();
        return;
    }

    uint32_t  count = ISC_Capture_FrameCount();
    TickType_t now  = xTaskGetTickCount();

    if (count != last_count)
    {
        last_count        = count;
        last_advance_tick = now;
        return;
    }

    if ((now - last_advance_tick) >= pdMS_TO_TICKS(VIDEO_STALL_TIMEOUT_MS))
    {
        LOG_WARN("VIDEO: capture stalled at frame %lu (source locked) — re-arming\r\n",
                 (unsigned long)count);
        /* Dump the pipeline register state for the first few collapses so the
         * error class is visible without flooding the UART on every re-arm. */
        static uint8_t diag_budget = 8u;
        if (diag_budget > 0u) { ISC_Capture_DumpDiag("stall"); diag_budget--; }
        capture_disarm();
        s_capture_armed = false;
        if (capture_arm()) { s_capture_armed = true; }

        last_count        = ISC_Capture_FrameCount();
        last_advance_tick = xTaskGetTickCount();
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
        capture_watchdog();
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
    info->timestamp_us    = 0u;   /* synchronous state read, not a capture event */
    info->width           = s_src_w;
    info->height          = s_src_h;
    info->bytes_per_pixel = VIDEO_BYTES_PER_PIXEL;
}

bool Video_GetActiveRect(uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *h)
{
    bool valid = s_active_valid;
    if (valid)
    {
        if (x != NULL) { *x = s_active_x; }
        if (y != NULL) { *y = s_active_y; }
        if (w != NULL) { *w = s_active_w; }
        if (h != NULL) { *h = s_active_h; }
    }
    else
    {
        if (x != NULL) { *x = 0u; }
        if (y != NULL) { *y = 0u; }
        if (w != NULL) { *w = s_src_w; }
        if (h != NULL) { *h = s_src_h; }
    }
    return valid;
}
