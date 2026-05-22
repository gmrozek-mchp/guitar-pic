#include "cv_marvin_v1.h"
#include "detector.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "log.h"
#include "video/video.h"

#define CV_TASK_STACK_WORDS    1024u
#define CV_TASK_PRIORITY       4u
#define CV_FRAME_QUEUE_DEPTH   1u    /* drop-intermediate policy */

/* Capture frames are BGR888 packed, 3 bytes per pixel. Frames whose
 * bytes_per_pixel disagrees are skipped — defensive guard against
 * future ISC reformats. */
#define CV_BYTES_PER_PIXEL     3u
#define CV_PATCH_RADIUS        2u    /* 5×5 patch */

/* Hold sensor (brightness): rising edge above CV_HOLD_THRESH; releases
 * when it falls below CV_HOLD_THRESH × CV_HOLD_RELEASE_FRAC. Per-color
 * edge thresholds are uniform at 50 by default — separable later if any
 * fret needs its own. Mirrors fret-tuner detect_video.py defaults. */
#define CV_HOLD_THRESH         100.0f
#define CV_HOLD_RELEASE_FRAC   0.78f
#define CV_EDGE_THRESH         25.0f

#define CV_US_PER_TICK         (1000000u / configTICK_RATE_HZ)

/* Sensor coordinates in native capture-frame space, anchored to 720×480
 * (Wii 480p60 — primary production source). (hx,hy) = brightness sensor,
 * (ex,ey) = color-filtered edge sensor; both above the strike line.
 * Derived from fret-tuner detect_video.py 1920×1080 defaults via the
 * same Elgato HDMI capture, scaled by (3/8, 4/9). Runtime calibration
 * UI lands with M6. */
typedef struct { uint16_t hx, hy, ex, ey; } sensor_xy_t;

static const sensor_xy_t s_sensor_coords[FRET_COUNT] =
{
    [FRET_GREEN]  = { 280, 311, 293, 311 },
    [FRET_RED]    = { 317, 311, 330, 311 },
    [FRET_YELLOW] = { 355, 311, 368, 311 },
    [FRET_BLUE]   = { 393, 311, 380, 311 },
    [FRET_ORANGE] = { 430, 311, 417, 311 },
};

/* Per-fret BGR target / reject weights. Edge signal is
 * (target·c − max(0, reject·c)) × sat_ratio. Reject totals exceed 1.0
 * so camera-captured whites with color-temperature bias get suppressed,
 * not just mathematically perfect whites. */
typedef struct { float target[3]; float reject[3]; } color_filter_t;

static const color_filter_t s_color_filter[FRET_COUNT] =
{
    [FRET_GREEN]  = { { 0.0f, 1.0f, 0.0f }, { 0.7f, 0.0f, 0.7f } },
    [FRET_RED]    = { { 0.0f, 0.0f, 1.0f }, { 0.7f, 0.7f, 0.0f } },
    [FRET_YELLOW] = { { 0.0f, 0.5f, 0.5f }, { 1.4f, 0.0f, 0.0f } },
    [FRET_BLUE]   = { { 1.0f, 0.4f, 0.0f }, { 0.0f, 0.0f, 1.4f } },
    [FRET_ORANGE] = { { 0.0f, 0.3f, 0.7f }, { 1.4f, 0.0f, 0.0f } },
};

/* Per-fret detector state. Private — not on the bus. press_count is the
 * rising-edge counter the strum scheduler will eventually consume. */
static float    s_hold_dist[FRET_COUNT];
static float    s_edge_dist[FRET_COUNT];
static bool     s_pressed[FRET_COUNT];
static bool     s_edge_active[FRET_COUNT];
static uint32_t s_press_count[FRET_COUNT];

static StaticQueue_t s_frame_queue_buf;
static uint8_t       s_frame_queue_storage[CV_FRAME_QUEUE_DEPTH * sizeof(Video_FrameInfo)];

static StackType_t   s_task_stack[CV_TASK_STACK_WORDS];
static StaticTask_t  s_task_tcb;

/* ─── Sampling ─────────────────────────────────────────────────────────── */

/* Average BGR over a (2r+1)×(2r+1) patch centered on (px,py), clipping
 * to frame bounds. Center is clamped first so a one-pixel-off calibration
 * still produces a reading instead of a silent zero. */
static void sample_patch_bgr(const uint8_t *frame, uint16_t fw, uint16_t fh,
                             uint16_t px, uint16_t py, float out[3])
{
    int cx = (int)px;
    if (cx < 0)        { cx = 0; }
    else if (cx >= fw) { cx = fw - 1; }
    int cy = (int)py;
    if (cy < 0)        { cy = 0; }
    else if (cy >= fh) { cy = fh - 1; }

    int r = (int)CV_PATCH_RADIUS;
    int x0 = cx - r;       if (x0 < 0)  { x0 = 0; }
    int y0 = cy - r;       if (y0 < 0)  { y0 = 0; }
    int x1 = cx + r + 1;   if (x1 > fw) { x1 = fw; }
    int y1 = cy + r + 1;   if (y1 > fh) { y1 = fh; }

    uint32_t sum_b = 0u, sum_g = 0u, sum_r = 0u;
    uint32_t row_stride = (uint32_t)fw * CV_BYTES_PER_PIXEL;
    for (int y = y0; y < y1; y++)
    {
        const uint8_t *row = frame
                           + (uint32_t)y * row_stride
                           + (uint32_t)x0 * CV_BYTES_PER_PIXEL;
        for (int x = x0; x < x1; x++)
        {
            sum_b += row[0];
            sum_g += row[1];
            sum_r += row[2];
            row   += CV_BYTES_PER_PIXEL;
        }
    }
    uint32_t n = (uint32_t)(x1 - x0) * (uint32_t)(y1 - y0);
    out[0] = (float)sum_b / (float)n;
    out[1] = (float)sum_g / (float)n;
    out[2] = (float)sum_r / (float)n;
}

/* Max channel — triggers on any color or white. */
static float brightness(const float bgr[3])
{
    float m = bgr[0];
    if (bgr[1] > m) { m = bgr[1]; }
    if (bgr[2] > m) { m = bgr[2]; }
    return m;
}

/* Color-filtered signal scaled by (max-min)/max so white/gray patches
 * produce near-zero signal regardless of camera white balance. */
static float color_signal(const float bgr[3], const color_filter_t *f)
{
    float cmax = brightness(bgr);
    if (cmax < 1.0f) { return 0.0f; }
    float cmin = bgr[0];
    if (bgr[1] < cmin) { cmin = bgr[1]; }
    if (bgr[2] < cmin) { cmin = bgr[2]; }
    float sat = (cmax - cmin) / cmax;

    float target = f->target[0]*bgr[0] + f->target[1]*bgr[1] + f->target[2]*bgr[2];
    float reject = f->reject[0]*bgr[0] + f->reject[1]*bgr[1] + f->reject[2]*bgr[2];
    if (reject < 0.0f) { reject = 0.0f; }
    return (target - reject) * sat;
}

/* ─── Detection ────────────────────────────────────────────────────────── */

static void detect_frame(const Video_FrameInfo *frame, QueueHandle_t bus)
{
    detector_state_t state;
    memset(&state, 0, sizeof(state));
    state.frame_epoch  = frame->frame_count;
    state.timestamp_us = (uint64_t)xTaskGetTickCount() * CV_US_PER_TICK;
    state.detector_id  = (uint8_t)DETECTOR_CV_MARVIN_V1;

    const float    hold_release = CV_HOLD_THRESH * CV_HOLD_RELEASE_FRAC;
    const uint8_t *fbuf = (const uint8_t *)frame->buffer;
    uint16_t       fw   = frame->width;
    uint16_t       fh   = frame->height;

    for (uint8_t i = 0u; i < FRET_COUNT; i++)
    {
        sensor_xy_t s = s_sensor_coords[i];
        float hold_bgr[3], edge_bgr[3];
        sample_patch_bgr(fbuf, fw, fh, s.hx, s.hy, hold_bgr);
        sample_patch_bgr(fbuf, fw, fh, s.ex, s.ey, edge_bgr);

        float hd = brightness(hold_bgr);
        float ed = color_signal(edge_bgr, &s_color_filter[i]);
        s_hold_dist[i] = hd;
        s_edge_dist[i] = ed;

        if (!s_pressed[i] && hd > CV_HOLD_THRESH)   { s_pressed[i] = true;  }
        else if (s_pressed[i] && hd < hold_release) { s_pressed[i] = false; }

        bool edge_on = ed > CV_EDGE_THRESH;
        if (edge_on && !s_edge_active[i]) { s_press_count[i]++; }
        s_edge_active[i] = edge_on;

        state.fret[i].pressed = s_pressed[i] ? 1u : 0u;

        /* confidence: edge_dist scaled to 0..65535. ed lives roughly in
         * channel-value units (~0..255) so ×256 fills the range. */
        float c = ed * 256.0f;
        if (c < 0.0f)        { c = 0.0f;     }
        if (c > 65535.0f)    { c = 65535.0f; }
        state.fret[i].confidence = (uint16_t)c;

        /* raw_value: hold_dist (max channel, 0..255). */
        float h = hd;
        if (h < 0.0f)        { h = 0.0f;     }
        if (h > 65535.0f)    { h = 65535.0f; }
        state.fret[i].raw_value = (uint16_t)h;
    }

    (void)xQueueSend(bus, &state, 0);
}

/* ─── Calibration overlay ──────────────────────────────────────────────── */

/* Paints a per-fret ring at each sample point on the just-sampled frame so
 * the user can visually verify alignment with on-screen note targets. The
 * ring radius is outside the 5×5 sample patch — overdrawing here would
 * not affect *this* frame's reads (detect_frame already ran) but a future
 * re-sample on the same ring slot must still see clean source pixels. */
#define CV_OVERLAY_RING_R    4u

static const uint8_t s_overlay_hold_bgr[3] = { 255u, 255u, 255u };  /* white */
static const uint8_t s_overlay_edge_bgr[FRET_COUNT][3] =
{   /* { B,    G,    R } */
    [FRET_GREEN]  = {   0u, 255u,   0u },
    [FRET_RED]    = {   0u,   0u, 255u },
    [FRET_YELLOW] = {   0u, 255u, 255u },
    [FRET_BLUE]   = { 255u,   0u,   0u },
    [FRET_ORANGE] = {   0u, 165u, 255u },
};

static inline void put_pixel_bgr(uint8_t *frame, int fw, int fh,
                                 int x, int y,
                                 uint8_t b, uint8_t g, uint8_t r)
{
    if (x < 0 || x >= fw || y < 0 || y >= fh) { return; }
    uint8_t *p = frame + ((uint32_t)y * (uint32_t)fw + (uint32_t)x) * CV_BYTES_PER_PIXEL;
    p[0] = b; p[1] = g; p[2] = r;
}

/* Bresenham midpoint circle — perimeter only, 8-way symmetric. */
static void draw_ring(uint8_t *frame, int fw, int fh,
                      int cx, int cy, int radius,
                      uint8_t b, uint8_t g, uint8_t r)
{
    int x = radius, y = 0, err = 0;
    while (x >= y)
    {
        put_pixel_bgr(frame, fw, fh, cx + x, cy + y, b, g, r);
        put_pixel_bgr(frame, fw, fh, cx + y, cy + x, b, g, r);
        put_pixel_bgr(frame, fw, fh, cx - y, cy + x, b, g, r);
        put_pixel_bgr(frame, fw, fh, cx - x, cy + y, b, g, r);
        put_pixel_bgr(frame, fw, fh, cx - x, cy - y, b, g, r);
        put_pixel_bgr(frame, fw, fh, cx - y, cy - x, b, g, r);
        put_pixel_bgr(frame, fw, fh, cx + y, cy - x, b, g, r);
        put_pixel_bgr(frame, fw, fh, cx + x, cy - y, b, g, r);
        y++;
        err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0) { x--; err += 1 - 2 * x; }
    }
}

static void draw_filled_disk(uint8_t *frame, int fw, int fh,
                             int cx, int cy, int radius,
                             uint8_t b, uint8_t g, uint8_t r)
{
    int rr = radius * radius;
    for (int dy = -radius; dy <= radius; dy++)
    {
        for (int dx = -radius; dx <= radius; dx++)
        {
            if (dx * dx + dy * dy <= rr)
            {
                put_pixel_bgr(frame, fw, fh, cx + dx, cy + dy, b, g, r);
            }
        }
    }
}

/* Per-fret marker: position ring (always) + center dot when the matching
 * detector is firing this frame. Hold dot = white => brightness over
 * threshold. Edge dot = fret color => color-filtered edge over threshold.
 * Lets the user watch chatter live: a stable note should show a steady
 * dot for the hold duration; a strum window should flash the edge dot
 * once. Constant flicker = noise pushing thresholds. */
#define CV_OVERLAY_DOT_R     1u

static void draw_overlay(uint8_t *frame, uint16_t fw, uint16_t fh)
{
    int w = (int)fw, h = (int)fh;
    for (uint8_t i = 0u; i < FRET_COUNT; i++)
    {
        sensor_xy_t s = s_sensor_coords[i];
        const uint8_t *e = s_overlay_edge_bgr[i];

        draw_ring(frame, w, h, s.hx, s.hy, (int)CV_OVERLAY_RING_R,
                  s_overlay_hold_bgr[0], s_overlay_hold_bgr[1], s_overlay_hold_bgr[2]);
        draw_ring(frame, w, h, s.ex, s.ey, (int)CV_OVERLAY_RING_R,
                  e[0], e[1], e[2]);

        if (s_pressed[i])
        {
            draw_filled_disk(frame, w, h, s.hx, s.hy, (int)CV_OVERLAY_DOT_R,
                             s_overlay_hold_bgr[0], s_overlay_hold_bgr[1], s_overlay_hold_bgr[2]);
        }
        if (s_edge_active[i])
        {
            draw_filled_disk(frame, w, h, s.ex, s.ey, (int)CV_OVERLAY_DOT_R,
                             e[0], e[1], e[2]);
        }
    }
}

/* ─── Task ─────────────────────────────────────────────────────────────── */

static void cv_marvin_v1_task(void *param)
{
    (void)param;

    QueueHandle_t frames = xQueueCreateStatic(CV_FRAME_QUEUE_DEPTH,
                                              sizeof(Video_FrameInfo),
                                              s_frame_queue_storage,
                                              &s_frame_queue_buf);
    configASSERT(frames != NULL);
    bool subscribed = Video_SubscribeFrames(frames);
    configASSERT(subscribed);

    QueueHandle_t bus = Detector_BusQueue();
    configASSERT(bus != NULL);

    LOG_INFO("CV: cv_marvin_v1 started\r\n");

    for (;;)
    {
        Video_FrameInfo frame;
        if (xQueueReceive(frames, &frame, portMAX_DELAY) != pdTRUE) { continue; }

        /* Always drain, even when disabled, so frames don't back up. */
        if (!Detector_IsEnabled(DETECTOR_CV_MARVIN_V1)) { continue; }
        if (frame.buffer == NULL)                       { continue; }
        if (frame.width == 0u || frame.height == 0u)    { continue; }
        if (frame.bytes_per_pixel != CV_BYTES_PER_PIXEL){ continue; }

        detect_frame(&frame, bus);
        draw_overlay((uint8_t *)frame.buffer, frame.width, frame.height);

        // /* ~2 Hz signal dump for threshold tuning. hold/edge values shown
        //  * vs the 50 threshold, with P/E flags reflecting current state. */
        // if ((frame.frame_count % 30u) == 0u)
        // {
        //     LOG_INFO("CV: G %3d/%3d%c%c R %3d/%3d%c%c Y %3d/%3d%c%c B %3d/%3d%c%c O %3d/%3d%c%c\r\n",
        //              (int)s_hold_dist[FRET_GREEN],  (int)s_edge_dist[FRET_GREEN],
        //              s_pressed[FRET_GREEN]      ? 'P' : '.', s_edge_active[FRET_GREEN]  ? 'E' : '.',
        //              (int)s_hold_dist[FRET_RED],    (int)s_edge_dist[FRET_RED],
        //              s_pressed[FRET_RED]        ? 'P' : '.', s_edge_active[FRET_RED]    ? 'E' : '.',
        //              (int)s_hold_dist[FRET_YELLOW], (int)s_edge_dist[FRET_YELLOW],
        //              s_pressed[FRET_YELLOW]     ? 'P' : '.', s_edge_active[FRET_YELLOW] ? 'E' : '.',
        //              (int)s_hold_dist[FRET_BLUE],   (int)s_edge_dist[FRET_BLUE],
        //              s_pressed[FRET_BLUE]       ? 'P' : '.', s_edge_active[FRET_BLUE]   ? 'E' : '.',
        //              (int)s_hold_dist[FRET_ORANGE], (int)s_edge_dist[FRET_ORANGE],
        //              s_pressed[FRET_ORANGE]     ? 'P' : '.', s_edge_active[FRET_ORANGE] ? 'E' : '.');
        // }
    }
}

void CvMarvinV1_Initialize(void)
{
    (void)xTaskCreateStatic(cv_marvin_v1_task,
                            "CvMarvinV1",
                            CV_TASK_STACK_WORDS,
                            NULL,
                            CV_TASK_PRIORITY,
                            s_task_stack,
                            &s_task_tcb);
}
