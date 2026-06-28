#include "ui/screens/splash/splash.h"

#include "FreeRTOS.h"
#include "task.h"

#include "definitions.h"            /* DRV_SST26_* */
#include "log.h"
#include "flash/qspi_layout.h"      /* QSPI_SPLASH_OFFSET */
#include "gfx/canvas/gfx_canvas_api.h"
#include "ui/ui_manager.h"          /* CANVAS_SPLASH, BASE_W, BASE_H */

/* Fallback fill: opaque black. The canvas is RGBA_8888 (packs 0xRRGGBBAA), so a
 * little-endian pixel word of 0x000000FF is R=G=B=0, A=0xFF. */
#define SPLASH_FILL        0x000000FFu

/* The splash pixels, also the buffer the splash layer scans out. Non-cached so
 * the QSPI read (CPU copy from the memory-mapped flash) and the LCDC scan-out are
 * coherent without cache maintenance; 32-byte aligned. */
static uint32_t s_fb[BASE_W * BASE_H]
    __attribute__((section(".region_nocache"), aligned(32)));

void Splash_InitSurface(void)
{
    gfxcSetPixelBuffer(CANVAS_SPLASH, BASE_W, BASE_H, GFX_COLOR_MODE_RGBA_8888, s_fb);
}

uint32_t Splash_CanvasId(void)
{
    return CANVAS_SPLASH;
}

static void fill_fallback(void)
{
    for (uint32_t i = 0u; i < (BASE_W * BASE_H); i++) { s_fb[i] = SPLASH_FILL; }
}

bool Splash_Load(void)
{
    /* Read the raw splash straight from QSPI NOR into the framebuffer the splash
     * layer scans out — no SD mount, no decode (the blob is provisioned via
     * openocd/program-qspi.sh). The DRV_SST26 read sets up the QSPI memory-read
     * frame itself and completes synchronously; the poll is a bounded backstop. */
    DRV_HANDLE h = DRV_SST26_Open(DRV_SST26_INDEX, DRV_IO_INTENT_READ);
    if (h == DRV_HANDLE_INVALID)
    {
        LOG_WARN("SPLASH: QSPI open failed\r\n");
        fill_fallback();
        return false;
    }

    TickType_t t0 = xTaskGetTickCount();
    bool ok = DRV_SST26_Read(h, s_fb, sizeof s_fb, QSPI_SPLASH_OFFSET);
    if (ok)
    {
        DRV_SST26_TRANSFER_STATUS st;
        uint32_t guard = 0u;
        do { st = DRV_SST26_TransferStatusGet(h); }
        while ((st == DRV_SST26_TRANSFER_BUSY) && (++guard < 4000000u));
        ok = (st == DRV_SST26_TRANSFER_COMPLETED);
    }
    DRV_SST26_Close(h);

    if (!ok)
    {
        LOG_WARN("SPLASH: QSPI read failed\r\n");
        fill_fallback();
        return false;
    }

    uint32_t ms = (uint32_t)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS);
    LOG_INFO("SPLASH: %u B from QSPI in %lu ms\r\n", (unsigned)sizeof s_fb, (unsigned long)ms);
    return true;
}
