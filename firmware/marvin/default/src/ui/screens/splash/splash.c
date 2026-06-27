#include "ui/screens/splash/splash.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "definitions.h"            /* SYS_FS_* */
#include "log.h"
#include "storage/storage.h"
#include "gfx/canvas/gfx_canvas_api.h"
#include "ui/ui_manager.h"          /* CANVAS_SPLASH, BASE_W, BASE_H */

#define SPLASH_REL_PATH    "/ui/splash.raw"
#define SPLASH_READ_CHUNK  (64u * 1024u)

/* Fallback fill: opaque black. The canvas is RGBA_8888 (packs 0xRRGGBBAA), so a
 * little-endian pixel word of 0x000000FF is R=G=B=0, A=0xFF. */
#define SPLASH_FILL        0x000000FFu

/* The splash pixels, also the buffer the splash layer scans out. Non-cached so
 * the SD DMA-in and the LCDC scan-out are coherent without cache maintenance
 * (the SD read is DMA-to-DRAM either way, so nocache costs nothing); 32-byte
 * aligned for the FatFs multi-block read fast path. */
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
    if (!Storage_Mount()) { fill_fallback(); return false; }

    char path[64];
    (void)snprintf(path, sizeof(path), "%s%s", Storage_MountPoint(), SPLASH_REL_PATH);

    SYS_FS_HANDLE h = SYS_FS_FileOpen(path, SYS_FS_FILE_OPEN_READ);
    if (h == SYS_FS_HANDLE_INVALID)
    {
        LOG_WARN("SPLASH: no image %s (fs err %d)\r\n", path, (int)SYS_FS_Error());
        fill_fallback();
        return false;
    }

    /* Raw is fixed-size: any other size is the wrong dimensions/format and would
     * render as garbage, so require an exact match to the canvas buffer. */
    int32_t sz = SYS_FS_FileSize(h);
    if (sz != (int32_t)sizeof(s_fb))
    {
        LOG_WARN("SPLASH: size %ld != expected %u\r\n", (long)sz, (unsigned)sizeof(s_fb));
        (void)SYS_FS_FileClose(h);
        fill_fallback();
        return false;
    }

    TickType_t t0 = xTaskGetTickCount();
    uint8_t   *dst = (uint8_t *)s_fb;
    uint32_t   total = 0u;
    bool       err = false;
    while (total < (uint32_t)sz)
    {
        size_t want = (size_t)((uint32_t)sz - total);
        if (want > SPLASH_READ_CHUNK) { want = SPLASH_READ_CHUNK; }

        size_t got = SYS_FS_FileRead(h, &dst[total], want);
        if (got == 0u || got == (size_t)-1) { err = true; break; }
        total += (uint32_t)got;
    }
    (void)SYS_FS_FileClose(h);

    if (err || total != (uint32_t)sz)
    {
        LOG_WARN("SPLASH: read short %lu/%ld\r\n", (unsigned long)total, (long)sz);
        fill_fallback();
        return false;
    }

    uint32_t ms = (uint32_t)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS);
    LOG_INFO("SPLASH: %lu B read in %lu ms\r\n", (unsigned long)total, (unsigned long)ms);
    return true;
}
