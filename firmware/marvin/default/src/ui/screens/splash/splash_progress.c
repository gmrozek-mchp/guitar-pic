#include "ui/screens/splash/splash_progress.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "log.h"
#include "flash/settings.h"                       /* persisted boot profile */
#include "ui/gfx/glyph_blit.h"
#include "ui/ui_manager.h"                        /* BASE_W, BASE_H */
#include "gfx/legato/generated/le_gen_assets.h"   /* DejaVuSansMono_12 */

/* Geometry, in panel pixels. Transcribed from the mockup (App.tsx SplashScreen) at
 * 1280×800: px-16 side margins, pb-10 bottom margin, h-2 bar, gap-2 above it, and a
 * text-xs (12 px / 16 px line) row of monospace above that. */
#define BAR_X        64u
#define BAR_W      1152u
#define BAR_Y       752u
#define BAR_H         8u
#define BAR_R         4u    /* rounded-full on an 8 px bar */
#define TEXT_Y      728u

/* Backup band: every row this module touches — the text row, the bar, and the glow
 * rows either side of it. */
#define BAND_Y      724u
#define BAND_H       40u

/* Erase box for the text: every band row above the glow, so a glyph reaching past the
 * 16 px line box can't leave residue behind a label change. */
#define TEXT_ERASE_Y  BAND_Y
#define TEXT_ERASE_H  (BAR_Y - GLOW_ROWS - BAND_Y)

#define FONT       (&DejaVuSansMono_12)

#define COL_TRACK   0x000000u   /* bg-black/50  */
#define COL_TRACK_A      128u
#define COL_BORDER  0xFFFFFFu   /* border-white/10 */
#define COL_BORDER_A      26u
#define COL_FILL_FROM 0x0E7490u
#define COL_FILL_TO   0x22D3EEu
#define COL_TEXT    0xA1A1AAu   /* zinc-400 */

/* Neon halo standing in for the mockup's `0 0 10px #22d3ee88` box-shadow: a couple of
 * fading rows above and below the fill. Set to 0 for a flat bar. */
#define GLOW_ENABLED  1
#define GLOW_ROWS     2u

#define TICK_MS          40u    /* 25 Hz; repaints only on an actual change */
#define DONE_HOLD_MS    200u    /* dwell at 100% before the reveal          */
#define CALIB_TOLERANCE_MS   250u   /* re-persist only past this per-stage drift */
#define MAX_PERMILLE         990u   /* only _Complete() writes 1000          */

#define TASK_STACK_WORDS 768u
#define TASK_PRIORITY      3u   /* above the boot task (2), which blocks in long calls */

/* Work stages — every stage but the last, which is slack (see below). The settings
 * record carries exactly this many measurements. */
#define WORK_STAGES   (SPLASH_STAGE_COUNT - 1u)
typedef char boot_profile_matches_settings[(WORK_STAGES == SETTINGS_BOOT_STAGES) ? 1 : -1];

static const char *const s_labels[SPLASH_STAGE_COUNT] =
{
    "INITIALIZING SYSTEM",
    "LOADING ARTWORK",
    "BUILDING INTERFACE",
    "RENDERING SCREENS",
    "READY",
};

/* Cold-start profile, in ms per work stage — the only guessed numbers left, and they
 * are replaced by measurement after one boot. Deliberately generous: the first-ever
 * bar overrunning its prediction just pins briefly at a stage ceiling. */
static const uint16_t s_seed_ms[WORK_STAGES] = { 400u, 3000u, 700u, 700u };

/* Pristine splash art for the band, and the rendered track. Both are backdrops the
 * repaints composite against, so nothing ever double-blends: the text erases to the
 * art, the fill (including its anti-aliased leading cap) draws onto the track. Plain
 * cached RAM — only the CPU reads them; the writes go to the non-cached scanout. */
static uint32_t s_art[BAND_H][BAR_W];
static uint32_t s_track[BAR_H][BAR_W];

static uint32_t *s_fb;
static TickType_t s_start;

/* Prediction, from the previous boot's profile: per-stage duration and the permille mark
 * each stage begins at (plus a final mark, so a stage always has both ends). */
static uint32_t s_pred_ms[SPLASH_STAGE_COUNT];      /* duration of each stage      */
static uint32_t s_pred_at_ms[SPLASH_STAGE_COUNT];   /* elapsed at each stage's entry */
static uint32_t s_mark[SPLASH_STAGE_COUNT + 1u];    /* permille at each entry       */
static uint32_t s_predicted_ms;

/* Measurement, for the next boot: when each stage was entered, and the total. */
static uint32_t s_entered_ms[SPLASH_STAGE_COUNT];
static uint32_t s_measured_ms;

static volatile splash_stage_t s_stage;
static volatile bool           s_stop;
static volatile bool           s_exited;

static uint32_t s_permille;        /* last rendered, for monotonicity */
static uint32_t s_fill_w;          /* last rendered fill width, px    */
static uint32_t s_glow_w;          /* glow painted out to this width  */
static uint32_t s_pct;             /* last rendered percentage        */
static const char *s_label;        /* last rendered label             */
static uint32_t s_label_box_w;     /* erase width for the label       */
static uint32_t s_pct_box_w;       /* erase width for the percentage  */

static StackType_t  s_stack[TASK_STACK_WORDS];
static StaticTask_t s_tcb;

/* ── pixel helpers ───────────────────────────────────────────────────────── */

/* Blend `rgb` at alpha `a` over one RGBA8888 pixel word (0xRRGGBBAA), staying opaque. */
static uint32_t px_blend(uint32_t dst, uint32_t rgb, uint32_t a)
{
    uint32_t inv = 255u - a;
    uint32_t r = ((((rgb >> 16) & 0xFFu) * a) + (((dst >> 24) & 0xFFu) * inv) + 127u) / 255u;
    uint32_t g = ((((rgb >>  8) & 0xFFu) * a) + (((dst >> 16) & 0xFFu) * inv) + 127u) / 255u;
    uint32_t b = ((((rgb      ) & 0xFFu) * a) + (((dst >>  8) & 0xFFu) * inv) + 127u) / 255u;
    return (r << 24) | (g << 16) | (b << 8) | 0xFFu;
}

static uint32_t *fb_at(uint32_t y, uint32_t lx)
{
    return &s_fb[(y * BASE_W) + BAR_X + lx];
}

static const uint32_t *art_at(uint32_t y, uint32_t lx)
{
    return &s_art[y - BAND_Y][lx];
}

/* Coverage of a rounded rect at a point, from its signed distance — 1 inside, 0
 * outside, a 1 px anti-aliased band at the edge. Same approach (and float math) as
 * ui/gfx/aa_corners.c. */
static float rr_cov(float px, float py, float x0, float y0, float w, float h, float r)
{
    float hw = w * 0.5f;
    float hh = h * 0.5f;
    float dx = fabsf(px - (x0 + hw)) - (hw - r);
    float dy = fabsf(py - (y0 + hh)) - (hh - r);
    float ox = (dx > 0.0f) ? dx : 0.0f;
    float oy = (dy > 0.0f) ? dy : 0.0f;
    float in = (dx > dy) ? dx : dy;

    if (in > 0.0f) { in = 0.0f; }

    float c = 0.5f - (sqrtf((ox * ox) + (oy * oy)) + in - r);

    if (c < 0.0f) { c = 0.0f; }
    if (c > 1.0f) { c = 1.0f; }
    return c;
}

/* Fill coverage for a capsule `w` px wide. The straight section between the caps is
 * fully covered at every row (height 8, radius 4), so the distance field is only
 * evaluated in the two cap boxes. */
static float fill_cov(uint32_t lx, uint32_t ly, uint32_t w)
{
    if ((lx >= BAR_R) && ((lx + BAR_R) < w)) { return 1.0f; }

    return rr_cov((float)lx + 0.5f, (float)ly + 0.5f,
                  0.0f, 0.0f, (float)w, (float)BAR_H, (float)BAR_R);
}

/* Gradient colour at a column. The ramp spans the FULL track, not the filled part, so
 * a pixel's colour doesn't shift as the bar grows (same rule as widget_bar.c). */
static uint32_t fill_rgb(uint32_t lx)
{
    uint32_t t  = (lx * 255u) / (BAR_W - 1u);
    uint32_t r0 = (COL_FILL_FROM >> 16) & 0xFFu, r1 = (COL_FILL_TO >> 16) & 0xFFu;
    uint32_t g0 = (COL_FILL_FROM >>  8) & 0xFFu, g1 = (COL_FILL_TO >>  8) & 0xFFu;
    uint32_t b0 =  COL_FILL_FROM        & 0xFFu, b1 =  COL_FILL_TO        & 0xFFu;

    uint32_t r = r0 + (((r1 - r0) * t) / 255u);
    uint32_t g = g0 + (((g1 - g0) * t) / 255u);
    uint32_t b = b0 + (((b1 - b0) * t) / 255u);
    return (r << 16) | (g << 8) | b;
}

/* ── drawing ─────────────────────────────────────────────────────────────── */

static void band_backup(void)
{
    for (uint32_t r = 0u; r < BAND_H; r++)
    {
        (void)memcpy(s_art[r], fb_at(BAND_Y + r, 0u), BAR_W * sizeof(uint32_t));
    }
}

static void art_restore(uint32_t y, uint32_t h, uint32_t lx, uint32_t w)
{
    if ((lx + w) > BAR_W) { w = BAR_W - lx; }
    for (uint32_t r = 0u; r < h; r++)
    {
        (void)memcpy(fb_at(y + r, lx), art_at(y + r, lx), w * sizeof(uint32_t));
    }
}

/* Translucent black capsule with a 1 px hairline border, composited onto the art and
 * kept as the fill's backdrop. Drawn once. */
static void draw_track(void)
{
    for (uint32_t ly = 0u; ly < BAR_H; ly++)
    {
        for (uint32_t lx = 0u; lx < BAR_W; lx++)
        {
            float px = (float)lx + 0.5f;
            float py = (float)ly + 0.5f;
            float co = rr_cov(px, py, 0.0f, 0.0f, (float)BAR_W, (float)BAR_H, (float)BAR_R);
            float ci = rr_cov(px, py, 1.0f, 1.0f, (float)(BAR_W - 2u), (float)(BAR_H - 2u),
                              (float)(BAR_R - 1u));
            float ring = co - ci;

            if (ring < 0.0f) { ring = 0.0f; }

            uint32_t p = px_blend(*art_at(BAR_Y + ly, lx), COL_TRACK,
                                  (uint32_t)((co * (float)COL_TRACK_A) + 0.5f));
            p = px_blend(p, COL_BORDER, (uint32_t)((ring * (float)COL_BORDER_A) + 0.5f));

            s_track[ly][lx] = p;
            *fb_at(BAR_Y + ly, lx) = p;
        }
    }
}

static void draw_fill(uint32_t w)
{
    for (uint32_t lx = 0u; lx < w; lx++)
    {
        uint32_t rgb = fill_rgb(lx);

        for (uint32_t ly = 0u; ly < BAR_H; ly++)
        {
            float c = fill_cov(lx, ly, w);

            if (c <= 0.0f) { continue; }
            *fb_at(BAR_Y + ly, lx) = px_blend(s_track[ly][lx], rgb,
                                              (uint32_t)((c * 255.0f) + 0.5f));
        }
    }
}

#if GLOW_ENABLED
static void draw_glow(uint32_t from_x, uint32_t to_x)
{
    static const uint8_t alpha[GLOW_ROWS] = { 56u, 22u };

    for (uint32_t lx = from_x; lx < to_x; lx++)
    {
        uint32_t rgb = fill_rgb(lx);

        for (uint32_t g = 0u; g < GLOW_ROWS; g++)
        {
            uint32_t above = BAR_Y - 1u - g;
            uint32_t below = BAR_Y + BAR_H + g;

            *fb_at(above, lx) = px_blend(*art_at(above, lx), rgb, alpha[g]);
            *fb_at(below, lx) = px_blend(*art_at(below, lx), rgb, alpha[g]);
        }
    }
}
#endif

/* "42%" — no printf on the splash path. */
static void fmt_pct(char *buf, uint32_t pct)
{
    char     digits[3];
    uint32_t n = 0u;

    do { digits[n++] = (char)('0' + (pct % 10u)); pct /= 10u; } while (pct != 0u);

    for (uint32_t i = 0u; i < n; i++) { buf[i] = digits[n - 1u - i]; }
    buf[n]      = '%';
    buf[n + 1u] = '\0';
}

static void draw_text(const char *label, uint32_t pct)
{
    if (label != s_label)
    {
        art_restore(TEXT_ERASE_Y, TEXT_ERASE_H, 0u, s_label_box_w);
        GlyphBlit_Text(s_fb, BASE_W, BASE_H, (int32_t)BAR_X, (int32_t)TEXT_Y,
                       label, FONT, COL_TEXT);
        s_label = label;
    }

    if (pct != s_pct)
    {
        char text[8];
        fmt_pct(text, pct);

        art_restore(TEXT_ERASE_Y, TEXT_ERASE_H, BAR_W - s_pct_box_w, s_pct_box_w);
        uint32_t w = GlyphBlit_TextWidth(FONT, text);
        GlyphBlit_Text(s_fb, BASE_W, BASE_H,
                       (int32_t)(BAR_X + BAR_W - w), (int32_t)TEXT_Y, text, FONT, COL_TEXT);
        s_pct = pct;
    }
}

static void render(uint32_t permille)
{
    uint32_t w = (BAR_W * permille) / 1000u;

    if (w != s_fill_w)
    {
        draw_fill(w);
#if GLOW_ENABLED
        draw_glow(s_glow_w, w);
        s_glow_w = w;
#endif
        s_fill_w = w;
    }
    draw_text(s_labels[s_stage], (permille + 5u) / 10u);
}

/* ── progress model ──────────────────────────────────────────────────────── */

static uint32_t elapsed_ms(void)
{
    return (uint32_t)((xTaskGetTickCount() - s_start) * portTICK_PERIOD_MS);
}

/* Build the permille marks from a per-stage duration profile: each stage owns the share
 * of the bar its measured duration is of the whole, so the bar's speed tracks what each
 * stage actually costs instead of being uniform in time.
 *
 * The last stage is SLACK, not work — it waits out the caller's minimum splash hold, so
 * its duration is whatever is left over (0 if the work already exceeded the hold). Storing
 * it as a measured cost would be self-defeating: speed the work up and the idle wait grows,
 * which would train the bar to crawl through the stages that do the work. */
static void predict(const uint16_t *work_ms, uint32_t min_hold_ms)
{
    uint32_t total = 0u;

    for (uint32_t i = 0u; i < WORK_STAGES; i++)
    {
        s_pred_ms[i] = (work_ms[i] > 0u) ? work_ms[i] : 1u;   /* never divide by zero */
        total       += s_pred_ms[i];
    }

    s_predicted_ms         = (total < min_hold_ms) ? min_hold_ms : total;
    s_pred_ms[WORK_STAGES] = s_predicted_ms - total;          /* the slack stage */

    uint32_t cum = 0u;
    for (uint32_t i = 0u; i < SPLASH_STAGE_COUNT; i++)
    {
        s_pred_at_ms[i] = cum;
        s_mark[i]       = (cum * MAX_PERMILLE) / s_predicted_ms;
        cum            += s_pred_ms[i];
    }
    s_mark[SPLASH_STAGE_COUNT] = MAX_PERMILLE;
}

/* Interpolate across the current stage's own share of the bar, by how long the stage has
 * actually been running against how long it is predicted to take. A stage that finishes
 * early hands the bar straight to the next stage's mark; one that runs long eases to its
 * own ceiling and waits there. Monotone — the marks only increase, and a stage's start
 * value is the previous stage's ceiling. */
static uint32_t permille_now(void)
{
    splash_stage_t stage = s_stage;
    uint32_t       from  = s_mark[stage];
    uint32_t       to    = s_mark[stage + 1u];
    uint32_t       dur   = s_pred_ms[stage];
    uint32_t       in    = elapsed_ms() - s_entered_ms[stage];
    uint32_t       p     = to;

    if (dur > 0u)
    {
        if (in > dur) { in = dur; }
        p = from + (((to - from) * in) / dur);
    }

    if (p < s_permille) { p = s_permille; }

    s_permille = p;
    return p;
}

static void progress_task(void *param)
{
    (void)param;

    while (!s_stop)
    {
        render(permille_now());
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
    s_exited = true;
    vTaskDelete(NULL);
}

/* ── public API ──────────────────────────────────────────────────────────── */

void SplashProgress_Start(uint32_t *fb, uint32_t min_hold_ms)
{
    if (fb == NULL) { return; }

    s_fb    = fb;
    s_start = xTaskGetTickCount();
    s_stage = SPLASH_STAGE_SPLASH;
    s_stop  = false;
    s_exited = false;
    s_permille = 0u;
    s_fill_w   = 0u;
    s_glow_w   = 0u;
    s_pct      = UINT32_MAX;   /* force the first text draw */
    s_label    = NULL;

    for (uint32_t i = 0u; i < SPLASH_STAGE_COUNT; i++) { s_entered_ms[i] = 0u; }

    /* Erase boxes sized to the content they have to clear. */
    s_label_box_w = 0u;
    for (uint32_t i = 0u; i < SPLASH_STAGE_COUNT; i++)
    {
        uint32_t w = GlyphBlit_TextWidth(FONT, s_labels[i]);
        if (w > s_label_box_w) { s_label_box_w = w; }
    }
    s_label_box_w += 4u;
    s_pct_box_w    = GlyphBlit_TextWidth(FONT, "100%") + 4u;

    /* Mark the bar from the previous boot's per-stage profile; all-zero means nothing has
     * been measured yet (fresh settings), so fall back to the compiled seed. */
    const uint16_t *stored = Settings_Get()->boot_stage_ms;
    uint32_t        sum    = 0u;
    for (uint32_t i = 0u; i < WORK_STAGES; i++) { sum += stored[i]; }
    predict((sum > 0u) ? stored : s_seed_ms, min_hold_ms);

    LOG_INFO("SPLASH: predict %lu ms %s, marks %lu/%lu/%lu/%lu/%lu\r\n",
             (unsigned long)s_predicted_ms, (sum > 0u) ? "(measured)" : "(seed)",
             (unsigned long)s_mark[0], (unsigned long)s_mark[1], (unsigned long)s_mark[2],
             (unsigned long)s_mark[3], (unsigned long)s_mark[4]);

    band_backup();
    draw_track();
    render(0u);

    (void)xTaskCreateStatic(progress_task, "SplashBar", TASK_STACK_WORDS,
                            NULL, TASK_PRIORITY, s_stack, &s_tcb);
}

void SplashProgress_SetStage(splash_stage_t stage)
{
    if ((s_fb == NULL) || (stage >= SPLASH_STAGE_COUNT)) { return; }

    /* Entry time before the stage itself, so the ticker never reads a stage against a
     * stale entry. This is also the measurement the next boot is marked from. */
    s_entered_ms[stage] = elapsed_ms();
    s_stage             = stage;

    LOG_INFO("SPLASH: %s at %lu ms (predicted %lu, %lu permille)\r\n", s_labels[stage],
             (unsigned long)s_entered_ms[stage], (unsigned long)s_pred_at_ms[stage],
             (unsigned long)s_mark[stage]);
}

void SplashProgress_Complete(void)
{
    if (s_fb == NULL) { return; }

    s_measured_ms = elapsed_ms();

    /* Stop the ticker and wait for it to leave, so the framebuffer has a single
     * writer again before the final draw (and before the fade). */
    s_stop = true;
    for (uint32_t guard = 0u; !s_exited && (guard < 100u); guard++)
    {
        vTaskDelay(pdMS_TO_TICKS(TICK_MS / 4u));
    }

    s_permille = 1000u;
    render(1000u);
    vTaskDelay(pdMS_TO_TICKS(DONE_HOLD_MS));

    LOG_INFO("SPLASH: boot %lu ms (predicted %lu)\r\n",
             (unsigned long)s_measured_ms, (unsigned long)s_predicted_ms);
}

void SplashProgress_Calibrate(void)
{
    if (s_measured_ms == 0u) { return; }

    /* This boot's work-stage durations: the gaps between successive stage entries. The
     * slack stage is not stored (see predict) — the entry that closes the last work stage
     * is when the slack stage began. */
    uint32_t        measured[WORK_STAGES];
    const uint16_t *stored = Settings_Get()->boot_stage_ms;
    bool            drifted = false;

    for (uint32_t i = 0u; i < WORK_STAGES; i++)
    {
        measured[i]     = s_entered_ms[i + 1u] - s_entered_ms[i];
        uint32_t drift  = (measured[i] > stored[i]) ? (measured[i] - stored[i])
                                                   : (stored[i] - measured[i]);
        if (drift > CALIB_TOLERANCE_MS) { drifted = true; }

        LOG_INFO("SPLASH: %-20s %4lu ms (was %lu)\r\n",
                 s_labels[i], (unsigned long)measured[i], (unsigned long)stored[i]);
    }

    if (drifted) { (void)Settings_SetBootStages(measured); }
}
