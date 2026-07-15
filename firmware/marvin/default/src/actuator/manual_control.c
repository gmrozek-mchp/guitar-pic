#include "manual_control.h"

#include "fretboard_link.h"
#include "guitar_cmd.h"
#include "game/timing_pipeline.h"

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"

#include "log.h"

static const uint8_t s_fret_bit[FRET_COUNT] =
{
    [FRET_GREEN]  = GUITAR_BTN_GREEN,
    [FRET_RED]    = GUITAR_BTN_RED,
    [FRET_YELLOW] = GUITAR_BTN_YELLOW,
    [FRET_BLUE]   = GUITAR_BTN_BLUE,
    [FRET_ORANGE] = GUITAR_BTN_ORANGE,
};

static volatile bool    s_enabled;
static volatile uint8_t s_fret_mask;
static volatile uint8_t s_strum_mask;

static void recompute_and_send(void)
{
    if (!s_enabled) { return; }
    uint8_t mask = (uint8_t)(s_fret_mask | s_strum_mask);
    FretboardLink_Send(mask, (uint8_t)PERF_ACTUATOR_PRODUCER_MANUAL);
}

void ManualControl_Initialize(void)
{
}

bool ManualControl_IsEnabled(void)
{
    return s_enabled;
}

void ManualControl_SetEnabled(bool enabled)
{
    if (enabled == s_enabled) { return; }

    if (enabled)
    {
        /* Gate the pipeline before we own the wire so the next pipeline
         * frame can't race a stale mask onto the link between our zero
         * and our enable flip. */
        TimingPipeline_SetEnabled(false);
        s_fret_mask = 0u;
        s_strum_mask = 0u;
        s_enabled = true;
        FretboardLink_Send(0u, (uint8_t)PERF_ACTUATOR_PRODUCER_MANUAL);
        LOG_INFO("MC: manual mode on\r\n");
    }
    else
    {
        /* Release while we still own the wire, then hand back. The
         * pipeline's next advance() (≤ TP_TICK_MS = 5 ms) republishes
         * its own correct mask. */
        s_enabled = false;
        s_fret_mask = 0u;
        s_strum_mask = 0u;
        FretboardLink_Send(0u, (uint8_t)PERF_ACTUATOR_PRODUCER_MANUAL);
        TimingPipeline_SetEnabled(true);
        LOG_INFO("MC: manual mode off\r\n");
    }
}

void ManualControl_SetFret(fret_t fret, bool pressed)
{
    if ((unsigned)fret >= FRET_COUNT) { return; }
    uint8_t bit = s_fret_bit[fret];
    if (pressed) { s_fret_mask |= bit; }
    else         { s_fret_mask &= (uint8_t)~bit; }
    recompute_and_send();
}

void ManualControl_SetStrum(bool down, bool pressed)
{
    uint8_t bit = down ? GUITAR_BTN_STRUM_DOWN : GUITAR_BTN_STRUM_UP;
    if (pressed) { s_strum_mask |= bit; }
    else         { s_strum_mask &= (uint8_t)~bit; }
    recompute_and_send();
}
