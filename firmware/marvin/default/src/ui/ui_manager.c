#include "ui/ui_manager.h"
#include "ui/screens/dashboard/screen_dashboard.h"
#include "ui/screens/navigation/screen_navigation.h"
#include "ui/screens/song_select/screen_song_select.h"
#include "ui/screens/album_art/screen_album_art.h"
#include "ui/screens/splash/screen_splash.h"
#include "ui/screens/splash/splash_progress.h"
#include "ui/screens/video/screen_video.h"
#include "ui/screens/wiimotes/screen_wiimotes.h"
#include "ui/screens/keyboard/screen_keyboard.h"
#include "ui/screens/bus/screen_bus.h"
#include "ui/screens/system/screen_system.h"
#include "ui/dashboard_feed.h"

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "definitions.h"   /* XLCDC_*, PWM_* (backlight) */
#include "log.h"
#include "flash/settings.h"   /* persisted backlight % */
#include "game/game_art.h"    /* GameArt_LoadAll — cover-art preload during splash */
#include "game/node_art.h"    /* NodeArt_LoadAll — board-photo preload during splash */
#include "storage/storage.h"  /* Storage_Mount — mounted explicitly during the splash */
#include "health/health_monitor.h"  /* armed at end of boot (HealthMonitor_NotifyReady) */
#include "video/video.h"      /* capture producer — compositor owns HEO display */
#include "ui/gfx/aa_corners.h"     /* rounded modal corners against the base view */
#include "ui/gfx/ui_surface.h"     /* the modal + base canvas surfaces to sample */
#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/core/legato_scheme.h"
#include "gfx/legato/generated/le_gen_scheme.h"
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

#define SPLASH_MIN_MS   5000u   /* hold the splash at least this long   */
#define SPLASH_FADE_MS   300u   /* cross-dissolve into the dashboard    */

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

/* ── HEO video layer (compositor-owned display of the capture producer) ───────
 * ui_manager owns the HEO hardware layer; video.c is the capture producer that
 * notifies us via two callbacks registered in UiManager_Initialize:
 *   heo_reconcile()   — video-task ctx, each tick: (re)bind/unbind HEO to match our
 *                       show intent + the current source size.
 *   heo_frame_latch() — ISC IRQ ctx: re-point HEO at the freshest ring slot.
 * Every HEO/BASE register write lives here but executes in those (video task / IRQ)
 * contexts, so the UI task only sets volatile intent (UiManager_VideoShow/Hide) and
 * HEO/BASE stay single-writer. HEO is free for other uses whenever video is hidden. */

typedef struct { uint32_t x, y, w, h; } rect_t;

static volatile bool   s_video_shown;               /* intent (UI task)              */
static volatile bool   s_video_rebind;              /* window changed → rebind        */
static volatile rect_t s_video_win;                 /* dst rect (UI task writes)      */
static volatile rect_t s_modal_disc;                /* discard BASE behind modal (UI)  */
static volatile uint8_t s_scrim_alpha;              /* modal dim 0..255 intent (UI)   */
static bool            s_video_bound;               /* HEO bound (video-task only)    */
static uint8_t         s_scrim_bound;               /* scrim ADEF programmed (task)   */
static uint16_t        s_bound_src_w, s_bound_src_h;/* full source HEO is bound at (task) */
static uint16_t        s_bound_aw, s_bound_ah;      /* active crop size HEO is bound at */
static uint16_t        s_bound_ax, s_bound_ay;      /* active crop offset HEO is bound at */

/* Byte offset from a ring-slot base to the active picture's top-left, applied to
 * every HEO frame address. Set by heo_bind (video task), added by heo_frame_latch
 * (ISC IRQ) — single-writer/single-reader of one aligned word. */
static volatile uint32_t s_heo_crop_off;

/* HEO scaler factor is 12.20 fixed-point: factor = (src / dst) << 20; 1.0 = 1:1. */
static uint32_t scaler_factor(uint32_t src, uint32_t dst)
{
    if (dst == 0u) { return 0x100000u; }
    return (uint32_t)(((uint64_t)src << 20) / dst);
}

/* ── HEO video levels expansion (per-component gamma CLUT) ─────────────────────
 * The captured source is mildly range-compressed (~5 black pedestal, ~233 white
 * ceiling) against the panel's full 0..255. The HEO CBHS limited-range block is
 * YCbCr-only, but the per-component gamma CLUT operates on true RGB, so we load it
 * with a linear levels-expansion curve (BLACK→0, WHITE→255) and enable GAM. This is
 * display-only — the capture in DDR is untouched, so the detector/gameplay see raw
 * pixels — and free at runtime (applied by the LCDC at scanout).
 *
 * The CLUT must be written while CLUTEN and GAM are clear (true at init, after MCC's
 * XLCDC_SetupHEOLayer); XLCDC_SetLayerRGBColorMode rewrites HEOCFG1 with GAM(0) on
 * every bind, so heo_bind re-asserts GAM after it. Fixed curve — retune VIDEO_LEVELS_*
 * for a different source. */
#define VIDEO_LEVELS_BLACK    5u    /* input level mapped to 0   */
#define VIDEO_LEVELS_WHITE  233u    /* input level mapped to 255 */

static volatile bool s_video_levels = true;   /* GAM (levels CLUT) on; `gamma` cmd toggles */

static void heo_gamma_load(void)
{
    const int32_t den = (int32_t)(VIDEO_LEVELS_WHITE - VIDEO_LEVELS_BLACK);
    for (uint32_t i = 0u; i < 256u; i++)
    {
        uint32_t c;
        if (i <= VIDEO_LEVELS_BLACK)
        {
            c = 0u;
        }
        else
        {
            int32_t v = ((int32_t)(i - VIDEO_LEVELS_BLACK) * 255 + den / 2) / den;
            c = (v > 255) ? 255u : (uint32_t)v;
        }
        XLCDC_REGS->LCDC_HEOCLUT[i] = LCDC_HEOCLUT_ACLUT(0xFFu) |
                                      LCDC_HEOCLUT_RCLUT(c) |
                                      LCDC_HEOCLUT_GCLUT(c) |
                                      LCDC_HEOCLUT_BCLUT(c);
    }
}

/* Bind HEO to the capture buffer at the window, engaging the bicubic scaler only
 * when dst != the active source size. The scaler reads only the detected active
 * picture rect (Video_GetActiveRect) — cropping the source's dead black bars — by
 * offsetting the frame base to the active top-left, sizing HEOCFG4 to the active
 * w/h, and skipping the cropped columns each line via XSTRIDE. Full-frame until
 * detection locks (safe fallback). The matching BASE DMA-discard behind the opaque
 * video is set by base_discard_reconcile (single DISCEN owner). Seeds HEO with the
 * latest frame (0 until the first frame, which heo_frame_latch then fixes within a
 * frame-time). Video-task ctx. */
static void heo_bind(uint16_t src_w, uint16_t src_h,
                     uint32_t x, uint32_t y, uint32_t dst_w, uint32_t dst_h)
{
    if (x + dst_w > BASE_W || y + dst_h > BASE_H)
    {
        LOG_WARN("UI: video window %lux%lu @(%lu,%lu) exceeds panel; skip bind\r\n",
                 (unsigned long)dst_w, (unsigned long)dst_h,
                 (unsigned long)x, (unsigned long)y);
        return;
    }

    Video_FrameInfo fi;
    Video_GetFrameInfo(&fi);

    /* Active picture crop within the full frame (full-frame fallback pre-lock). */
    uint16_t ax, ay, aw, ah;
    (void)Video_GetActiveRect(&ax, &ay, &aw, &ah);
    uint32_t bpp        = fi.bytes_per_pixel;
    uint32_t stride     = (uint32_t)src_w * bpp;                 /* full line bytes */
    uint32_t crop_off   = (uint32_t)ay * stride + (uint32_t)ax * bpp;
    uint32_t addr       = (uint32_t)(uintptr_t)fi.buffer + crop_off;

    XLCDC_SetLayerEnable(XLCDC_LAYER_HEO, false, true);
    XLCDC_SetLayerRGBColorMode(XLCDC_LAYER_HEO, XLCDC_RGB_COLOR_MODE_RGB_888_PACKED, false);
    /* Take the pixel stream from memory again. DRV_XLCDC_Initialize leaves HEO's
     * HEOCFG12.DMA set, so this used to be implicit — but heo_scrim_bind clears it, and
     * a video bind after a scrim would otherwise scan out the default colour instead of
     * the capture. Same call the scrim uses, with dma = true. */
    XLCDC_SetLayerOpts(XLCDC_LAYER_HEO, 255u, true, false);
    /* Re-apply the levels CLUT enable (RGBColorMode clears GAM); off = raw pass-through. */
    if (s_video_levels) { XLCDC_REGS->LCDC_HEOCFG1 |=  LCDC_HEOCFG1_GAM_Msk; }
    else                { XLCDC_REGS->LCDC_HEOCFG1 &= ~LCDC_HEOCFG1_GAM_Msk; }
    XLCDC_SetLayerAddress(XLCDC_LAYER_HEO, addr, false);
    XLCDC_SetLayerXStride(XLCDC_LAYER_HEO, (uint32_t)(src_w - aw) * bpp, false);
    XLCDC_SetLayerWindowXYPos(XLCDC_LAYER_HEO, x, y, false);

    /* Display rect (HEOCFG3, dst) vs source-memory rect (HEOCFG4, active crop):
     * equal for 1:1, differ when scaling. Written directly — XLCDC_SetLayerWindowXYSize
     * sets both equal. */
    XLCDC_REGS->LCDC_HEOCFG3 = LCDC_HEOCFG3_XSIZE(dst_w - 1u) | LCDC_HEOCFG3_YSIZE(dst_h - 1u);
    XLCDC_REGS->LCDC_HEOCFG4 = LCDC_HEOCFG4_XMEMSIZE(aw - 1u) | LCDC_HEOCFG4_YMEMSIZE(ah - 1u);

    bool scaling = (dst_w != aw) || (dst_h != ah);
    if (scaling)
    {
        uint32_t hf = scaler_factor(aw, dst_w);
        uint32_t vf = scaler_factor(ah, dst_h);

        XLCDC_REGS->LCDC_HEOCFG23 = LCDC_HEOCFG23_VXSYEN(1) | LCDC_HEOCFG23_VXSCEN(1)
                                  | LCDC_HEOCFG23_HXSYEN(1) | LCDC_HEOCFG23_HXSCEN(1);
        /* Bicubic luma + chroma, both planes (mirrors MCC's surface-set path). */
        XLCDC_REGS->LCDC_HEOCFG30 = LCDC_HEOCFG30_VXSYCFG(1) | LCDC_HEOCFG30_VXSYBICU(1)
                                  | LCDC_HEOCFG30_VXSCCFG(1) | LCDC_HEOCFG30_VXSCBICU(1);
        XLCDC_REGS->LCDC_HEOCFG31 = LCDC_HEOCFG31_HXSYCFG(1) | LCDC_HEOCFG31_HXSYBICU(1)
                                  | LCDC_HEOCFG31_HXSCCFG(1) | LCDC_HEOCFG31_HXSCBICU(1);
        XLCDC_REGS->LCDC_HEOCFG24 = LCDC_HEOCFG24_VXSYFACT(vf);
        XLCDC_REGS->LCDC_HEOCFG25 = LCDC_HEOCFG25_VXSCFACT(vf);
        XLCDC_REGS->LCDC_HEOCFG26 = LCDC_HEOCFG26_HXSYFACT(hf);
        XLCDC_REGS->LCDC_HEOCFG27 = LCDC_HEOCFG27_HXSCFACT(hf);
    }
    else
    {
        XLCDC_REGS->LCDC_HEOCFG23 = LCDC_HEOCFG23_VXSYEN(0) | LCDC_HEOCFG23_VXSCEN(0)
                                  | LCDC_HEOCFG23_HXSYEN(0) | LCDC_HEOCFG23_HXSCEN(0);
    }

    XLCDC_SetLayerEnable(XLCDC_LAYER_HEO, true, true);
    XLCDC_SetLayerEnable(XLCDC_LAYER_BASE, true, true);

    s_heo_crop_off = crop_off;
    s_bound_src_w = src_w; s_bound_src_h = src_h;
    s_bound_ax = ax; s_bound_ay = ay; s_bound_aw = aw; s_bound_ah = ah;
    LOG_INFO("UI: HEO bound src %ux%u active %ux%u@(%u,%u) -> dst %lux%lu @(%lu,%lu) %s\r\n",
             (unsigned)src_w, (unsigned)src_h, (unsigned)aw, (unsigned)ah, (unsigned)ax, (unsigned)ay,
             (unsigned long)dst_w, (unsigned long)dst_h,
             (unsigned long)x, (unsigned long)y, scaling ? "scaled" : "1:1");
}

/* Disable HEO output. The BASE discard is dropped (or handed to the dialog rect)
 * by base_discard_reconcile, which owns DISCEN. */
static void heo_unbind(void)
{
    XLCDC_SetLayerEnable(XLCDC_LAYER_HEO, false, true);
    XLCDC_SetLayerEnable(XLCDC_LAYER_BASE, true, true);
}

/* ── modal scrim: a full-screen dim on HEO with NO framebuffer ─────────────────
 * The mockup dims everything behind a modal (`bg-black/75`). Doing that with a
 * surface would cost a full-screen buffer and the DDR bandwidth to scan it every
 * frame — the contention that has knocked the CSI-2 D-PHY out of lock before. It
 * costs nothing instead, because of two things the LCDC gives us:
 *
 *  - `HEOCFG12.DMA = 0` makes the layer take its pixel from the DEFAULT COLOUR
 *    register rather than memory, and HEO's `HEOCFG9` carries a real ALPHA there
 *    (`ADEF`) as well as RGB. So the layer emits one constant ARGB pixel with no
 *    buffer, no DMA and no bandwidth at all. (OVR1/OVR2 have `ADEF` too, but the
 *    datasheet marks theirs "only for post-processing usage" — this is HEO's.)
 *  - MCC already configures HEO's blender as straight src-over on source alpha
 *    (`SFACTC = A0*As`, `DFACTC = 1-(A0*As)`, `A0 = 255`), so the composite is
 *    exactly `out = black*ADEF/255 + dst*(1 - ADEF/255)` — i.e. `bg-black/N`.
 *
 * HEO sits below OVR1 and above BASE (`VIDPRI = 0`), which is precisely where a
 * scrim belongs: it dims the base view while the modal on OVR1 stays full strength.
 * It is free whenever the video is hidden, which every modal already does.
 *
 * Video-task ctx, like every other HEO write. */
static void heo_scrim_bind(uint8_t alpha)
{
    XLCDC_SetLayerEnable(XLCDC_LAYER_HEO, false, true);

    XLCDC_REGS->LCDC_HEOCFG9 = LCDC_HEOCFG9_RDEF(0u) | LCDC_HEOCFG9_GDEF(0u)
                             | LCDC_HEOCFG9_BDEF(0u) | LCDC_HEOCFG9_ADEF(alpha);
    XLCDC_SetLayerOpts(XLCDC_LAYER_HEO, 255u, false, false);   /* DMA off, A0 = 255 */

    XLCDC_SetLayerWindowXYPos(XLCDC_LAYER_HEO, 0u, 0u, false);
    XLCDC_REGS->LCDC_HEOCFG3 = LCDC_HEOCFG3_XSIZE(BASE_W - 1u) | LCDC_HEOCFG3_YSIZE(BASE_H - 1u);
    XLCDC_REGS->LCDC_HEOCFG4 = LCDC_HEOCFG4_XMEMSIZE(BASE_W - 1u) | LCDC_HEOCFG4_YMEMSIZE(BASE_H - 1u);
    /* No scaling: a previous video bind may have left the scalers engaged, and there
     * is no pixel stream to scale. */
    XLCDC_REGS->LCDC_HEOCFG23 = 0u;

    XLCDC_SetLayerEnable(XLCDC_LAYER_HEO, true, true);
    LOG_INFO("UI: HEO scrim %u/255 full-screen (no framebuffer)\r\n", (unsigned)alpha);
}

/* ── BASE DMA-discard (single DISCEN owner, video-task ctx) ───────────────────
 * The LCDC has one BASE discard window (§44.6.4.7): BASE skips its DDR read where
 * an opaque layer fully covers it, freeing read bandwidth. base_discard_reconcile
 * picks the region each tick — the video rect while HEO is bound, else the dialog
 * rect while the song-select modal is open, else none — and applies only on change
 * so it isn't re-committing BASE every tick. Keeping every DISCEN write here (never
 * in heo_bind/heo_unbind or the UI task) keeps BASE single-writer. */
static struct { bool on; uint32_t x, y, w, h; } s_base_disc;

static void base_discard_apply(bool on, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (on == s_base_disc.on && x == s_base_disc.x && y == s_base_disc.y &&
        w == s_base_disc.w && h == s_base_disc.h) { return; }

    if (on)
    {
        XLCDC_REGS->LCDC_BASECFG5 = LCDC_BASECFG5_DISCXPOS(x) | LCDC_BASECFG5_DISCYPOS(y);
        XLCDC_REGS->LCDC_BASECFG6 = LCDC_BASECFG6_DISCXSIZE(w - 1u) | LCDC_BASECFG6_DISCYSIZE(h - 1u);
        XLCDC_REGS->LCDC_BASECFG4 |= LCDC_BASECFG4_DISCEN_Msk;
    }
    else
    {
        XLCDC_REGS->LCDC_BASECFG4 &= ~LCDC_BASECFG4_DISCEN_Msk;
    }
    XLCDC_SetLayerEnable(XLCDC_LAYER_BASE, true, true);   /* commit the CFG change */

    s_base_disc.on = on;
    s_base_disc.x = x; s_base_disc.y = y; s_base_disc.w = w; s_base_disc.h = h;
}

static void base_discard_reconcile(void)
{
    rect_t m = s_modal_disc;

    if (s_video_bound)
    {
        rect_t r = s_video_win;
        base_discard_apply(true, r.x, r.y, r.w, r.h);
    }
    else if (m.w != 0u && m.h != 0u)
    {
        base_discard_apply(true, m.x, m.y, m.w, m.h);
    }
    else
    {
        base_discard_apply(false, 0u, 0u, 0u, 0u);
    }
}

/* Mark/clear the region BASE may skip while an opaque modal covers it. The rect is
 * read from the modal's own CANVAS WINDOW rather than kept as constants here, so there
 * is one source of that geometry — a copy in this file had to be manually kept in sync
 * with the screen module that owns the layout. Any opaque full-canvas modal qualifies.
 * UI-task ctx; the video task applies it (single DISCEN owner). */
static void modal_discard_set(unsigned int canvas)
{
    int          x = 0, y = 0;
    unsigned int w = 0u, h = 0u;
    rect_t       r = { 0u, 0u, 0u, 0u };

    if (gfxcGetWindowPosition(canvas, &x, &y) == GFX_SUCCESS &&
        gfxcGetWindowSize(canvas, &w, &h) == GFX_SUCCESS &&
        x >= 0 && y >= 0 && w != 0u && h != 0u)
    {
        r.x = (uint32_t)x; r.y = (uint32_t)y; r.w = w; r.h = h;
    }
    s_modal_disc = r;
}

static void modal_discard_clear(void)
{
    rect_t none = { 0u, 0u, 0u, 0u };
    s_modal_disc = none;
}

/* video-task tick: converge HEO to intent + source. Bind on first show, window
 * change (rebind flag), source-size change, or active-crop change (detection
 * locking flips the source from full-frame to the active rect); unbind when
 * hidden — and when hidden, HEO is free for the modal scrim, so the layer has
 * three states here: video / scrim / off, in that priority. */
static void heo_reconcile(bool source_valid, uint16_t src_w, uint16_t src_h)
{
    rect_t w = s_video_win;
    bool   win_valid = (w.w != 0u) && (w.h != 0u);
    bool   want = s_video_shown && source_valid && win_valid;

    uint16_t ax, ay, aw, ah;
    (void)Video_GetActiveRect(&ax, &ay, &aw, &ah);
    bool geom_changed = src_w != s_bound_src_w || src_h != s_bound_src_h ||
                        aw != s_bound_aw || ah != s_bound_ah ||
                        ax != s_bound_ax || ay != s_bound_ay;

    if (want && (!s_video_bound || s_video_rebind || geom_changed))
    {
        heo_bind(src_w, src_h, w.x, w.y, w.w, w.h);
        s_video_bound  = true;
        s_video_rebind = false;
        s_scrim_bound  = 0u;   /* heo_bind re-took DMA; any scrim is gone */
    }
    else if (!want)
    {
        uint8_t alpha = s_scrim_alpha;   /* UI-task intent */

        /* Act only when leaving a video bind or when the dim level changes, so the
         * steady state costs no register writes. heo_scrim_bind reconfigures the layer
         * wholesale, so it replaces a video bind directly — no unbind first, which
         * matters because every `update` busy-waits a vsync. */
        if (s_video_bound || alpha != s_scrim_bound)
        {
            if (alpha != 0u) { heo_scrim_bind(alpha); }
            else             { heo_unbind(); }
            s_video_bound = false;
            s_scrim_bound = alpha;
        }
    }

    /* Own the single BASE discard window (video rect / dialog rect / none). Runs
     * after the HEO decision above so it sees the current s_video_bound. */
    base_discard_reconcile();
}

/* ISC IRQ: point HEO at the just-completed ring slot every frame so it always scans
 * the freshest complete frame (not a slot the capture engine is mid-write on). This
 * is unconditional, and safe in both of the states where video is not showing:
 * disabled (nothing scans the address) and scrimmed (HEOCFG12.DMA is 0, so the layer
 * takes its pixel from the default-colour register and never reads the address).
 * Keeping the write out of any task/IRQ-shared flag guarantees the scanout base
 * advances regardless of task timing. Latches at the next vsync. */
static void heo_frame_latch(uint32_t buffer_addr)
{
    /* Non-blocking base update: write the HEO frame address and *request* the layer
     * attribute update, but do NOT wait for the sync. XLCDC_SetLayerAddress(...,true)
     * → XLCDC_UpdateLayerAttributes busy-waits on LCDC_ATTRE/ATTRS_SIP until the update
     * latches (a vsync away while HEO scans) — too costly for this per-frame ISC IRQ.
     * The address register is double-buffered, so the hardware latches the new base at
     * the next vsync on its own; the following frame writes the newer address. OR into
     * ATTRE so a pending BASE/OVR update isn't cleared. The crop offset (0 until
     * the active area is detected) points HEO at the active top-left of the slot. */
    XLCDC_REGS->LCDC_HEO[0].LCDC_HEOYFBA = buffer_addr + s_heo_crop_off;
    XLCDC_REGS->LCDC_ATTRE |= LCDC_ATTRE_HEO_Msk;
}

void UiManager_VideoShow(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    rect_t r = { x, y, w, h };
    s_video_win    = r;
    s_video_rebind = true;
    s_video_shown  = true;
}

void UiManager_VideoHide(void)
{
    s_video_shown = false;
}

/* ── rounded modal corners ────────────────────────────────────────────────────
 * A modal canvas is OPAQUE RGB565, so its rounded corners cannot be transparent —
 * there is no per-pixel alpha to reveal the layer below. Instead each corner box is
 * filled with the pixels the BASE view has at the same screen position, dimmed to match
 * the scrim, and anti-aliased against the modal's own fill and 1px border. For as long
 * as the modal is up that is indistinguishable from transparency, because what sits
 * behind those four boxes is static page/card background.
 *
 * This lives here rather than in a screen module because every input it needs is
 * compositor knowledge: which canvas is the base view, where the modal's window sits,
 * and how dim the scrim is. Screens just ask, and both modals share one implementation.
 *
 * Snapshot semantics: run on open, not per frame, so live content moving under a corner
 * would go stale. Anything that repaints over a corner also undoes it — see
 * ui_compositor.md §18. */
typedef struct { const uint16_t *px; uint32_t stride; int x0, y0; } base_sampler_t;

/* Scale an RGB565 pixel by (100 - MODAL_SCRIM_PCT)%, per channel at its own depth: the
 * corner holds a COPY of the base view, but what the panel shows around it is the base
 * view as dimmed by the scrim, so an undimmed copy reads as bright notches. Same
 * arithmetic the LCDC blender does for the rest of the screen. */
static uint16_t scrim_dim565(uint16_t px)
{
    uint32_t keep = 100u - MODAL_SCRIM_PCT;
    uint32_t r    = (((uint32_t)px >> 11) & 0x1Fu) * keep / 100u;
    uint32_t g    = (((uint32_t)px >>  5) & 0x3Fu) * keep / 100u;
    uint32_t b    = ( (uint32_t)px        & 0x1Fu) * keep / 100u;

    return (uint16_t)((r << 11) | (g << 5) | b);
}

static leColor base_sample(void *ctx, int32_t x, int32_t y)
{
    const base_sampler_t *s = (const base_sampler_t *)ctx;

    return (leColor)scrim_dim565(s->px[(uint32_t)(s->y0 + y) * s->stride + (uint32_t)(s->x0 + x)]);
}

void UiManager_CutModalCorners(unsigned int canvas)
{
    const leScheme   *scheme    = &SCHEME_FILL_ZINC_900;   /* every modal's card colour */
    const void       *modal_buf = NULL;
    const void       *base_buf  = NULL;
    uint16_t          mw = 0u, mh = 0u, bw = 0u, bh = 0u;
    GFXC_COLOR_FORMAT mmode = GFX_COLOR_MODE_RGB_565;
    GFXC_COLOR_FORMAT bmode = GFX_COLOR_MODE_RGB_565;
    base_sampler_t    ctx;
    int               x = 0, y = 0;
    leRect            rect;

    /* Both surfaces must be RGB565 for the copy to be a copy; if either isn't, leave the
     * corners square rather than write converted garbage. */
    if (!UiSurface_Get(canvas, &modal_buf, &mw, &mh, &mmode))            { return; }
    if (!UiSurface_Get(UiManager_BaseCanvas(), &base_buf, &bw, &bh, &bmode)) { return; }
    if (modal_buf == NULL || base_buf == NULL)                            { return; }
    if (mmode != GFX_COLOR_MODE_RGB_565 || bmode != GFX_COLOR_MODE_RGB_565) { return; }

    /* Where the modal sits on the panel — its canvas window, the same single source of
     * that geometry modal_discard_set uses. */
    if (gfxcGetWindowPosition(canvas, &x, &y) != GFX_SUCCESS) { return; }
    if (x < 0 || y < 0)                                       { return; }
    if ((uint32_t)x + mw > bw || (uint32_t)y + mh > bh)        { return; }

    ctx.px     = (const uint16_t *)base_buf;
    ctx.stride = bw;
    ctx.x0     = x;
    ctx.y0     = y;

    rect.x = 0; rect.y = 0; rect.width = (int16_t)mw; rect.height = (int16_t)mh;

    AaCorners_RenderSurface565((uint16_t *)modal_buf, mw, &rect, MODAL_R, 1u,
                               leScheme_GetColor(scheme, LE_SCHM_BASE, LE_COLOR_MODE_RGB_565),
                               leScheme_GetColor(scheme, LE_SCHM_SHADOWDARK, LE_COLOR_MODE_RGB_565),
                               base_sample, &ctx);
}

/* Intent only, like the video verbs: the video task's heo_reconcile applies it, so
 * HEO stays single-writer. Takes effect on the next tick — the same latency as the
 * VideoHide every modal already issues, so the video going away and the dim arriving
 * land together. */
void UiManager_ScrimShow(uint8_t percent)
{
    if (percent > 100u) { percent = 100u; }
    s_scrim_alpha = (uint8_t)(((uint32_t)percent * 255u) / 100u);
}

void UiManager_ScrimHide(void)
{
    s_scrim_alpha = 0u;
}

/* ── video frame overlay (OVR1, above HEO) ────────────────────────────────────
 * A caller-owned ARGB_4444 surface composited on OVR1 over the video for the
 * rounded anti-aliased frame. Driven directly via the PLIB (not the canvas
 * framework, which has no 4444 mode) — same pattern as the splash/HEO. OVR1 is
 * time-shared with the song-select dialog canvas; the frame is hidden whenever
 * video is (fullscreen / dialog open), so they never contend. */
void UiManager_VideoOverlayShow(const void *buf, uint32_t x, uint32_t y,
                                uint32_t w, uint32_t h)
{
    XLCDC_SetLayerEnable(XLCDC_LAYER_OVR1, false, true);
    XLCDC_SetLayerRGBColorMode(XLCDC_LAYER_OVR1, XLCDC_RGB_COLOR_MODE_ARGB_4444, false);
    XLCDC_SetLayerAddress(XLCDC_LAYER_OVR1, (uint32_t)(uintptr_t)buf, false);
    XLCDC_SetLayerXStride(XLCDC_LAYER_OVR1, 0u, false);   /* buffer == window, no gap */
    XLCDC_SetLayerWindowXYPos(XLCDC_LAYER_OVR1, x, y, false);
    XLCDC_SetLayerWindowXYSize(XLCDC_LAYER_OVR1, w, h, false);
    XLCDC_SetLayerEnable(XLCDC_LAYER_OVR1, true, true);
}

void UiManager_VideoOverlayHide(void)
{
    XLCDC_SetLayerEnable(XLCDC_LAYER_OVR1, false, true);
}

void UiManager_SetVideoLevels(bool on)
{
    s_video_levels = on;
    s_video_rebind = true;   /* reconcile re-binds HEO next tick, applying the new GAM state */
}

bool UiManager_GetVideoLevels(void)
{
    return s_video_levels;
}

/* ── navigation drawer layer (OVR2) ───────────────────────────────────────────
 * The drawer canvas rides OVR2 (above the video frame on OVR1, so an open drawer
 * covers the frame's left edge). bind_canvas sets OVR2 to the drawer's RGB565 mode
 * each show — needed because album-art leaves OVR2 in RGBA8888. The drawer module
 * drives the slide FX after showing. */
void UiManager_ShowNavLayer(void)
{
    bind_canvas(CANVAS_NAVIGATION, HW_OVR2, XLCDC_RGB_COLOR_MODE_RGB_565, true);
}

void UiManager_HideNavLayer(void)
{
    gfxcHideCanvas(CANVAS_NAVIGATION);
    gfxcCanvasUpdate(CANVAS_NAVIGATION);
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
/* Gate a panel's whole subtree from picking without repainting it. leWidget's
 * setEnabled toggles LE_WIDGET_ENABLED but also invalidates, which would repaint
 * an unchanged surface and stall touch (Legato defers input while drawing). We
 * only want the pick effect — leUtils_PickFromWidget descends only into ENABLED
 * children, and the renderer never reads ENABLED — so toggle the flag directly.
 * IGNOREPICK would not do: it stops the panel being the pick result but still lets
 * its children be picked, so the subtree wouldn't be gated. */
static void panel_set_pickable(leWidget *w, leBool on)
{
    if (on) { w->flags |=  LE_WIDGET_ENABLED; }
    else    { w->flags &= ~LE_WIDGET_ENABLED; }
}

static void songsel_set_input(leBool on)
{
    panel_set_pickable(Marvin_PANEL_DASHBOARD, on ? LE_FALSE : LE_TRUE);
    panel_set_pickable(Marvin_PANEL_SONG_SELECT, on);
    panel_set_pickable(Marvin_PANEL_SONG_SELECT_ALBUM_ART, on);
}

void UiManager_SetDashboardPickable(bool on)
{
    panel_set_pickable(Marvin_PANEL_DASHBOARD, on ? LE_TRUE : LE_FALSE);
}

void UiManager_OpenSongSelect(void)
{
    if (s_songsel_open) { return; }
    songsel_set_input(LE_TRUE);

    /* The dialog takes OVR1 from the video frame overlay; drop the frame first
     * (video is hidden below, so there's nothing to frame). */
    UiManager_VideoOverlayHide();

    /* Dim the base view behind the dialog (mockup: bg-black/75). Free — a
     * constant-colour HEO layer with DMA off; HEO is idle because the dialog hides the
     * video anyway. Requested BEFORE the corner cut below, which has to match this dim. */
    UiManager_ScrimShow(MODAL_SCRIM_PCT);

    /* Cut the rounded corners against whatever the base view has behind them, NOW
     * rather than at build time: this is the last moment those pixels are known good. */
    UiManager_CutModalCorners(CANVAS_SONGSEL);
    bind_canvas(CANVAS_SONGSEL,   HW_OVR1, XLCDC_RGB_COLOR_MODE_RGB_565,   true);
    bind_canvas(CANVAS_ALBUM_ART, HW_OVR2, XLCDC_RGB_COLOR_MODE_RGBA_8888, true);

    /* The opaque dialog fully covers the video window, so free the DDR read
     * bandwidth its display was costing: hide HEO (capture keeps running for the
     * detector/gameplay) and discard BASE DMA behind the 1100x660 dialog. This is
     * the contention that knocked the CSI-2 D-PHY out of lock on every song switch
     * (album-art decode/blit + repaint). Both are intent only — the video-task
     * reconcile applies them, so HEO/BASE stay single-writer. */
    UiManager_VideoHide();
    modal_discard_set(CANVAS_SONGSEL);

    s_songsel_open = true;
}

void UiManager_CloseSongSelect(void)
{
    if (!s_songsel_open) { return; }
    gfxcHideCanvas(CANVAS_SONGSEL);   gfxcCanvasUpdate(CANVAS_SONGSEL);
    gfxcHideCanvas(CANVAS_ALBUM_ART); gfxcCanvasUpdate(CANVAS_ALBUM_ART);
    songsel_set_input(LE_FALSE);

    /* Dashboard is back: drop the modal's dim and BASE discard, and bring the live video
     * back up (reconcile re-binds HEO once the source is locked — that bind also takes
     * HEO's DMA back from the scrim). */
    UiManager_ScrimHide();
    modal_discard_clear();
    ScreenVideo_ShowWindowed();

    s_songsel_open = false;
}

/* ── on-screen keyboard modal (OVR1) ──────────────────────────────────────────
 * A full modal over whichever base view is showing. Like the song-select dialog it takes
 * OVR1 (dropping the video-frame overlay first), hides the live video for a clean
 * backdrop, dims what is left with the HEO scrim, and discards BASE behind its own
 * opaque rect; unlike it there's no album-art layer, and its corners are square, so
 * nothing here holds a copy of the base view that would have to be dimmed to match. The
 * keyboard canvas paints continuously into its surface; open is a pure layer bind, close
 * hides it and restores the windowed video. */
static bool s_keyboard_open = false;

void UiManager_OpenKeyboard(const char *title, const char *initial, uint32_t maxlen,
                            void (*commit)(const char *text))
{
    if (s_keyboard_open) { return; }

    ScreenKeyboard_Prepare(title, initial, maxlen, commit);
    ScreenKeyboard_SetInput(true);
    UiManager_SetBaseViewPickable(false);   /* modal: nothing behind reacts */

    /* Take OVR1 from the video frame overlay, then hide the live video. */
    UiManager_VideoOverlayHide();
    bind_canvas(CANVAS_KEYBOARD, HW_OVR1, XLCDC_RGB_COLOR_MODE_RGB_565, true);
    UiManager_VideoHide();
    UiManager_ScrimShow(MODAL_SCRIM_PCT);
    UiManager_CutModalCorners(CANVAS_KEYBOARD);
    modal_discard_set(CANVAS_KEYBOARD);

    s_keyboard_open = true;
}

void UiManager_CloseKeyboard(void)
{
    if (!s_keyboard_open) { return; }

    gfxcHideCanvas(CANVAS_KEYBOARD); gfxcCanvasUpdate(CANVAS_KEYBOARD);
    ScreenKeyboard_SetInput(false);
    UiManager_SetBaseViewPickable(true);
    UiManager_ScrimHide();
    modal_discard_clear();

    ScreenVideo_ShowWindowed();   /* restores HEO windowed + its OVR1 frame */

    s_keyboard_open = false;
}

/* ── Base view (BASE hardware layer) ──────────────────────────────────────────
 * The BASE layer shows one full-screen view at a time. Boot reveals the dashboard;
 * the nav drawer swaps it. The dashboard (CANVAS_DASH, layer 0) and wiimotes
 * (CANVAS_WIIMOTES, layer 4) are peer full-screen canvases; the swap hides the
 * outgoing canvas (an active canvas drives its HW layer — two on BASE would fight)
 * then binds the incoming one to BASE. Picking is gated to the shown view: Legato
 * picks across all attached layers regardless of canvas visibility, so the hidden
 * view's panel must be gated off or it would still intercept touches. */
typedef enum { BASE_VIEW_DASHBOARD, BASE_VIEW_WIIMOTES, BASE_VIEW_BUS,
               BASE_VIEW_SYSTEM } base_view_t;
static base_view_t s_base_view = BASE_VIEW_DASHBOARD;

/* Hide the currently-shown base view: stop its canvas driving BASE and gate its
 * (still-attached) panel out of picking. The incoming Show* then binds its own
 * canvas onto BASE. */
static void hide_current_base(void)
{
    switch (s_base_view)
    {
        case BASE_VIEW_WIIMOTES:
            gfxcHideCanvas(CANVAS_WIIMOTES); gfxcCanvasUpdate(CANVAS_WIIMOTES);
            ScreenWiimotes_SetInput(false);
            ScreenWiimotes_SetShown(false);
            break;
        case BASE_VIEW_BUS:
            gfxcHideCanvas(CANVAS_BUS); gfxcCanvasUpdate(CANVAS_BUS);
            ScreenBus_SetInput(false);
            ScreenBus_SetShown(false);
            break;
        case BASE_VIEW_SYSTEM:
            gfxcHideCanvas(CANVAS_SYSTEM); gfxcCanvasUpdate(CANVAS_SYSTEM);
            ScreenSystem_SetInput(false);
            ScreenSystem_SetShown(false);
            break;
        case BASE_VIEW_DASHBOARD:
        default:
            gfxcHideCanvas(CANVAS_DASH); gfxcCanvasUpdate(CANVAS_DASH);
            UiManager_SetDashboardPickable(false);
            ScreenDashboard_SetShown(false);
            break;
    }
}

unsigned int UiManager_BaseCanvas(void)
{
    switch (s_base_view)
    {
        case BASE_VIEW_WIIMOTES: return CANVAS_WIIMOTES;
        case BASE_VIEW_BUS:      return CANVAS_BUS;
        case BASE_VIEW_SYSTEM:   return CANVAS_SYSTEM;
        default:                 return CANVAS_DASH;
    }
}

/* Gate the currently-shown base view in/out of picking (used by the nav drawer to
 * be modal over whichever view is active). */
void UiManager_SetBaseViewPickable(bool on)
{
    switch (s_base_view)
    {
        case BASE_VIEW_WIIMOTES: ScreenWiimotes_SetInput(on);        break;
        case BASE_VIEW_BUS:      ScreenBus_SetInput(on);             break;
        case BASE_VIEW_SYSTEM:   ScreenSystem_SetInput(on);          break;
        default:                 UiManager_SetDashboardPickable(on); break;
    }
}

void UiManager_ShowWiimotes(void)
{
    if (s_base_view == BASE_VIEW_WIIMOTES) { return; }

    hide_current_base();
    bind_canvas(CANVAS_WIIMOTES, HW_BASE, XLCDC_RGB_COLOR_MODE_RGB_565, true);
    ScreenWiimotes_SetInput(true);
    ScreenWiimotes_SetShown(true);

    /* This screen shows the live video in its own smaller rect above the control
     * cards, with the matching AA frame on the overlay layer. HEO composites above
     * BASE, so the video covers whatever is under its rect — the layout keeps the
     * cards clear of it. Intent only; the video-task reconcile applies it. */
    uint32_t vx, vy, vw, vh;
    ScreenWiimotes_VideoRect(&vx, &vy, &vw, &vh);
    UiManager_VideoShow(vx, vy, vw, vh);
    UiManager_VideoOverlayShow(ScreenWiimotes_VideoFrameSurface(), vx, vy, vw, vh);

    s_base_view = BASE_VIEW_WIIMOTES;
}

/* Bus-statistics screen: a full-screen base view like wiimotes (owns the panel, so
 * the live video is dropped). Its titlebar hamburger reopens the drawer to leave. */
void UiManager_ShowStats(void)
{
    if (s_base_view == BASE_VIEW_BUS) { return; }

    UiManager_VideoHide();
    UiManager_VideoOverlayHide();

    hide_current_base();
    bind_canvas(CANVAS_BUS, HW_BASE, XLCDC_RGB_COLOR_MODE_RGB_565, true);
    ScreenBus_SetInput(true);
    ScreenBus_SetShown(true);   /* starts the ~1 Hz telemetry refresh */

    s_base_view = BASE_VIEW_BUS;
}

/* System-info screen: the node showcase, another full-screen base view (so the live
 * video is dropped, same as the bus screen). Its titlebar hamburger reopens the
 * drawer to leave; its own back button only moves between overview and detail. */
void UiManager_ShowSystem(void)
{
    if (s_base_view == BASE_VIEW_SYSTEM) { return; }

    UiManager_VideoHide();
    UiManager_VideoOverlayHide();

    hide_current_base();
    bind_canvas(CANVAS_SYSTEM, HW_BASE, XLCDC_RGB_COLOR_MODE_RGB_565, true);
    ScreenSystem_SetInput(true);
    ScreenSystem_SetShown(true);

    s_base_view = BASE_VIEW_SYSTEM;
}

void UiManager_ShowDashboard(void)
{
    if (s_base_view == BASE_VIEW_DASHBOARD) { return; }

    hide_current_base();
    bind_canvas(CANVAS_DASH, HW_BASE, XLCDC_RGB_COLOR_MODE_RGB_565, true);
    UiManager_SetDashboardPickable(true);
    ScreenDashboard_SetShown(true);   /* flushes whatever telemetry changed while away */

    s_base_view = BASE_VIEW_DASHBOARD;

    /* Restore the live video (reconcile re-binds HEO once the source locks). */
    ScreenVideo_ShowWindowed();
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

/* Runtime render lock — mutual exclusion for post-boot widget edits (setString/
 * setPressed/setScheme/invalidate) done from an app task. Legato here is
 * single-threaded (no LEGATO_USE_OSAL, no renderer lock): leUpdate runs in
 * LEGATO_Tasks and any concurrent widget/damage edit races it, dropping the damage
 * (stale-by-one repaints) or corrupting the damage list mid-paint. This suspends the
 * two Legato threads, and — because rendering is one-pass (LE_SCRATCH sized to a full
 * screen, see the journal) — only proceeds once leRenderer_IsIdle() confirms no paint
 * is in flight, so appending damage can't corrupt an active paint traversal. Held
 * only for the microseconds of an edit; never touches the actuation tasks (prio 4–5),
 * which are unaffected by suspending the prio-2 render/input pair. */
void UiManager_RenderLock(void)
{
    if (s_legato_task == NULL) { s_legato_task = xTaskGetHandle("LEGATO_Tasks"); }
    if (s_input_task  == NULL) { s_input_task  = xTaskGetHandle("SYS_INPUT_Tasks"); }

    for (;;)
    {
        if (s_legato_task != NULL) { vTaskSuspend(s_legato_task); }
        if (s_input_task  != NULL) { vTaskSuspend(s_input_task);  }

        /* Idle (or no render task to guard) → safe to edit. */
        if (s_legato_task == NULL || leRenderer_IsIdle()) { return; }

        /* Suspended mid-paint — back off, let the frame finish, retry. */
        if (s_input_task  != NULL) { vTaskResume(s_input_task);  }
        vTaskResume(s_legato_task);
        vTaskDelay(1);
    }
}

void UiManager_RenderUnlock(void)
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
    ScreenVideo_Setup();
    ScreenNavigation_Setup();
    ScreenSongSelect_Setup();
    ScreenAlbumArt_Setup();
    ScreenWiimotes_Setup();
    ScreenKeyboard_Setup();
    ScreenBus_Setup();
    ScreenSystem_Setup();

    /* Song-select starts closed: disable its layer-screens' background panels so
     * those (hidden) overlays don't capture touches meant for the dashboard.
     * UiManager_OpenSongSelect re-enables them. */
    songsel_set_input(LE_FALSE);
}

/* Paint every layer-screen surface once, behind the splash. Legato renders into a
 * canvas surface independent of whether that canvas is shown or bound to a hardware
 * layer, so a full paint here leaves every surface complete — after which showing a
 * screen is a pure layer bind with no repaint. Each panel is VISIBLE by now (its
 * *_Setup set it) and its canvas window is full-surface sized, so one invalidate
 * lands the whole surface; wait_render_idle then confirms it drained. Relied upon so
 * the drawer/dialog need never invalidate on open — MGS builds the nav panel
 * setVisible(FALSE), so its boot root-damage paints nothing until this runs. */
static void paint_all_screens_once(void)
{
    Marvin_PANEL_DASHBOARD->fn->invalidate(Marvin_PANEL_DASHBOARD);
    Marvin_PANEL_NAVIGATION->fn->invalidate(Marvin_PANEL_NAVIGATION);
    Marvin_PANEL_SONG_SELECT->fn->invalidate(Marvin_PANEL_SONG_SELECT);
    Marvin_PANEL_SONG_SELECT_ALBUM_ART->fn->invalidate(Marvin_PANEL_SONG_SELECT_ALBUM_ART);
    Marvin_PANEL_WIIMOTES->fn->invalidate(Marvin_PANEL_WIIMOTES);
    Marvin_PANEL_KEYBOARD->fn->invalidate(Marvin_PANEL_KEYBOARD);
    Marvin_PANEL_BUS->fn->invalidate(Marvin_PANEL_BUS);
    Marvin_PANEL_SYSTEM->fn->invalidate(Marvin_PANEL_SYSTEM);
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

/* The art caches report which tier they are on and how far through it; the wording is
 * ours, so they stay free of any UI vocabulary (registered in UiManager_Initialize). */
static void art_progress(const char *tier, uint32_t done, uint32_t total)
{
    bool small = (tier != NULL) && (tier[0] == 's');
    SplashProgress_SetNote(small ? "LOADING ALBUM ART" : "LOADING COVER DETAIL", done, total);
}

static void node_art_progress(uint32_t done, uint32_t total)
{
    SplashProgress_SetNote("LOADING BOARD PHOTOS", done, total);
}

static void ui_boot_task(void *param)
{
    (void)param;

    /* PHASE 1 — splash, before any Legato work, as fast as the SD allows. */
    (void)ScreenSplash_Load();   /* loads the image, or fills the fallback colour */
    ScreenSplash_Show(xlcdc_layer(SPLASH_HW_LAYER));
    enable_backlight();
    TickType_t shown_at = xTaskGetTickCount();

    /* Progress bar over the splash art, ticked by its own task so it keeps moving
     * while this one blocks in the loads below. It times itself against the duration
     * measured on the previous boot; SPLASH_MIN_MS is the floor on that prediction
     * because the reveal below can't happen sooner. See splash_progress.h. */
    SplashProgress_Start(ScreenSplash_Framebuffer(), SPLASH_MIN_MS);

    /* Splash is up — let the app start everything else (services + camera) now,
     * in parallel with the screen painting below, so it's all warm at reveal. */
    if (s_splash_shown_cb != NULL) { s_splash_shown_cb(); }

    /* Decode all album art into the RGB888 caches *before* building the screens,
     * so the song-select panel's initial selection (ScreenSongSelect_Setup ->
     * song_detail_show(0)) finds its cover already cached. Storage mounts here;
     * Legato's image decoders are up from SYS_Initialize. All behind the splash;
     * the ~1-3 s decode just extends the splash hold. See game/game_art.h. */
    SplashProgress_SetStage(SPLASH_STAGE_ART);

    /* Mount explicitly rather than leaving it to the first loader, so the card wait
     * gets its own note instead of hiding inside "loading artwork". Idempotent. */
    SplashProgress_SetNote("MOUNTING CARD", 0u, 0u);
    (void)Storage_Mount();

    (void)GameArt_LoadAll();

    /* Same deal for the board photos, and for the same reason: ScreenSystem_Setup
     * seeds its detail view below, so the photos have to be cached by then. */
    (void)NodeArt_LoadAll();

    /* PHASE 2 — build the Marvin screen + per-panel setup behind the splash.
     * Scene-graph edits (screenInit_Marvin's leAddRootWidget calls) are guarded
     * against the render tasks; the canvas/layer binds after are GFX-canvas only
     * (no root-list mutation) so they need no guard. */
    SplashProgress_SetStage(SPLASH_STAGE_SCREENS);
    scene_edit_begin();
    init_screens();
    scene_edit_end();

    /* Dashboard live on BASE (covered by the splash until reveal). Nav and
     * song-select are built and painting into their own canvases but not bound to
     * a layer yet — they take their layers at reveal, once the splash vacates. */
    bind_canvas(CANVAS_DASH, HW_BASE, XLCDC_RGB_COLOR_MODE_RGB_565, true);

    /* Park HEO off (video hidden) so the dashboard owns the full panel until video
     * is shown at reveal. BASE discard is off by reset default and stays off until
     * the reconcile arms it. Was video.c's job at task start; the compositor owns HEO
     * now. Safe as a one-shot here — the video task's reconcile is a no-op while
     * video is hidden, so this is the only HEO writer at boot. */
    heo_unbind();

    /* Paint all screens once now, behind the splash, then wait for it to drain.
     * Every surface is complete before it's ever shown; screens are event-driven
     * from here — showing one is a pure layer bind, no repaint. */
    SplashProgress_SetStage(SPLASH_STAGE_PAINT);
    paint_all_screens_once();
    wait_render_idle();

    /* Hold the splash a minimum time so a fast boot doesn't flash it away. */
    SplashProgress_SetStage(SPLASH_STAGE_HOLD);
    TickType_t elapsed   = xTaskGetTickCount() - shown_at;
    TickType_t min_ticks = pdMS_TO_TICKS(SPLASH_MIN_MS);
    if (elapsed < min_ticks) { vTaskDelay(min_ticks - elapsed); }

    /* Bar to 100% and the ticker stopped — this is the interval the next boot
     * animates against, so it is measured here, before the dwell and the fade. */
    SplashProgress_Complete();

    /* REVEAL — dissolve into the (painted) dashboard. The song-select dialog starts
     * closed: it and its OVR2 cover strip are painted into their canvases but not
     * shown; UiManager_OpenSongSelect() binds + shows the pair on demand (header tap).
     * OVR1/OVR2 colour modes are set by their users when shown (video frame → ARGB_4444,
     * dialog → RGB565, nav/album-art on OVR2), so no preemptive poke is needed here. */
    ScreenSplash_FadeOut(xlcdc_layer(SPLASH_HW_LAYER), SPLASH_FADE_MS);

    /* Bring the live video up (windowed) on HEO over the dashboard, and its OVR1
     * frame overlay. Video is intent only — the video task binds HEO on its next
     * reconcile once the source is locked. */
    ScreenVideo_ShowWindowed();

    /* Arm capture LAST — after the display is up and the boot-time task/SD/paint
     * contention has drained. The CSI-2 D-PHY RX is timing-sensitive at bring-up
     * (it must catch the source's LP11→HS transition); arming it earlier, during the
     * splash, makes it lock marginally and the lanes sit in stop-state (~0 fps). This
     * is the one ordering that must hold — see the journal (2026-06-30). */
    Video_CaptureEnable();

    /* Dashboard is revealed and every surface is painted — start the telemetry feed
     * consumer now. It's the sole writer of dashboard widgets; deferring it to here
     * keeps it off the widget tree during the build/paint/reveal window. */
    DashboardFeed_Start();

    /* Boot sequence done — arm the health monitor now (it stays idle until this
     * so its card I/O + task-list walks never perturb the reveal window). */
    HealthMonitor_NotifyReady();

    /* Last: persist this boot's duration if it has drifted, so the next boot's bar is
     * calibrated. Flash write, deliberately after everything else. */
    SplashProgress_Calibrate();

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
    ScreenWiimotes_InitSurface();
    ScreenKeyboard_InitSurface();
    ScreenBus_InitSurface();
    ScreenSystem_InitSurface();
    GFX_CANVAS_Task();

    /* Dashboard telemetry feed: create the event queue now so producers (fret
     * actuation, selection) can post immediately; the consumer task starts
     * post-reveal (below), draining any buffered events on its first run. */
    DashboardFeed_Init();

    leSetStringTable(&stringTable);
    initializeStrings();

    /* Register the compositor's HEO display hooks with the capture producer before
     * the video task starts (Video_Initialize runs later, in APP_Tasks). The video
     * task then drives heo_reconcile each tick and the ISC IRQ drives heo_frame_latch. */
    Video_SetFrameLatchCallback(heo_frame_latch);
    Video_SetDisplayReconcileCallback(heo_reconcile);

    /* Load the HEO levels-expansion gamma CLUT now, while GAM/CLUTEN are clear
     * (post XLCDC_SetupHEOLayer, pre any HEO bind). heo_bind enables GAM. */
    heo_gamma_load();

    /* Let the splash bar say what the art loaders are chewing through. */
    GameArt_SetProgressCallback(art_progress);
    NodeArt_SetProgressCallback(node_art_progress);

    (void)xTaskCreateStatic(ui_boot_task, "UiBoot", BOOT_TASK_STACK_WORDS,
                            NULL, BOOT_TASK_PRIORITY, s_boot_stack, &s_boot_tcb);
}

void UiManager_SetSplashShownCallback(void (*cb)(void))
{
    s_splash_shown_cb = cb;
}
