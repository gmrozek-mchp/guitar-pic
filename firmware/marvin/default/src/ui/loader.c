#include "ui/loader.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "definitions.h"
#include "app.h"
#include "log.h"
#include "storage/storage.h"
#include "video/video.h"
#include "ui/ui_manager.h"
#include "gfx/legato/renderer/legato_renderer.h"  /* leRenderer_IsIdle */

#define LOADER_TASK_STACK_WORDS  1024u
#define LOADER_TASK_PRIORITY     2u    /* UI band; blocks on SD I/O + the render wait */

#define SPLASH_REL_PATH    "/ui/splash.raw"
#define SPLASH_READ_CHUNK  (64u * 1024u)

#define SPLASH_MIN_MS              5000u  /* hold the splash at least this long */

/* Render-completion wait. leRenderer_IsIdle() is just frameState == READY, which
 * is also true in the gaps *between* leUpdate calls — and LEGATO_Tasks only ticks
 * every ~10 ms, so a full-screen paint spans several frames. A single idle sample
 * therefore reads "done" mid-paint. So require idle to hold continuously for a
 * window several leUpdate cycles long before trusting it. Bounded overall. */
#define RENDER_POLL_MS              5u
#define RENDER_IDLE_STABLE_MS     120u   /* idle must persist this long (>> ~10 ms tick) */
#define RENDER_IDLE_TIMEOUT_MS   8000u

/* Video window: 720×480 video at (280, 76) on the 1280×800 panel — 1:1 with the
 * bridge's typical 480p source, leaving a UI strip below. */
#define VIDEO_WIN_X   280u
#define VIDEO_WIN_Y    76u
#define VIDEO_WIN_W   720u
#define VIDEO_WIN_H   480u

static StackType_t  s_task_stack[LOADER_TASK_STACK_WORDS];
static StaticTask_t s_task_tcb;

/* Read the raw splash file straight into the OVR2 canvas buffer `dst` (capacity
 * `cap` bytes — the buffer OVR2 scans out). The file is raw RGBA8888 in the
 * layer's byte order, so the read IS the load: no decode, no blit. Returns false
 * on any failure (no card, missing file, wrong size, short read) — caller leaves
 * the pre-filled solid splash in place. */
static bool load_splash_into(uint8_t *dst, uint32_t cap)
{
    if (!Storage_Mount()) { return false; }

    /* Time the file access (open + read + close) separately from the mount,
     * which Storage_Mount already logs — so the boot log shows the mount-vs-read
     * split of the "mount + read" window. */
    TickType_t read_t0 = xTaskGetTickCount();

    char path[64];
    (void)snprintf(path, sizeof(path), "%s%s", Storage_MountPoint(), SPLASH_REL_PATH);

    SYS_FS_HANDLE h = SYS_FS_FileOpen(path, SYS_FS_FILE_OPEN_READ);
    if (h == SYS_FS_HANDLE_INVALID)
    {
        LOG_WARN("LOADER: no splash %s (fs err %d)\r\n", path, (int)SYS_FS_Error());
        return false;
    }

    /* Raw is fixed-size: anything else is the wrong dimensions/format and would
     * render as garbage, so require an exact match to the canvas buffer. */
    int32_t sz = SYS_FS_FileSize(h);
    if (sz != (int32_t)cap)
    {
        LOG_WARN("LOADER: splash size %ld != expected %u\r\n", (long)sz, (unsigned)cap);
        (void)SYS_FS_FileClose(h);
        return false;
    }

    uint32_t total = 0u;
    bool err = false;
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
        LOG_WARN("LOADER: splash read short %lu/%ld\r\n", (unsigned long)total, (long)sz);
        return false;
    }
    uint32_t read_ms = (uint32_t)((xTaskGetTickCount() - read_t0) * portTICK_PERIOD_MS);
    LOG_INFO("LOADER: splash %lu B read in %lu ms\r\n", (unsigned long)total, (unsigned long)read_ms);
    return true;
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

    /* UiManager_Initialize set up the splash canvas (OVR2, pre-filled solid) and
     * built the dashboard + nav pre-scheduler but left them detached. Sequence:
     * read the raw splash straight into the OVR2 buffer, light the panel, then
     * attach the dashboard + nav so they paint behind the splash while it's held,
     * then reveal. */

    /* Mount the card and read the raw splash directly into the OVR2 canvas buffer
     * — no decode, no blit, the read is the load. Storage_Mount polls until the
     * SDMMC driver finishes card analysis (and logs how long), so this is one
     * bounded attempt. On failure the pre-filled solid splash stays. */
    uint32_t cap = 0u;
    uint8_t *fb  = (uint8_t *)UiManager_SplashFramebuffer(&cap);
    if (load_splash_into(fb, cap))
    {
        UiManager_CommitSplash();
    }
    else
    {
        LOG_WARN("LOADER: solid-fill splash (no image)\r\n");
    }

    /* The splash is a raw framebuffer OVR2 scans out directly — no render to wait
     * on. Light the panel immediately. */
    UiManager_EnableBacklight();
    TickType_t shown_at = xTaskGetTickCount();

    /* Splash is up. Now pull the dashboard + nav into the render path; they paint
     * behind the opaque splash while it's held, off the splash-to-screen path. */
    UiManager_AttachMainScreens();

    /* --- Asset pre-load goes here (album art → DDR cache, etc.). None yet. --- */

    /* Wait for the dashboard to finish painting (bounded) so the reveal never
     * shows a partial frame, then hold the splash at least SPLASH_MIN_MS so a
     * fast paint doesn't flash it away. */
    wait_render_idle();
    TickType_t elapsed   = xTaskGetTickCount() - shown_at;
    TickType_t min_ticks = pdMS_TO_TICKS(SPLASH_MIN_MS);
    if (elapsed < min_ticks) { vTaskDelay(min_ticks - elapsed); }

    /* Drop the splash → the dashboard (painted behind it) is revealed, complete. */
    UiManager_RevealDashboard();

    /* Splash handoff done — bring up the deferred subsystems (video, detector,
     * actuator links, gameplay, console, perf drain). Held off until now so
     * their higher-priority tasks didn't preempt the SD mount + splash render. */
    App_StartServices();

    /* Camera go-live: these just set intent flags the (now-running) video task
     * reconciles, so the order relative to Video_Initialize above is benign. */
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
