#include "video.h"

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"

#include "definitions.h"
#include "tc358743.h"
#include "isc_capture.h"

/* 10.1" LVDS panel is 1280x800. MCC auto-initializes XLCDC + LVDSC + GFX2D
 * in SYS_Initialize; the timing engine is already running by the time the
 * task body runs. Auto-init leaves backlight off. */
#define LCD_PANEL_W  1280u
#define LCD_PANEL_H  800u

#define VIDEO_TASK_STACK_WORDS  1024u
#define VIDEO_TASK_PRIORITY     1u
#define VIDEO_POLL_MS           20u

typedef enum
{
    VIDEO_STATE_NONE = 0,
    VIDEO_STATE_ACTIVE,
} video_state_t;

/* Point LCDC HEO at the capture framebuffer at the caller-specified
 * (xpos, ypos) within the 1280x800 panel. HEO (not BASE) because BASE has
 * no window position/size registers — always full-panel, so a 720-wide
 * source on BASE gets read at panel-wide stride and smears line-to-line.
 *
 * RGB_888_PACKED on SAM9X75 LCDC reads memory in B, G, R order per pixel
 * (3 B/pixel dense — Table 44.26), exactly matching the CSI2DC RMS=1 +
 * ISC PACKED32 byte stream that fills g_framebuffer.
 *
 * XLCDC_SetLayerWindowXYSize writes both HEOCFG3 (display size) and
 * HEOCFG4 (source memory size) to the same value, giving 1:1 with the
 * scaler at its default 0x100000 factor — no scaling needed for native
 * 720x480 → 720x480 display. */
static void lcd_bind_capture(uint32_t src_w, uint32_t src_h,
                             uint32_t xpos, uint32_t ypos)
{
    if (xpos + src_w > LCD_PANEL_W || ypos + src_h > LCD_PANEL_H)
    {
        printf("LCD: window %lux%lu @(%lu,%lu) exceeds panel %ux%u; skipping bind\r\n",
               (unsigned long)src_w, (unsigned long)src_h,
               (unsigned long)xpos, (unsigned long)ypos,
               LCD_PANEL_W, LCD_PANEL_H);
        return;
    }

    XLCDC_SetLayerEnable(XLCDC_LAYER_HEO, false, true);
    XLCDC_SetLayerRGBColorMode(XLCDC_LAYER_HEO,
                               XLCDC_RGB_COLOR_MODE_RGB_888_PACKED, false);
    XLCDC_SetLayerAddress(XLCDC_LAYER_HEO,
                          ISC_Capture_GetBufferAddress(), false);
    XLCDC_SetLayerXStride(XLCDC_LAYER_HEO, 0u, false);
    XLCDC_SetLayerWindowXYPos(XLCDC_LAYER_HEO, xpos, ypos, false);
    XLCDC_SetLayerWindowXYSize(XLCDC_LAYER_HEO, src_w, src_h, false);

    /* §44.6.4.7 — discard BASE DMA in the HEO region. HEO with MCC's
     * default SFACTC=4 (A0×As) / DFACTC=6 (1-A0×As) reaches 100%
     * opacity for RGB_888_PACKED: no per-pixel alpha → As is sourced
     * from A0=255 → A0×As=1.0, src factor=1, dst factor=0. BASE pixels
     * in this rect are blended out anyway, so DISCEN tells the BASE
     * channel to skip fetching them. Saves ~35 MB/s at 720×480 / 50 Hz. */
    XLCDC_REGS->LCDC_BASECFG5 = LCDC_BASECFG5_DISCXPOS(xpos)
                              | LCDC_BASECFG5_DISCYPOS(ypos);
    XLCDC_REGS->LCDC_BASECFG6 = LCDC_BASECFG6_DISCXSIZE(src_w - 1u)
                              | LCDC_BASECFG6_DISCYSIZE(src_h - 1u);
    XLCDC_REGS->LCDC_BASECFG4 |= LCDC_BASECFG4_DISCEN_Msk;

    /* BASECFG4-6 are double-buffered; the writes above don't take effect
     * until BASE's attribute update is triggered. The two SetLayerEnable
     * calls below trigger HEO and BASE updates atomically on the next
     * vsync, so the new HEO geometry and BASE DISCEN window switch in
     * together. */
    XLCDC_SetLayerEnable(XLCDC_LAYER_HEO, true, true);
    XLCDC_SetLayerEnable(XLCDC_LAYER_BASE, true, true);

    printf("LCD: HEO bound to capture buf @0x%08lX, "
           "%lux%lu at (%lu,%lu)\r\n",
           (unsigned long)ISC_Capture_GetBufferAddress(),
           (unsigned long)src_w, (unsigned long)src_h,
           (unsigned long)xpos, (unsigned long)ypos);
}

/* Hide HEO and let BASE (Legato UI) fill the whole panel. */
static void lcd_unbind_capture(void)
{
    XLCDC_SetLayerEnable(XLCDC_LAYER_HEO, false, true);
    XLCDC_REGS->LCDC_BASECFG4 &= ~LCDC_BASECFG4_DISCEN_Msk;
    XLCDC_SetLayerEnable(XLCDC_LAYER_BASE, true, true);
}

static bool video_try_start(void)
{
    uint16_t w = 0, h = 0;
    if (!TC358743_GetDetectedFormat(&w, &h)) { return false; }

    /* Pre-condition: bridge is stream-off with lanes parked in LP-11.
     * That holds at boot (init never enables stream) and after every
     * video_stop (TC358743_EnableStream(false) rebuilds the CSI-TX
     * block and parks the lanes — see tc358743_enable_stream). So we
     * can configure host RX directly, then the EnableStream(true)
     * below produces the LP11→HS edge that the PFE needs. */
    if (!ISC_Capture_Configure(w, h)) { return false; }
    /* Layout: video anchored 76 px from top, horizontally centered. Leaves
     * a UI strip below the video on the 1280x800 panel. */
    uint32_t xpos = (LCD_PANEL_W > w) ? (LCD_PANEL_W - w) / 2u : 0u;
    uint32_t ypos = 76u;
    lcd_bind_capture(w, h, xpos, ypos);
    (void)TC358743_EnableStream(true);
    if (!ISC_Capture_Start())
    {
        (void)TC358743_EnableStream(false);
        lcd_unbind_capture();
        return false;
    }
    printf("VIDEO: ACTIVE %ux%u\r\n", w, h);
    return true;
}

static void video_stop(void)
{
    ISC_Capture_Stop();
    (void)TC358743_EnableStream(false);
    lcd_unbind_capture();
    printf("VIDEO: NONE\r\n");
}

static void video_coordinate(void)
{
    static video_state_t state = VIDEO_STATE_NONE;

    bool locked = TC358743_IsLocked();

    switch (state)
    {
        case VIDEO_STATE_NONE:
            if (locked && video_try_start())
            {
                state = VIDEO_STATE_ACTIVE;
            }
            break;

        case VIDEO_STATE_ACTIVE:
            if (!locked)
            {
                video_stop();
                state = VIDEO_STATE_NONE;
            }
            break;
    }
}

static void video_task(void *param)
{
    (void)param;

    /* One-time init in task context (scheduler running, so synchronous
     * I²C and SYS_TIME work). Brings up capture pipeline, TC358743
     * bridge, backlight, and parks the panel in UI-only mode. */
    ISC_Capture_Initialize();
    TC358743_Initialize();
    XLCDC_EnableBacklight();
    lcd_unbind_capture();

    for (;;)
    {
        TC358743_Tasks();
        video_coordinate();
        vTaskDelay(pdMS_TO_TICKS(VIDEO_POLL_MS));
    }
}

void Video_Initialize(void)
{
    /* xTaskCreate is safe to call before vTaskStartScheduler — the task
     * sits Ready until the scheduler picks it. Call from APP_Initialize. */
    (void)xTaskCreate(video_task,
                      "VideoTask",
                      VIDEO_TASK_STACK_WORDS,
                      NULL,
                      VIDEO_TASK_PRIORITY,
                      NULL);
}
