#include "ui/screens/splash/screen_splash.h"

#include "FreeRTOS.h"
#include "task.h"

#include "definitions.h"            /* DRV_SST26_*, XLCDC_* */
#include "log.h"
#include "flash/qspi_layout.h"      /* QSPI_SPLASH_OFFSET */
#include "ui/ui_manager.h"          /* BASE_W, BASE_H */

/* Fallback fill: opaque black. The layer is RGBA_8888 (packs 0xRRGGBBAA), so a
 * little-endian pixel word of 0x000000FF is R=G=B=0, A=0xFF. */
#define SPLASH_FILL        0x000000FFu

/* The splash pixels, also the buffer the splash layer scans out. Non-cached so
 * the QSPI read (CPU copy from the memory-mapped flash) and the LCDC scan-out are
 * coherent without cache maintenance; 32-byte aligned. */
static uint32_t s_fb[BASE_W * BASE_H]
    __attribute__((section(".region_nocache"), aligned(32)));

void ScreenSplash_Show(XLCDC_LAYER layer)
{
    /* Drive the overlay layer directly — no GFX canvas. Set every attribute
     * deferred (update=false) then enable with update=true so they latch together
     * at the next vsync. Mirrors the XLCDC GFX driver's own layer-commit sequence
     * (drv_gfx_xlcdc.c): opaque (alpha 255), DMA enabled, full-screen, zero stride
     * (the buffer width equals the window width). */
    XLCDC_SetLayerRGBColorMode(layer, XLCDC_RGB_COLOR_MODE_RGBA_8888, false);
    XLCDC_SetLayerAddress(layer, (uint32_t)(uintptr_t)s_fb, false);
    XLCDC_SetLayerOpts(layer, 255u, true, false);
    XLCDC_SetLayerWindowXYPos(layer, 0u, 0u, false);
    XLCDC_SetLayerWindowXYSize(layer, BASE_W, BASE_H, false);
    XLCDC_SetLayerXStride(layer, 0u, false);
    XLCDC_SetLayerEnable(layer, true, true);
}

void ScreenSplash_Hide(XLCDC_LAYER layer)
{
    XLCDC_SetLayerEnable(layer, false, true);
}

static void fill_fallback(void)
{
    for (uint32_t i = 0u; i < (BASE_W * BASE_H); i++) { s_fb[i] = SPLASH_FILL; }
}

bool ScreenSplash_Load(void)
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
