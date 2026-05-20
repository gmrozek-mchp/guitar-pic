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

#define LCD_PANEL_W  1280u
#define LCD_PANEL_H  800u

#define VIDEO_TASK_STACK_WORDS  1024u
#define VIDEO_TASK_PRIORITY     1u
#define VIDEO_POLL_MS           20u

/* Capture pixels are RGB_888_PACKED, 3 bytes per pixel. Hard-coded to match
 * isc_capture's ISC_CAP_BPP. If that ever becomes runtime-configurable, push
 * it into a getter. */
#define VIDEO_BYTES_PER_PIXEL   3u

/* Public intent — set via the API, read by the task body. Single-writer
 * (each setter is called by one task), single-reader (video task), so
 * plain volatile is sufficient on a 32-bit MCU. */
static volatile bool s_capture_enabled;
static volatile bool s_display_shown;

/* Display window. Zero-initialized; the app must call Video_SetWindow
 * before Video_DisplayShow has any visible effect. Layout is app policy,
 * not module policy. */
typedef struct { uint32_t x, y, w, h; } window_t;
static volatile window_t s_window;

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

/* ─── HEO scaler helper ────────────────────────────────────────────────── */

/* HEO scaler factor is 12.20 fixed-point, factor = (src / dst) << 20.
 * 1.0 (= 0x100000) means 1:1, no scaling. Mirrors plib_xlcdc.c's
 * static CALC_SCALING_FACT. */
static uint32_t scaler_factor(uint32_t src, uint32_t dst)
{
    if (dst == 0u) { return 0x100000u; }
    return (uint32_t)(((uint64_t)src << 20) / dst);
}

/* ─── HEO bind / unbind ────────────────────────────────────────────────── */

/* Point HEO at the capture buffer at (x, y) sized dst_w × dst_h. If the
 * destination size matches the source size, the HEO scaler stays at its
 * 1.0 factor (effectively bypassed). Otherwise the bilinear scaler is
 * engaged. BASE DISCEN is set to the destination rect so BASE skips DMA
 * for the area HEO covers (§44.6.4.7). */
static void lcd_bind(uint32_t src_w, uint32_t src_h,
                     uint32_t x, uint32_t y, uint32_t dst_w, uint32_t dst_h)
{
    if (x + dst_w > LCD_PANEL_W || y + dst_h > LCD_PANEL_H)
    {
        LOG_WARN("VIDEO: window %lux%lu @(%lu,%lu) exceeds panel %ux%u; skipping bind\r\n",
                 (unsigned long)dst_w, (unsigned long)dst_h,
                 (unsigned long)x, (unsigned long)y,
                 LCD_PANEL_W, LCD_PANEL_H);
        return;
    }

    /* Initial HEO address: the latest completed buffer if we have one,
     * else fall back to the framebuffer base. The on_frame_done IRQ
     * re-points HEO at every subsequent completed buffer, so this is
     * the seed that holds for at most one frame-time before the first
     * IRQ overwrites it. */
    uint32_t initial_addr = s_latest_buffer;
    if (initial_addr == 0u) { initial_addr = ISC_Capture_GetBufferAddress(); }

    XLCDC_SetLayerEnable(XLCDC_LAYER_HEO, false, true);
    XLCDC_SetLayerRGBColorMode(XLCDC_LAYER_HEO,
                               XLCDC_RGB_COLOR_MODE_RGB_888_PACKED, false);
    XLCDC_SetLayerAddress(XLCDC_LAYER_HEO, initial_addr, false);
    XLCDC_SetLayerXStride(XLCDC_LAYER_HEO, 0u, false);
    XLCDC_SetLayerWindowXYPos(XLCDC_LAYER_HEO, x, y, false);

    /* Display rect (HEOCFG3) and source-memory rect (HEOCFG4) are the same
     * for 1:1 and differ for scaling. Write them directly — XLCDC_Set-
     * LayerWindowXYSize writes both registers to the same value, which is
     * only correct for 1:1. */
    XLCDC_REGS->LCDC_HEOCFG3 = LCDC_HEOCFG3_XSIZE(dst_w - 1u)
                             | LCDC_HEOCFG3_YSIZE(dst_h - 1u);
    XLCDC_REGS->LCDC_HEOCFG4 = LCDC_HEOCFG4_XMEMSIZE(src_w - 1u)
                             | LCDC_HEOCFG4_YMEMSIZE(src_h - 1u);

    bool scaling = (dst_w != src_w) || (dst_h != src_h);
    if (scaling)
    {
        uint32_t hf = scaler_factor(src_w, dst_w);
        uint32_t vf = scaler_factor(src_h, dst_h);

        XLCDC_REGS->LCDC_HEOCFG23 = LCDC_HEOCFG23_VXSYEN(1)
                                  | LCDC_HEOCFG23_VXSCEN(1)
                                  | LCDC_HEOCFG23_HXSYEN(1)
                                  | LCDC_HEOCFG23_HXSCEN(1);
        /* Bicubic luminance + bicubic chrominance, both planes. Mirrors
         * MCC's surface-set path — best image quality for video. */
        XLCDC_REGS->LCDC_HEOCFG30 = LCDC_HEOCFG30_VXSYCFG(1)
                                  | LCDC_HEOCFG30_VXSYBICU(1)
                                  | LCDC_HEOCFG30_VXSCCFG(1)
                                  | LCDC_HEOCFG30_VXSCBICU(1);
        XLCDC_REGS->LCDC_HEOCFG31 = LCDC_HEOCFG31_HXSYCFG(1)
                                  | LCDC_HEOCFG31_HXSYBICU(1)
                                  | LCDC_HEOCFG31_HXSCCFG(1)
                                  | LCDC_HEOCFG31_HXSCBICU(1);
        XLCDC_REGS->LCDC_HEOCFG24 = LCDC_HEOCFG24_VXSYFACT(vf);
        XLCDC_REGS->LCDC_HEOCFG25 = LCDC_HEOCFG25_VXSCFACT(vf);
        XLCDC_REGS->LCDC_HEOCFG26 = LCDC_HEOCFG26_HXSYFACT(hf);
        XLCDC_REGS->LCDC_HEOCFG27 = LCDC_HEOCFG27_HXSCFACT(hf);
    }
    else
    {
        /* Disable scaler — matches MCC's HEO-setup defaults. */
        XLCDC_REGS->LCDC_HEOCFG23 = LCDC_HEOCFG23_VXSYEN(0)
                                  | LCDC_HEOCFG23_VXSCEN(0)
                                  | LCDC_HEOCFG23_HXSYEN(0)
                                  | LCDC_HEOCFG23_HXSCEN(0);
    }

    /* §44.6.4.7 — discard BASE DMA in the destination rect. HEO with MCC's
     * default SFACTC=4 (A0×As) / DFACTC=6 (1−A0×As) reaches 100% opacity
     * for RGB_888_PACKED (no per-pixel alpha → As sourced from A0=255),
     * so BASE pixels in this rect are blended out. DISCEN tells the BASE
     * channel to skip fetching them. */
    XLCDC_REGS->LCDC_BASECFG5 = LCDC_BASECFG5_DISCXPOS(x)
                              | LCDC_BASECFG5_DISCYPOS(y);
    XLCDC_REGS->LCDC_BASECFG6 = LCDC_BASECFG6_DISCXSIZE(dst_w - 1u)
                              | LCDC_BASECFG6_DISCYSIZE(dst_h - 1u);
    XLCDC_REGS->LCDC_BASECFG4 |= LCDC_BASECFG4_DISCEN_Msk;

    /* HEO and BASE attribute updates triggered together. */
    XLCDC_SetLayerEnable(XLCDC_LAYER_HEO, true, true);
    XLCDC_SetLayerEnable(XLCDC_LAYER_BASE, true, true);

    LOG_INFO("VIDEO: HEO bound src %lux%lu → dst %lux%lu @(%lu,%lu) %s\r\n",
             (unsigned long)src_w, (unsigned long)src_h,
             (unsigned long)dst_w, (unsigned long)dst_h,
             (unsigned long)x, (unsigned long)y,
             scaling ? "scaled" : "1:1");
}

/* Hide HEO and let BASE (Legato UI) fill the whole panel. */
static void lcd_unbind(void)
{
    XLCDC_SetLayerEnable(XLCDC_LAYER_HEO, false, true);
    XLCDC_REGS->LCDC_BASECFG4 &= ~LCDC_BASECFG4_DISCEN_Msk;
    XLCDC_SetLayerEnable(XLCDC_LAYER_BASE, true, true);
}

/* ─── Frame-done ISR (runs in IRQ context) ─────────────────────────────── */

static void on_frame_done(uint32_t frame_count,
                          uint32_t buffer_addr,
                          uintptr_t ctx)
{
    (void)ctx;

    s_latest_buffer = buffer_addr;

    /* Re-point HEO at the buffer that was just completed so the panel sees
     * every captured frame. With the descriptor ring and a static base
     * binding, HEO would only see frames written to slot 0. The address
     * latches at next vsync (XLCDC update=true); HEO and ISC run on
     * independent clocks so this is fine. Skip when display is hidden. */
    if (s_display_bound)
    {
        XLCDC_SetLayerAddress(XLCDC_LAYER_HEO, buffer_addr, true);
    }

    Video_FrameInfo info =
    {
        .buffer          = (void *)(uintptr_t)buffer_addr,
        .frame_count     = frame_count,
        .width           = s_src_w,
        .height          = s_src_h,
        .bytes_per_pixel = VIDEO_BYTES_PER_PIXEL,
    };

    BaseType_t higher_priority_task_woken = pdFALSE;
    for (uint8_t i = 0u; i < VIDEO_MAX_SUBSCRIBERS; i++)
    {
        QueueHandle_t q = s_subscribers[i];
        if (q != NULL)
        {
            (void)xQueueSendFromISR(q, &info, &higher_priority_task_woken);
        }
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/* ─── Capture state machine ────────────────────────────────────────────── */

static bool s_capture_armed;            /* task-only state; tracks ISC running */
static volatile bool s_display_bound;   /* read by IRQ; written by task        */

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

/* Bind/unbind display from current intent + most-recent source size +
 * caller-set window. A zero-sized window means the app hasn't set one
 * yet — silently skip the bind in that case rather than emit a malformed
 * HEO geometry; DisplayShow without a prior SetWindow is a no-op. */
static void display_apply(void)
{
    window_t w = s_window;
    bool window_valid = (w.w != 0u) && (w.h != 0u);
    bool source_valid = (s_src_w != 0u) && (s_src_h != 0u);
    bool want = s_display_shown && source_valid && window_valid;

    if (want && !s_display_bound)
    {
        lcd_bind(s_src_w, s_src_h, w.x, w.y, w.w, w.h);
        s_display_bound = true;
    }
    else if (!want && s_display_bound)
    {
        lcd_unbind();
        s_display_bound = false;
    }
}

/* Each tick: align current chain state (capture armed / display bound) to
 * the user's intent flags, given the current source-lock state. */
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

    /* Display: shown iff intent + we have a source size to work with. The
     * source size is captured at arm time and persists across capture
     * teardown so the user can leave HEO bound on a stale frame. */
    display_apply();
}

/* ─── Task body ────────────────────────────────────────────────────────── */

static void video_task(void *param)
{
    (void)param;

    /* One-time init in task context (scheduler running, so synchronous I²C
     * and SYS_TIME work). Brings up capture pipeline, TC358743 bridge,
     * backlight, and parks the panel in UI-only mode. */
    ISC_Capture_Initialize();
    ISC_Capture_SetFrameCallback(on_frame_done, 0);
    TC358743_Initialize();
    XLCDC_EnableBacklight();
    lcd_unbind();

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
    (void)xTaskCreateStatic(video_task,
                            "VideoTask",
                            VIDEO_TASK_STACK_WORDS,
                            NULL,
                            VIDEO_TASK_PRIORITY,
                            s_task_stack,
                            &s_task_tcb);
}

void Video_CaptureEnable(void)  { s_capture_enabled = true;  }
void Video_CaptureDisable(void) { s_capture_enabled = false; }

void Video_DisplayShow(void)    { s_display_shown = true;    }
void Video_DisplayHide(void)    { s_display_shown = false;   }

void Video_SetWindow(uint32_t x, uint32_t y, uint32_t dst_w, uint32_t dst_h)
{
    window_t w = { x, y, dst_w, dst_h };
    s_window = w;
    /* Force a re-bind on next reconcile if we're currently shown. */
    s_display_bound = false;
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
