#include "ui/loader.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "definitions.h"
#include "log.h"
#include "storage/storage.h"
#include "video/video.h"
#include "ui/ui_manager.h"
#include "ui/screens/splash/screen_splash.h"
#include "gfx/legato/renderer/legato_renderer.h"  /* leRenderer_IsIdle */

#define LOADER_TASK_STACK_WORDS  1024u
#define LOADER_TASK_PRIORITY     2u    /* UI band; blocks on SD I/O + the render wait */

#define SPLASH_REL_PATH    "/ui/splash.jpg"
#define SPLASH_JPEG_MAX    (1024u * 1024u)   /* cap; skip splash if file is larger */
#define SPLASH_READ_CHUNK  (64u * 1024u)

#define SPLASH_MIN_MS              1200u  /* hold the splash at least this long */

/* Render-completion wait. leRenderer_IsIdle() is just frameState == READY, which
 * is also true in the gaps *between* leUpdate calls — and LEGATO_Tasks only ticks
 * every ~10 ms, so a full-screen paint spans several frames. A single idle sample
 * therefore reads "done" mid-paint. So require idle to hold continuously for a
 * window several leUpdate cycles long before trusting it. Bounded overall. */
#define RENDER_POLL_MS              5u
#define RENDER_IDLE_STABLE_MS     120u   /* idle must persist this long (>> ~10 ms tick) */
#define RENDER_IDLE_TIMEOUT_MS   8000u

/* SD readiness: the SDMMC card-detect/analysis isn't done this early in boot, so
 * the first mount can fail (FR_NOT_READY). Retry the whole load a few times. */
#define SD_LOAD_ATTEMPTS            5u
#define SD_RETRY_MS               300u

/* Video window: 720×480 video at (280, 76) on the 1280×800 panel — 1:1 with the
 * bridge's typical 480p source, leaving a UI strip below. */
#define VIDEO_WIN_X   280u
#define VIDEO_WIN_Y    76u
#define VIDEO_WIN_W   720u
#define VIDEO_WIN_H   480u

/* Compressed splash JPEG, read from SD. Static (off-stack); the leImage handed
 * to Legato points straight at it, so it must stay resident while the splash is
 * shown. Cache-line aligned for the FatFs multi-block read fast path. */
static uint8_t s_jpeg[SPLASH_JPEG_MAX] __ALIGNED(CACHE_LINE_SIZE);

static StackType_t  s_task_stack[LOADER_TASK_STACK_WORDS];
static StaticTask_t s_task_tcb;

/* Read the splash file into s_jpeg. Returns byte count, or 0 on any failure
 * (no card, missing file, too large, short read) — caller falls back to the
 * splash's solid fill. */
static uint32_t load_splash_file(void)
{
    if (!Storage_Mount()) { return 0u; }

    char path[64];
    (void)snprintf(path, sizeof(path), "%s%s", Storage_MountPoint(), SPLASH_REL_PATH);

    SYS_FS_HANDLE h = SYS_FS_FileOpen(path, SYS_FS_FILE_OPEN_READ);
    if (h == SYS_FS_HANDLE_INVALID)
    {
        LOG_WARN("LOADER: no splash %s (fs err %d)\r\n", path, (int)SYS_FS_Error());
        return 0u;
    }

    int32_t sz = SYS_FS_FileSize(h);
    if (sz <= 0 || (uint32_t)sz > SPLASH_JPEG_MAX)
    {
        LOG_WARN("LOADER: splash size %ld invalid (cap %u)\r\n",
                 (long)sz, (unsigned)SPLASH_JPEG_MAX);
        (void)SYS_FS_FileClose(h);
        return 0u;
    }

    uint32_t total = 0u;
    bool err = false;
    while (total < (uint32_t)sz)
    {
        size_t want = (size_t)((uint32_t)sz - total);
        if (want > SPLASH_READ_CHUNK) { want = SPLASH_READ_CHUNK; }

        size_t got = SYS_FS_FileRead(h, &s_jpeg[total], want);
        if (got == 0u || got == (size_t)-1) { err = true; break; }
        total += (uint32_t)got;
    }
    (void)SYS_FS_FileClose(h);

    if (err || total != (uint32_t)sz)
    {
        LOG_WARN("LOADER: splash read short %lu/%ld\r\n", (unsigned long)total, (long)sz);
        return 0u;
    }
    LOG_INFO("LOADER: splash %lu B loaded\r\n", (unsigned long)total);
    return total;
}

/* Block until the Legato render task has fully painted all pending damage.
 *
 * We do NOT drive leUpdate ourselves — the normal LEGATO_Tasks + GFX_CANVAS_Task
 * pair renders the full screen correctly (a full-screen surface spans several
 * scratch tiles, and the scratch is freed by the canvas commit between render
 * passes; that only happens when those tasks actually run). So the loader just
 * yields and polls the public idle flag. First wait for the task to pick up the
 * queued damage (go busy), then wait for it to finish — both bounded so a render
 * stall can't hang boot. */
static void wait_render_idle(void)
{
    TickType_t  deadline   = xTaskGetTickCount() + pdMS_TO_TICKS(RENDER_IDLE_TIMEOUT_MS);
    TickType_t  idle_since = 0;
    bool        idle_run   = false;

    while (xTaskGetTickCount() < deadline)
    {
        if (leRenderer_IsIdle())
        {
            /* Idle must hold continuously — a lone idle sample is just an
             * inter-frame gap, not a finished paint. */
            if (!idle_run)
            {
                idle_run   = true;
                idle_since = xTaskGetTickCount();
            }
            else if ((xTaskGetTickCount() - idle_since) >= pdMS_TO_TICKS(RENDER_IDLE_STABLE_MS))
            {
                return;
            }
        }
        else
        {
            idle_run = false;   /* a frame started → reset the streak */
        }
        vTaskDelay(pdMS_TO_TICKS(RENDER_POLL_MS));
    }
    LOG_WARN("LOADER: render-idle wait timed out\r\n");
}

static void loader_task(void *param)
{
    (void)param;

    /* The splash, dashboard, and nav were built on their own canvases by
     * UiManager_Initialize (pre-scheduler); the Legato render task is now
     * painting them (backlight still off). Load the splash image, wait for the
     * render to finish, then light the panel — the first lit frame is the
     * fully-painted splash, with the dashboard finished behind it on BASE. No
     * image (no card / too large) → the panel's solid fill is the fallback. */
    uint32_t len = 0u;
    for (uint32_t a = 0u; (a < SD_LOAD_ATTEMPTS) && (len == 0u); a++)
    {
        if (a > 0u) { vTaskDelay(pdMS_TO_TICKS(SD_RETRY_MS)); }
        len = load_splash_file();
    }
    if (!((len > 0u) && Splash_SetImageJpeg(s_jpeg, len)))
    {
        LOG_WARN("LOADER: solid-fill splash (no image)\r\n");
    }

    wait_render_idle();
    UiManager_EnableBacklight();
    TickType_t shown_at = xTaskGetTickCount();

    /* --- Asset pre-load goes here (album art → DDR cache, etc.). The 32bpp
     * splash buffer (ui_manager) is free to reuse as decode scratch once the
     * splash leaves the screen. None yet. --- */

    /* Hold the splash a minimum time so a fast boot doesn't flash it away. */
    TickType_t elapsed   = xTaskGetTickCount() - shown_at;
    TickType_t min_ticks = pdMS_TO_TICKS(SPLASH_MIN_MS);
    if (elapsed < min_ticks) { vTaskDelay(min_ticks - elapsed); }

    /* Drop the splash → the dashboard (painted behind it) is revealed, complete.
     * Then bring the camera up. */
    UiManager_RevealDashboard();
    Video_SetWindow(VIDEO_WIN_X, VIDEO_WIN_Y, VIDEO_WIN_W, VIDEO_WIN_H);
    Video_CaptureEnable();
    Video_DisplayShow();

    vTaskDelete(NULL);
}

void Loader_Start(void)
{
    (void)xTaskCreateStatic(loader_task, "LoaderTask", LOADER_TASK_STACK_WORDS,
                            NULL, LOADER_TASK_PRIORITY, s_task_stack, &s_task_tcb);
}
