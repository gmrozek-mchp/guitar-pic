#ifndef SPLASH_PROGRESS_H
#define SPLASH_PROGRESS_H

#include <stdint.h>

/* Boot progress bar drawn into the splash framebuffer.
 *
 * The splash is up long before Legato paints anything, so the bar is composited
 * straight into the splash's RGBA8888 scanout buffer by a small ticker task — no
 * canvas, no widget, no paint pass (text comes from the Legato font ASSETS via
 * ui/gfx/glyph_blit.h, which needs neither).
 *
 * The bar's milestones are not apportioned by hand: each stage below owns the share of
 * the bar that its duration ON THE PREVIOUS BOOT was of the whole, read from the QSPI
 * settings ring (settings_t.boot_stage_ms). Within a stage the bar interpolates on how
 * long that stage has actually been running against how long it is predicted to take, so
 * a stage that finishes early hands the bar straight to the next mark and one that runs
 * long eases to its own ceiling and waits there. Smooth, monotone, and self-grounding —
 * the first boot after a settings wipe uses a compiled seed profile and stores what it
 * measured for the next one.
 *
 * The LAST stage is slack, not work: it waits out the caller's minimum splash hold, so
 * its duration is the remainder and is never stored. Treating it as a measured cost would
 * invert the calibration — speeding the work up lengthens the idle wait, which would then
 * train the bar to crawl through the stages that do the work. */

typedef enum
{
    SPLASH_STAGE_SPLASH = 0,   /* splash up, services spawning          */
    SPLASH_STAGE_ART,          /* GameArt/NodeArt_LoadAll (the long one) */
    SPLASH_STAGE_SCREENS,      /* init_screens                          */
    SPLASH_STAGE_PAINT,        /* paint_all_screens_once + render wait   */
    SPLASH_STAGE_HOLD,         /* work done, waiting out the min hold    */
    SPLASH_STAGE_COUNT
} splash_stage_t;

/* Draw the bar's initial state into `fb` (the splash scanout buffer) and start the
 * ticker. `min_hold_ms` is the caller's minimum splash hold — the reveal cannot happen
 * sooner, so it also floors the predicted duration and sizes the slack stage. */
void SplashProgress_Start(uint32_t *fb, uint32_t min_hold_ms);

/* Advance to a boot stage: sets the label, hands the bar that stage's share, and stamps
 * the entry time this boot's profile is measured from. Call from the boot task, in order
 * — every stage's entry closes the previous stage's measurement. */
void SplashProgress_SetStage(splash_stage_t stage);

/* Boot done: records the measured duration, stops the ticker, and shows 100% for a
 * moment. Returns once the bar is at 100% and no task is writing the framebuffer. */
void SplashProgress_Complete(void);

/* Log this boot's measured profile and persist it if any stage drifted from the stored
 * one. Flash write — call after the reveal, off the boot critical path. */
void SplashProgress_Calibrate(void);

#endif /* SPLASH_PROGRESS_H */
