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
#include "perf_log/perf_log.h"

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

/* SENSING scratch is sized for the largest configured strip so a runtime
 * config swap can't overflow it (1p's 185×48 dominates 2p-left's 166×48).
 *
 * 48 rows rather than the sensor row's immediate neighbourhood: the rows below
 * the sample point are where a gem's descent is visible, so they carry its
 * velocity and separate a note head from a sustain trail. Offline work needs
 * that to derive the observation lead instead of hand-tuning it. Paid for by
 * halving STRIKE (see decimate2_bgr). */
#define CV_SENSING_MAX_W       185u
#define CV_SENSING_MAX_H       48u

/* STRIKE is shipped 2:1-decimated in both axes — it carries no sensors and is
 * read by eye, for hit flashes and the fret-pressed highlight, both of which are
 * large high-contrast features that survive the halving. Scratch is sized for the
 * largest decimated strike (1p 290×32 → 145×16; 2p-left 205×34 → 102×17). */
#define CV_STRIKE_HALF_MAX_W   145u
#define CV_STRIKE_HALF_MAX_H   17u

/* Per-highway geometry, selectable at runtime via CvMarvinV1_SetConfig.
 * (hx,hy) = brightness sensor, (ex,ey) = color-filtered edge sensor. 1p coords
 * derive from fret-tuner detect_video.py defaults via the Elgato HDMI capture.
 * 2p-left sits lower than 1p (row 337 vs 311) because its highway is farther
 * from the camera: at a matched row the same screen distance is more note
 * travel time, so the lower row is what makes both leads agree — see the fan
 * construction in tools/gameplay/docs/journal.md. SENSING spans the sensor row
 * (rings painted here); STRIKE spans the strum zone below. */
const cv_marvin_v1_config_t CV_MARVIN_CFG_1P =
{
    .name = "1p",
    .sensor =
    {
        [FRET_GREEN]  = { 280, 311, 268, 311 },
        [FRET_RED]    = { 317, 311, 304, 311 },
        [FRET_YELLOW] = { 355, 311, 368, 311 },
        [FRET_BLUE]   = { 393, 311, 406, 311 },
        [FRET_ORANGE] = { 430, 311, 442, 311 },
    },
    .sensing_x = 265u, .sensing_y = 300u, .sensing_w = 185u, .sensing_h = 48u,
    .strike_x  = 212u, .strike_y  = 395u, .strike_w  = 290u, .strike_h  = 32u,
    .sensing_kind = PERF_STRIP_SENSING,
    .strike_kind  = PERF_STRIP_STRIKE_HALF,
    .lead_slot    = CV_LEAD_SLOT_1P,
};

const cv_marvin_v1_config_t CV_MARVIN_CFG_2P_LEFT =
{
    .name = "2p-left",
    .sensor =
    {
        [FRET_GREEN]  = { 158, 337, 148, 337 },
        [FRET_RED]    = { 191, 337, 180, 337 },
        [FRET_YELLOW] = { 223, 337, 234, 337 },
        [FRET_BLUE]   = { 255, 337, 266, 337 },
        [FRET_ORANGE] = { 287, 337, 297, 337 },
    },
    .sensing_x = 140u, .sensing_y = 326u, .sensing_w = 166u, .sensing_h = 48u,
    .strike_x  = 120u, .strike_y  = 395u, .strike_w  = 205u, .strike_h  = 34u,
    .sensing_kind = PERF_STRIP_SENSING_2P,
    .strike_kind  = PERF_STRIP_STRIKE_2P_HALF,
    .lead_slot    = CV_LEAD_SLOT_2P_LEFT,
};

/* Active geometry, and a pending swap picked up by the task on the next frame. */
static const cv_marvin_v1_config_t *volatile s_active_cfg  = &CV_MARVIN_CFG_1P;
static const cv_marvin_v1_config_t *volatile s_pending_cfg = NULL;

/* Observation lead per (highway, difficulty), in ms. RAM rather than const so
 * `cvdiff` can retune a cell live against a running game — calibrating this by
 * rebuild is impractical.
 *
 * Easy (420) and hard (250) are measured, roughly; medium and expert are
 * *derived*, not measured — taking scroll speed as linear in difficulty index,
 * the lead is its harmonic interpolation/extrapolation from those two points
 * (1/420 and 1/250 give ≈313 and ≈208, rounded so they don't read as
 * measurements).
 *
 * The two highways hold the same numbers because 2p-left's sensor row was
 * placed to make them agree, not because the slots are required to match —
 * the row is the calibration knob, this table is where a rig disagreement
 * gets recorded. The 2p column is a geometric prediction, not yet measured. */
static uint16_t s_lead_ms[CV_LEAD_SLOT_COUNT][CV_DIFF_COUNT] =
{
    /*                       easy  medium  hard  expert */
    [CV_LEAD_SLOT_1P]      = { 420u, 315u, 250u, 210u },
    [CV_LEAD_SLOT_2P_LEFT] = { 390u, 290u, 210u, 150u },
};

/* Active difficulty (a CV_DIFF_COUNT-range index matching game_difficulty_t).
 * Boots to hard: it is a measured tier, and it matches the fretboard node's
 * MODEL_SEL_DEFAULT. A real run always sets it from the committed selection, so
 * this only governs a console-driven or attach-mode session. */
static volatile uint8_t s_difficulty = 2u;   /* hard */

/* Raised by any setter that changes what publish_detector_config would emit;
 * the task clears it and re-publishes before the next frame. */
static volatile bool s_cfg_dirty = false;

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

/* Tightly-packed scratch for the SENSING strip: the region is row-copied here,
 * the rings are painted on the copy, then it's shipped via PerfLog_EmitStripPacked
 * — the source capture frame is never written, so snapshots and the LVDS panel
 * stay clean. */
static uint8_t s_sensing_scratch[CV_SENSING_MAX_W * CV_SENSING_MAX_H * CV_BYTES_PER_PIXEL];

/* Tightly-packed scratch for the decimated STRIKE strip. */
static uint8_t s_strike_scratch[CV_STRIKE_HALF_MAX_W * CV_STRIKE_HALF_MAX_H
                                * CV_BYTES_PER_PIXEL];

static StackType_t   s_task_stack[CV_TASK_STACK_WORDS];
static StaticTask_t  s_task_tcb;

/* ─── Strip helpers ────────────────────────────────────────────────────── */

/* Box-average a w×h BGR888 region 2:1 in each axis into tightly-packed `dst`,
 * which must hold (w/2)*(h/2) pixels. An odd trailing row or column is dropped,
 * so the caller's (w/2, h/2) is exactly what lands on the wire. */
static void decimate2_bgr(const uint8_t *src, uint32_t src_stride,
                          uint16_t w, uint16_t h, uint8_t *dst)
{
    uint16_t ow = (uint16_t)(w / 2u);
    uint16_t oh = (uint16_t)(h / 2u);

    for (uint16_t oy = 0u; oy < oh; oy++)
    {
        const uint8_t *r0 = src + (uint32_t)(2u * oy) * src_stride;
        const uint8_t *r1 = r0 + src_stride;
        for (uint16_t ox = 0u; ox < ow; ox++)
        {
            uint32_t off = (uint32_t)(2u * ox) * CV_BYTES_PER_PIXEL;
            const uint8_t *a = r0 + off;
            const uint8_t *b = a + CV_BYTES_PER_PIXEL;
            const uint8_t *c = r1 + off;
            const uint8_t *d = c + CV_BYTES_PER_PIXEL;
            for (uint8_t ch = 0u; ch < CV_BYTES_PER_PIXEL; ch++)
            {
                uint32_t sum = (uint32_t)a[ch] + b[ch] + c[ch] + d[ch] + 2u;
                *dst++ = (uint8_t)(sum / 4u);
            }
        }
    }
}

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

/* Lead for the active (highway, difficulty) cell. Both indices are read once so
 * a concurrent setter can't split them across the two lookups. */
static uint16_t active_lead_ms(const cv_marvin_v1_config_t *cfg)
{
    uint8_t slot = cfg->lead_slot;
    uint8_t diff = s_difficulty;
    if (slot >= CV_LEAD_SLOT_COUNT) { slot = CV_LEAD_SLOT_1P; }
    if (diff >= CV_DIFF_COUNT)      { diff = 0u; }
    return s_lead_ms[slot][diff];
}

static void detect_frame(const Video_FrameInfo *frame,
                         const cv_marvin_v1_config_t *cfg)
{
    detector_state_t state;
    memset(&state, 0, sizeof(state));
    state.frame_epoch  = frame->frame_count;
    state.timestamp_us = frame->timestamp_us;   /* capture time, not detect time */
    state.strike_at_ms = (uint32_t)(state.timestamp_us / 1000ull)
                       + active_lead_ms(cfg);
    state.detector_id  = (uint8_t)DETECTOR_CV_MARVIN_V1;

    const float    hold_release = CV_HOLD_THRESH * CV_HOLD_RELEASE_FRAC;
    const uint8_t *fbuf = (const uint8_t *)frame->buffer;
    uint16_t       fw   = frame->width;
    uint16_t       fh   = frame->height;

    for (uint8_t i = 0u; i < FRET_COUNT; i++)
    {
        cv_sensor_xy_t s = cfg->sensor[i];
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

    Detector_Publish(&state);

    /* Mirror the same per-frame decision into the perf-log so offline tools
     * (marvin-perf, export-ml) can join detector ground truth with sensor
     * input. raw_value/confidence already scale 0..65535 to match the
     * perf-log struct. masks are LSB-first per fret_t. Default-disabled at
     * boot like the other high-rate types; host enables via SET_TYPE_MASK. */
    uint16_t hold_dist[FRET_COUNT];
    uint16_t edge_dist[FRET_COUNT];
    uint8_t  pressed_mask = 0u;
    uint8_t  edge_active_mask = 0u;
    for (uint8_t i = 0u; i < FRET_COUNT; i++)
    {
        hold_dist[i] = state.fret[i].raw_value;
        edge_dist[i] = state.fret[i].confidence;
        if (s_pressed[i])     { pressed_mask     |= (uint8_t)(1u << i); }
        if (s_edge_active[i]) { edge_active_mask |= (uint8_t)(1u << i); }
    }
    PerfLog_EmitDetector(state.frame_epoch, hold_dist, edge_dist,
                         pressed_mask, edge_active_mask);
}

/* ─── Calibration overlay ──────────────────────────────────────────────── */

/* Paints a per-fret ring at each sample point onto a target buffer (the SENSING
 * strip copy), translated by the buffer's origin in frame space, so the user
 * can visually verify alignment with on-screen note targets in the host viewer.
 * The source capture frame is never touched. The ring radius is outside the 5×5
 * sample patch; the detector has already sampled by the time this runs. */
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

/* Draw the per-fret rings/dots into `buf` (bw×bh, tightly packed), with sensor
 * coords translated by the buffer's frame-space origin (ox, oy). Pixels falling
 * outside the buffer are clipped by put_pixel_bgr. */
static void draw_overlay(uint8_t *buf, uint16_t bw, uint16_t bh,
                         uint16_t ox, uint16_t oy,
                         const cv_marvin_v1_config_t *cfg)
{
    int w = (int)bw, h = (int)bh;
    for (uint8_t i = 0u; i < FRET_COUNT; i++)
    {
        cv_sensor_xy_t s = cfg->sensor[i];
        const uint8_t *e = s_overlay_edge_bgr[i];
        int hx = (int)s.hx - (int)ox, hy = (int)s.hy - (int)oy;
        int ex = (int)s.ex - (int)ox, ey = (int)s.ey - (int)oy;

        draw_ring(buf, w, h, hx, hy, (int)CV_OVERLAY_RING_R,
                  s_overlay_hold_bgr[0], s_overlay_hold_bgr[1], s_overlay_hold_bgr[2]);
        draw_ring(buf, w, h, ex, ey, (int)CV_OVERLAY_RING_R,
                  e[0], e[1], e[2]);

        if (s_pressed[i])
        {
            draw_filled_disk(buf, w, h, hx, hy, (int)CV_OVERLAY_DOT_R,
                             s_overlay_hold_bgr[0], s_overlay_hold_bgr[1], s_overlay_hold_bgr[2]);
        }
        if (s_edge_active[i])
        {
            draw_filled_disk(buf, w, h, ex, ey, (int)CV_OVERLAY_DOT_R,
                             e[0], e[1], e[2]);
        }
    }
}

/* ─── Detector-config publish ──────────────────────────────────────────── */

/* Snapshot the cv_marvin_v1 tables (sample coords, thresholds, color filter
 * weights) plus the active observation lead into a wire record. The coords and
 * thresholds are still compile-time; the lead is not, which is why every setter
 * routes back through here so a capture always records the lead that produced
 * its detector decisions. */
static void publish_detector_config(const cv_marvin_v1_config_t *geom)
{
    perf_rec_detector_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    for (uint8_t i = 0u; i < FRET_COUNT; i++)
    {
        cfg.sensor_hx[i]      = geom->sensor[i].hx;
        cfg.sensor_hy[i]      = geom->sensor[i].hy;
        cfg.sensor_ex[i]      = geom->sensor[i].ex;
        cfg.sensor_ey[i]      = geom->sensor[i].ey;
        cfg.color_target_b[i] = s_color_filter[i].target[0];
        cfg.color_target_g[i] = s_color_filter[i].target[1];
        cfg.color_target_r[i] = s_color_filter[i].target[2];
        cfg.color_reject_b[i] = s_color_filter[i].reject[0];
        cfg.color_reject_g[i] = s_color_filter[i].reject[1];
        cfg.color_reject_r[i] = s_color_filter[i].reject[2];
    }
    cfg.hold_thresh          = CV_HOLD_THRESH;
    cfg.hold_release_frac    = CV_HOLD_RELEASE_FRAC;
    cfg.edge_thresh          = CV_EDGE_THRESH;
    cfg.observation_lead_ms  = active_lead_ms(geom);
    cfg.difficulty           = s_difficulty;
    cfg.lead_slot            = geom->lead_slot;
    PerfLog_EmitDetectorConfig(&cfg);
}

/* Clear per-fret latch/hysteresis state — called on a geometry swap so stale
 * press/edge latches from the old highway don't leak into the new one. */
static void reset_detector_state(void)
{
    memset(s_hold_dist,   0, sizeof(s_hold_dist));
    memset(s_edge_dist,   0, sizeof(s_edge_dist));
    memset(s_pressed,     0, sizeof(s_pressed));
    memset(s_edge_active, 0, sizeof(s_edge_active));
    memset(s_press_count, 0, sizeof(s_press_count));
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

    LOG_INFO("CV: cv_marvin_v1 started\r\n");

    publish_detector_config(s_active_cfg);

    for (;;)
    {
        Video_FrameInfo frame;
        if (xQueueReceive(frames, &frame, portMAX_DELAY) != pdTRUE) { continue; }

        /* Apply a pending geometry swap before touching the frame, even when
         * disabled, so a config change while paused takes effect on resume.
         * A difficulty/lead change arrives through s_cfg_dirty alone: the
         * sensors haven't moved, so the latch state must survive it. */
        const cv_marvin_v1_config_t *pending = s_pending_cfg;
        if (pending != NULL)
        {
            s_pending_cfg = NULL;
            s_active_cfg  = pending;
            reset_detector_state();
            s_cfg_dirty   = true;
            LOG_INFO("CV: config -> %s\r\n", pending->name);
        }
        const cv_marvin_v1_config_t *cfg = s_active_cfg;
        if (s_cfg_dirty)
        {
            s_cfg_dirty = false;
            publish_detector_config(cfg);
        }

        /* Always drain, even when disabled, so frames don't back up. */
        if (!Detector_IsEnabled(DETECTOR_CV_MARVIN_V1)) { continue; }
        if (frame.buffer == NULL)                       { continue; }
        if (frame.width == 0u || frame.height == 0u)    { continue; }
        if (frame.bytes_per_pixel != CV_BYTES_PER_PIXEL){ continue; }

        PerfLog_EmitStamp(PERF_STAGE_CV_START, frame.frame_count, 0u);
        detect_frame(&frame, cfg);
        PerfLog_EmitStamp(PERF_STAGE_CV_END, frame.frame_count, 0u);

        /* Re-emit detector config at ~1 Hz so a mid-stream host attach
         * picks it up within a second of frames flowing. Cheap (~188 B/s
         * × 1 record/s). On-change emits land above via the pending-swap path. */
        if ((frame.frame_count % 60u) == 0u) { publish_detector_config(cfg); }

        const uint32_t fstride = (uint32_t)frame.width * CV_BYTES_PER_PIXEL;

        /* STRIKE: the strum trigger zone, 2:1-decimated into scratch (no sensors
         * there → no rings, and nothing reads it but the host viewer). Gated on
         * the STRIP mask like SENSING below, so the box-average costs nothing
         * when the host isn't consuming strips. */
        if ((PerfLog_GetEnabledMask() & (1u << PERF_REC_STRIP)) != 0u)
        {
            const uint8_t *ssrc = (const uint8_t *)frame.buffer
                                + (uint32_t)cfg->strike_y * fstride
                                + (uint32_t)cfg->strike_x * CV_BYTES_PER_PIXEL;
            decimate2_bgr(ssrc, fstride, cfg->strike_w, cfg->strike_h,
                          s_strike_scratch);
            PerfLog_EmitStripPacked(frame.frame_count,
                                    (perf_strip_kind_t)cfg->strike_kind,
                                    cfg->strike_x, cfg->strike_y,
                                    (uint16_t)(cfg->strike_w / 2u),
                                    (uint16_t)(cfg->strike_h / 2u),
                                    s_strike_scratch);
        }

        /* SENSING: copy the sensor row into scratch and (when the overlay sink
         * is on) paint the target rings onto the copy before shipping it. The
         * source frame is never written, so snapshots and the LVDS panel stay
         * clean. Gate the copy on the STRIP mask so it costs nothing when the
         * host isn't consuming strips. */
        if ((PerfLog_GetEnabledMask() & (1u << PERF_REC_STRIP)) != 0u)
        {
            const uint8_t *src = (const uint8_t *)frame.buffer
                               + (uint32_t)cfg->sensing_y * fstride
                               + (uint32_t)cfg->sensing_x * CV_BYTES_PER_PIXEL;
            const uint32_t row_bytes = (uint32_t)cfg->sensing_w * CV_BYTES_PER_PIXEL;
            uint8_t *dst = s_sensing_scratch;
            for (uint16_t row = 0u; row < cfg->sensing_h; row++)
            {
                memcpy(dst, src, row_bytes);
                src += fstride;
                dst += row_bytes;
            }
            if ((PerfLog_GetOverlayFlags() & PERF_OVERLAY_STRIP) != 0u)
            {
                draw_overlay(s_sensing_scratch, cfg->sensing_w, cfg->sensing_h,
                             cfg->sensing_x, cfg->sensing_y, cfg);
            }
            PerfLog_EmitStripPacked(frame.frame_count,
                                    (perf_strip_kind_t)cfg->sensing_kind,
                                    cfg->sensing_x, cfg->sensing_y,
                                    cfg->sensing_w, cfg->sensing_h, s_sensing_scratch);
        }

        /* REGION: host-selected sub-region (e.g. the score block), streamed one
         * strip per frame when the host has started the stream. No-op otherwise. */
        PerfLog_EmitRegionIfEnabled(frame.frame_count, (const uint8_t *)frame.buffer,
                                    fstride, frame.width, frame.height);

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

void CvMarvinV1_SetConfig(const cv_marvin_v1_config_t *cfg)
{
    if (cfg != NULL) { s_pending_cfg = cfg; }
}

const cv_marvin_v1_config_t *CvMarvinV1_GetConfig(void)
{
    return s_active_cfg;
}

void CvMarvinV1_SetDifficulty(uint8_t difficulty)
{
    if (difficulty >= CV_DIFF_COUNT)   { return; }
    if (difficulty == s_difficulty)    { return; }
    s_difficulty = difficulty;
    s_cfg_dirty  = true;
}

uint8_t CvMarvinV1_GetDifficulty(void)
{
    return s_difficulty;
}

uint16_t CvMarvinV1_GetLeadMs(uint8_t slot, uint8_t difficulty)
{
    if (slot >= CV_LEAD_SLOT_COUNT || difficulty >= CV_DIFF_COUNT) { return 0u; }
    return s_lead_ms[slot][difficulty];
}

void CvMarvinV1_SetLeadMs(uint8_t slot, uint8_t difficulty, uint16_t ms)
{
    if (slot >= CV_LEAD_SLOT_COUNT || difficulty >= CV_DIFF_COUNT) { return; }
    s_lead_ms[slot][difficulty] = ms;
    s_cfg_dirty = true;
}

void CvMarvinV1_Initialize(void)
{
    TaskHandle_t h = xTaskCreateStatic(cv_marvin_v1_task,
                                       "CvMarvinV1",
                                       CV_TASK_STACK_WORDS,
                                       NULL,
                                       CV_TASK_PRIORITY,
                                       s_task_stack,
                                       &s_task_tcb);
    PerfLog_RegisterTaskForHighwater(PERF_TASK_CV_MARVIN_V1, h);
}
