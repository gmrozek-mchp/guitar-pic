#include "ui/ui_manager.h"
#include "ui/screens/nav/screen_nav.h"
#include "ui/screens/splash/splash.h"

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "definitions.h"   /* XLCDC_*, AC69T88A_BACKLIGHT_EN_Set */
#include "log.h"
#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/generated/le_gen_assets.h"
#include "gfx/legato/generated/screen/le_gen_screen_Dashboard.h"   /* screenInit/GetRoot/OnShow */
#include "gfx/legato/generated/screen/le_gen_screen_Navigation.h"
#include "gfx/legato/renderer/legato_renderer.h"                   /* leRenderer_IsIdle */

/* ui_manager is the UI orchestrator + compositor.
 *
 * Canvas vs. hardware layer: a canvas (CANVAS_* in ui_manager.h) is a RAM
 * surface a screen renders into; a hardware layer (HW_*) is what the LCDC scans
 * out. They are independent — ui_manager binds a canvas onto whatever HW layer
 * suits at the moment with gfxcSetLayer. A screen never owns a layer; the layer
 * is chosen here, at display time. Note gfxcSetLayer only takes effect while the
 * canvas is hidden, so layer binds always precede gfxcShowCanvas.
 *
 * Boot is two phases, run by the boot task once the scheduler is up:
 *   1. Splash — the splash module loads a raw RGBA8888 image off the SD straight
 *      into its canvas; we bind that canvas to SPLASH_HW_LAYER and light the
 *      backlight. No Legato. The splash-shown callback then lets the app start
 *      everything else in parallel.
 *   2. Screens — each MGS/Legato screen is built one at a time (they all build
 *      into Legato layer 0, so each is detached after init to free layer 0 for
 *      the next), then the active screens are bound to their display layers and
 *      paint behind the splash. After a minimum hold the splash is hidden,
 *      revealing the (warm) dashboard. */

#define SPLASH_MIN_MS   5000u   /* hold the splash at least this long */

/* Render-completion wait. leRenderer_IsIdle() is also true in the gaps between
 * leUpdate calls (LEGATO_Tasks ticks ~10 ms), so a single idle sample can read
 * "done" mid-paint. Require idle to hold continuously for a window several ticks
 * long before trusting it; bounded overall so a stall can't hang boot. */
#define RENDER_POLL_MS            5u
#define RENDER_IDLE_STABLE_MS   120u
#define RENDER_IDLE_TIMEOUT_MS 8000u

#define BOOT_TASK_STACK_WORDS  2048u
#define BOOT_TASK_PRIORITY        2u   /* UI band; blocks on SD I/O + the render wait */

#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))

/* Dashboard surface, non-cached so the 2D engine and LCDC DMA read CPU-rendered
 * pixels coherently. RGB565 — steady-state UI needs no more. (The nav owns its
 * own surface via Nav_InitSurface; the splash owns its own via Splash_InitSurface.) */
static uint16_t FB_NOCACHE s_fb_base[BASE_W * BASE_H];

static void (*s_splash_shown_cb)(void);

static StackType_t  s_boot_stack[BOOT_TASK_STACK_WORDS];
static StaticTask_t s_boot_tcb;

/* ── Legato screen registry ──────────────────────────────────────────────────
 * Each screen: the generated init (builds the tree into Legato layer 0), the
 * generated root getter, a host hook (attach the root onto the screen's own
 * canvas + content setup; layer-agnostic, leaves the canvas hidden), and the
 * canvas it renders into. Adding a screen is one row + one canvas surface. */
typedef struct
{
    leResult  (*init)(void);
    leWidget *(*get_root)(uint32_t);
    void      (*host)(void);
    uint32_t  canvas;
} ui_screen_t;

typedef enum { UI_SCREEN_DASHBOARD = 0, UI_SCREEN_NAV, UI_SCREEN_COUNT } ui_screen_id;

static const ui_screen_t s_screens[UI_SCREEN_COUNT] =
{
    [UI_SCREEN_DASHBOARD] = { screenInit_Dashboard,  screenGetRoot_Dashboard,  Dashboard_OnShow,  CANVAS_DASH },
    [UI_SCREEN_NAV]       = { screenInit_Navigation, screenGetRoot_Navigation, Navigation_OnShow, CANVAS_NAV  },
};

/* ── compositor primitives ───────────────────────────────────────────────── */

/* GFX-canvas drvLayer index → XLCDC layer enum (the XLCDC poke namespace). */
static XLCDC_LAYER xlcdc_layer(uint32_t hw)
{
    switch (hw)
    {
        case HW_BASE: return XLCDC_LAYER_BASE;
        case HW_OVR2: return XLCDC_LAYER_OVR2;
        case HW_OVR1:
        default:      return XLCDC_LAYER_OVR1;
    }
}

/* Bind a canvas to a hardware layer and optionally show it. The GFX-XLCDC driver
 * never writes RGBMODE itself, so we poke the layer's colour mode to match the
 * canvas placed on it — the layer always follows its current canvas. gfxcSetLayer
 * requires the canvas hidden, which it is until shown. */
static void bind_canvas(uint32_t canvas, uint32_t hw, XLCDC_RGB_COLOR_MODE mode, bool show)
{
    gfxcSetLayer(canvas, hw);
    if (show) { gfxcShowCanvas(canvas); }
    gfxcCanvasUpdate(canvas);
    XLCDC_SetLayerRGBColorMode(xlcdc_layer(hw), mode, true);
}

static void enable_backlight(void)
{
    /* The backlight is a plain GPIO (AC69T88A_BACKLIGHT_EN / PC18), NOT the LCDC
     * PWM that XLCDC_EnableBacklight() drives (that output isn't wired to this
     * board's backlight). The pin starts low at boot, so the panel stays dark
     * until here — no pre-splash frame. Active-high (the _Set name == enable). */
    AC69T88A_BACKLIGHT_EN_Set();
}

/* ── Legato render serialization ──────────────────────────────────────────────
 * leAddRootWidget/leRemoveRootWidget mutate Legato's per-layer root lists with no
 * locking, and LEGATO_Tasks + SYS_INPUT_Tasks (both run from scheduler start)
 * traverse those lists. The boot task edits the scene graph post-scheduler, so it
 * suspends those two readers around the edits. Safe to suspend here: at guard
 * time no app roots are attached (the splash is pure LCDC scanout), so the
 * renderer is idle; resuming hands rendering back. GFX_CANVAS_Tasks is left
 * running (it commits canvases; not a root-list reader). */
static TaskHandle_t s_legato_task;
static TaskHandle_t s_input_task;

static void scene_edit_begin(void)
{
    s_legato_task = xTaskGetHandle("LEGATO_Tasks");
    s_input_task  = xTaskGetHandle("SYS_INPUT_Tasks");
    if (s_legato_task != NULL) { vTaskSuspend(s_legato_task); }
    if (s_input_task  != NULL) { vTaskSuspend(s_input_task);  }
    if (s_legato_task == NULL || s_input_task == NULL)
    {
        LOG_WARN("UI: render task handle not found; scene edit unguarded\r\n");
    }
}

static void scene_edit_end(void)
{
    if (s_input_task  != NULL) { vTaskResume(s_input_task);  }
    if (s_legato_task != NULL) { vTaskResume(s_legato_task); }
}

/* Build every screen and leave them all detached. They each build into Legato
 * layer 0, so detach each before the next so layer 0 is free to reuse. */
static void init_screens(void)
{
    for (uint32_t i = 0u; i < UI_SCREEN_COUNT; i++)
    {
        (void)s_screens[i].init();
        leRemoveRootWidget(s_screens[i].get_root(0), 0);
    }
}

/* Block until the Legato render task has painted all pending damage. We don't
 * drive leUpdate — LEGATO_Tasks + GFX_CANVAS_Task render correctly when they run;
 * we just yield and poll the public idle flag, requiring it to hold continuously
 * (a lone idle sample is an inter-frame gap, not a finished paint). Bounded. */
static void wait_render_idle(void)
{
    TickType_t deadline   = xTaskGetTickCount() + pdMS_TO_TICKS(RENDER_IDLE_TIMEOUT_MS);
    TickType_t idle_since = 0;
    bool       idle_run   = false;

    while (xTaskGetTickCount() < deadline)
    {
        if (leRenderer_IsIdle())
        {
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
            idle_run = false;
        }
        vTaskDelay(pdMS_TO_TICKS(RENDER_POLL_MS));
    }
    LOG_WARN("UI: render-idle wait timed out\r\n");
}

/* ── boot sequence ───────────────────────────────────────────────────────── */

static void ui_boot_task(void *param)
{
    (void)param;

    /* PHASE 1 — splash, before any Legato work, as fast as the SD allows. */
    (void)Splash_Load();   /* loads the image, or fills the fallback colour */
    bind_canvas(Splash_CanvasId(), SPLASH_HW_LAYER, XLCDC_RGB_COLOR_MODE_RGBA_8888, true);
    enable_backlight();
    TickType_t shown_at = xTaskGetTickCount();

    /* Splash is up — let the app start everything else (services + camera) now,
     * in parallel with the screen painting below, so it's all warm at reveal. */
    if (s_splash_shown_cb != NULL) { s_splash_shown_cb(); }

    /* PHASE 2 — build + host the Legato screens behind the splash. Scene-graph
     * edits are guarded against the render tasks; the canvas/layer binds after
     * are GFX-canvas only (no root-list mutation) so they need no guard. */
    scene_edit_begin();
    init_screens();
    s_screens[UI_SCREEN_DASHBOARD].host();
    s_screens[UI_SCREEN_NAV].host();
    scene_edit_end();

    /* Dashboard live on BASE (covered by the splash until reveal). The nav is
     * hosted + painting into its own canvas, but not bound to a layer yet — it
     * shares OVR1 with the splash, so it takes OVR1 only after the splash vacates
     * it at reveal. */
    bind_canvas(CANVAS_DASH, HW_BASE, XLCDC_RGB_COLOR_MODE_RGB_565, true);

    wait_render_idle();

    /* Hold the splash a minimum time so a fast boot doesn't flash it away. */
    TickType_t elapsed   = xTaskGetTickCount() - shown_at;
    TickType_t min_ticks = pdMS_TO_TICKS(SPLASH_MIN_MS);
    if (elapsed < min_ticks) { vTaskDelay(min_ticks - elapsed); }

    /* REVEAL — cut over to the (painted) dashboard, then give the nav OVR1. */
    gfxcHideCanvas(Splash_CanvasId());
    gfxcCanvasUpdate(Splash_CanvasId());
    bind_canvas(CANVAS_NAV, HW_OVR1, XLCDC_RGB_COLOR_MODE_RGB_565, false);

    vTaskDelete(NULL);
}

/* ── screen host hooks ───────────────────────────────────────────────────────
 * Attach the screen's root onto its own canvas and set its window/content. No
 * layer bind, no show — the compositor decides the layer and visibility. (The nav
 * has its own host hook, Navigation_OnShow, in screen_nav.c.) */
void Dashboard_OnShow(void)
{
    leWidget *root = screenGetRoot_Dashboard(0);
    leAddRootWidget(root, CANVAS_DASH);
    gfxcSetWindowPosition(CANVAS_DASH, 0, 0);
    gfxcSetWindowSize(CANVAS_DASH, BASE_W, BASE_H);
    root->fn->invalidate(root);
}

/* ── public API ──────────────────────────────────────────────────────────── */

void UiManager_Initialize(void)
{
    /* Assign the per-screen canvas surfaces, then drive the canvas state machine
     * to RUNNING so a blit isn't dropped by the RUNNING gate, then set the Legato
     * string table the screens need. All pre-scheduler config — no screen built
     * or shown here; the boot task does the sequence. */
    gfxcSetPixelBuffer(CANVAS_DASH, BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565, s_fb_base);
    Splash_InitSurface();
    Nav_InitSurface();
    GFX_CANVAS_Task();

    leSetStringTable(&stringTable);
    initializeStrings();

    (void)xTaskCreateStatic(ui_boot_task, "UiBoot", BOOT_TASK_STACK_WORDS,
                            NULL, BOOT_TASK_PRIORITY, s_boot_stack, &s_boot_tcb);
}

void UiManager_SetSplashShownCallback(void (*cb)(void))
{
    s_splash_shown_cb = cb;
}
