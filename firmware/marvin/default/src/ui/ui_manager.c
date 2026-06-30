#include "ui/ui_manager.h"
#include "ui/screens/dashboard/screen_dashboard.h"
#include "ui/screens/navigation/screen_navigation.h"
#include "ui/screens/song_select/screen_song_select.h"
#include "ui/screens/album_art/screen_album_art.h"
#include "ui/screens/splash/screen_splash.h"

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "definitions.h"   /* XLCDC_*, PWM_* (backlight) */
#include "log.h"
#include "flash/settings.h"   /* persisted backlight % */
#include "game/art.h"         /* Art_LoadAll — cover-art preload during splash */
#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/generated/le_gen_assets.h"
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"      /* screenInit/GetRoot_Marvin */
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
 *   1. Splash — the splash module loads a raw RGBA8888 image from QSPI into its
 *      own framebuffer and drives SPLASH_HW_LAYER directly (ScreenSplash_Show; no canvas,
 *      no Legato), and we light the backlight. The splash-shown callback then lets
 *      the app start everything else in parallel.
 *   2. Screens — the single Marvin master screen is built (screenInit_Marvin),
 *      which places each layer-screen onto its own Legato layer/canvas (dashboard
 *      0, navigation 1, song-select 2); per-panel setup then wires events/content. The
 *      active canvases are bound to display layers and paint behind the splash.
 *      After a minimum hold the splash is hidden, revealing the (warm) dashboard. */

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


static void (*s_splash_shown_cb)(void);

static StackType_t  s_boot_stack[BOOT_TASK_STACK_WORDS];
static StaticTask_t s_boot_tcb;

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

/* ── song-select modal (OVR1 dialog + OVR2 cover strip) ───────────────────────
 * The dialog and its full-color cover are a coupled pair shown/hidden together.
 * Both canvases paint continuously into their surfaces; open binds them onto
 * OVR1/OVR2 and shows them, close hides them (freeing OVR1 for the nav drawer).
 * bind_canvas needs the canvas hidden for gfxcSetLayer — true while closed. */
static bool s_songsel_open = false;

/* Route input for the song-select modal via the layer-screens' background panels.
 * Legato treats every layer-screen as stacked and pickable regardless of canvas
 * visibility, so a hidden overlay still captures touches meant for the dashboard
 * beneath it unless its background panel is disabled (the MGS layer-screen
 * "background panel" pattern). PANEL_SONG_SELECT (layer 2) and
 * PANEL_SONG_SELECT_ALBUM_ART (layer 3) are the first pickable widgets on their
 * IGNOREPICK roots, i.e. the layers' background panels. `on` = dialog shown:
 * enable the dialog panels and *disable the dashboard* (PANEL_DASHBOARD, layer 0) so
 * it's a true modal — nothing behind the dialog (header tap, hamburger) reacts.
 * Inverted while closed: dashboard live, overlays pass input through. */
static void songsel_set_input(leBool on)
{
    Marvin_PANEL_DASHBOARD->fn->setEnabled(Marvin_PANEL_DASHBOARD, on ? LE_FALSE : LE_TRUE);
    Marvin_PANEL_SONG_SELECT->fn->setEnabled(Marvin_PANEL_SONG_SELECT, on);
    Marvin_PANEL_SONG_SELECT_ALBUM_ART->fn->setEnabled(Marvin_PANEL_SONG_SELECT_ALBUM_ART, on);
}

void UiManager_OpenSongSelect(void)
{
    if (s_songsel_open) { return; }
    songsel_set_input(LE_TRUE);
    bind_canvas(CANVAS_SONGSEL,   HW_OVR1, XLCDC_RGB_COLOR_MODE_RGB_565,   true);
    bind_canvas(CANVAS_ALBUM_ART, HW_OVR2, XLCDC_RGB_COLOR_MODE_RGBA_8888, true);
    s_songsel_open = true;
}

void UiManager_CloseSongSelect(void)
{
    if (!s_songsel_open) { return; }
    gfxcHideCanvas(CANVAS_SONGSEL);   gfxcCanvasUpdate(CANVAS_SONGSEL);
    gfxcHideCanvas(CANVAS_ALBUM_ART); gfxcCanvasUpdate(CANVAS_ALBUM_ART);
    songsel_set_input(LE_FALSE);
    s_songsel_open = false;
}

static uint32_t s_backlight_pct;

/* Set the backlight brightness (0–100%, clamped). The backlight is PWM-dimmed on
 * PC18 (PWM channel 0, configured by MCC), period read at runtime so this tracks
 * whatever MCC sets. The channel is CPOL_LOW — it idles low (dark) when stopped,
 * so the panel stays off until the splash — which means CDTY is the LOW-level
 * time and brightness is the high fraction: CDTY = period·(100−pct)/100, so
 * pct=100 → CDTY 0 → full bright, pct=0 → CDTY period → off. PWM_ChannelDutySet
 * writes CDTY directly while stopped and the update register once running, so
 * this is valid both before the channel starts and at runtime. */
void UiManager_SetBacklight(uint32_t pct)
{
    if (pct > 100u) { pct = 100u; }
    s_backlight_pct = pct;
    uint32_t period = PWM_ChannelPeriodGet(PWM_CHANNEL_0);
    PWM_ChannelDutySet(PWM_CHANNEL_0, (period * (100u - pct)) / 100u);
}

uint32_t UiManager_GetBacklight(void)
{
    return s_backlight_pct;
}

/* Light the backlight at the persisted brightness (Settings_Get loads the QSPI
 * ring on first use, or the compiled default if no record). The PWM channel is
 * stopped (no output) until started here, so the panel stays dark until the
 * splash is up — no pre-splash frame. PWM_Initialize ran at startup; this sets
 * the duty (while stopped, so the channel starts straight at the target) and
 * starts it. */
static void enable_backlight(void)
{
    UiManager_SetBacklight(Settings_Get()->backlight_pct);
    PWM_ChannelsStart(PWM_CHANNEL_0_MASK);
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

/* Build the Marvin master screen and run per-panel setup. screenInit_Marvin builds
 * all layer-screens and places each on its own Legato layer/canvas (dashboard 0,
 * navigation 1, song-select 2) — no re-hosting. Each panel module's *_Setup() then sets
 * its canvas window and wires its content/events. */
static void init_screens(void)
{
    /* screenInit_Marvin only flips the one-time "initialized" flag; the widget
     * tree (all three layers) is constructed in screenShow_Marvin, which also
     * attaches each layer's root to its Legato layer (leAddRootWidget rootN, N). */
    (void)screenInit_Marvin();
    (void)screenShow_Marvin();

    /* The album-art layer (3) must be RGBA8888: the 2D engine has no RGB888 support
     * (gfx2dFormats[RGB_888] = -1), so a 24bpp canvas can't be GFX2D-blitted and
     * renders garbage. MGS currently emits LE_COLOR_MODE_RGB_888 for this layer;
     * override it here to match the RGBA8888 canvas surface + OVR2 scanout. (Set the
     * layer-screen to RGBA8888 in MGS to make this override redundant.) */
    leSetLayerColorMode(CANVAS_ALBUM_ART, LE_COLOR_MODE_RGBA_8888);

    ScreenDashboard_Setup();
    ScreenNavigation_Setup();
    ScreenSongSelect_Setup();
    ScreenAlbumArt_Setup();

    /* Song-select starts closed: disable its layer-screens' background panels so
     * those (hidden) overlays don't capture touches meant for the dashboard.
     * UiManager_OpenSongSelect re-enables them. */
    songsel_set_input(LE_FALSE);
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
    (void)ScreenSplash_Load();   /* loads the image, or fills the fallback colour */
    ScreenSplash_Show(xlcdc_layer(SPLASH_HW_LAYER));
    enable_backlight();
    TickType_t shown_at = xTaskGetTickCount();

    /* Splash is up — let the app start everything else (services + camera) now,
     * in parallel with the screen painting below, so it's all warm at reveal. */
    if (s_splash_shown_cb != NULL) { s_splash_shown_cb(); }

    /* Decode all album art into the RGB888 caches *before* building the screens,
     * so the song-select panel's initial selection (ScreenSongSelect_Setup ->
     * song_detail_show(0)) finds its cover already cached. Storage mounts here;
     * Legato's image decoders are up from SYS_Initialize. All behind the splash;
     * the ~1-3 s decode just extends the splash hold. See game/art.h. */
    (void)Art_LoadAll();

    /* PHASE 2 — build the Marvin screen + per-panel setup behind the splash.
     * Scene-graph edits (screenInit_Marvin's leAddRootWidget calls) are guarded
     * against the render tasks; the canvas/layer binds after are GFX-canvas only
     * (no root-list mutation) so they need no guard. */
    scene_edit_begin();
    init_screens();
    scene_edit_end();

    /* Dashboard live on BASE (covered by the splash until reveal). Nav and
     * song-select are built and painting into their own canvases but not bound to
     * a layer yet — they take their layers at reveal, once the splash vacates. */
    bind_canvas(CANVAS_DASH, HW_BASE, XLCDC_RGB_COLOR_MODE_RGB_565, true);

    wait_render_idle();

    /* Hold the splash a minimum time so a fast boot doesn't flash it away. */
    TickType_t elapsed   = xTaskGetTickCount() - shown_at;
    TickType_t min_ticks = pdMS_TO_TICKS(SPLASH_MIN_MS);
    if (elapsed < min_ticks) { vTaskDelay(min_ticks - elapsed); }

    /* REVEAL — cut over to the (painted) dashboard. The song-select dialog starts
     * closed: it and its OVR2 cover strip are painted into their canvases but not
     * shown; UiManager_OpenSongSelect() binds + shows the pair on demand (header tap).
     * Set OVR1's colour mode to RGB565 now so the first nav-open (nav shares OVR1 and
     * doesn't poke RGBMODE itself) scans out correctly even before any dialog open. */
    ScreenSplash_Hide(xlcdc_layer(SPLASH_HW_LAYER));
    XLCDC_SetLayerRGBColorMode(xlcdc_layer(HW_OVR1), XLCDC_RGB_COLOR_MODE_RGB_565, true);

    vTaskDelete(NULL);
}

/* ── public API ──────────────────────────────────────────────────────────── */

void UiManager_Initialize(void)
{
    /* Assign the per-screen canvas surfaces, then drive the canvas state machine
     * to RUNNING so a blit isn't dropped by the RUNNING gate, then set the Legato
     * string table the screens need. All pre-scheduler config — no screen built
     * or shown here; the boot task does the sequence. */
    ScreenDashboard_InitSurface();
    ScreenNavigation_InitSurface();
    ScreenSongSelect_InitSurface();
    ScreenAlbumArt_InitSurface();
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
