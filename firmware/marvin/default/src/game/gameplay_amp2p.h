#ifndef MARVIN_GAMEPLAY_AMP2P_H
#define MARVIN_GAMEPLAY_AMP2P_H

#include <stdint.h>

#include "game/gameplay_metadata.h"

/* 2-player amp scoreboard score reader, ported from the host prototype
 * (tools/gameplay/gameplay/amp2p.py) and driven by game/gameplay_metadata.h.
 *
 * Two-player GH3 replaces the 1-player scoring block with two amp scoreboards, one
 * per side. Inside each block the score is a *fixed grid* — equal cells at a
 * constant pitch, right-aligned against a fixed edge — so unlike gp_read_score
 * there is no ink segmentation: cell positions come from the grid table and each
 * cell is classified on its own. Which cells hold a digit is read off the display
 * rather than inferred from the match: unused leading cells are unpowered, so a
 * per-cell luma-contrast gate gives the count independently of glyph quality.
 *
 * The block origin is gp_probes[side].block — the same rect the presence probe
 * uses — so `side` is a GP_AMP2P_SIDE_* value, i.e. an index into gp_probes[].
 * One definition of where the amp is, shared with presence.
 *
 * Pure (no FreeRTOS/video), so it compiles + cross-checks on a host. Not reentrant
 * (static scratch); only the single observer task calls it.
 *
 * PRESENCE IS THE CALLER'S JOB and is not optional: this reader only asks what the
 * digit band says, so on a frame where the amp is off screen it can still find lit
 * cells in whatever art is there. Gate on gp_present() first. */

typedef struct
{
    int32_t value;           /* assembled score, or -1 when nothing trustworthy was read */
    uint8_t ncells;          /* powered//laid-out cells found (0 = strip dark) */
    uint8_t layout_measured; /* 0 = read on an extrapolated 6-digit layout (see below) */
    uint8_t layout_w;        /* cell width and pitch actually used (diagnostic) */
    uint8_t layout_pitch;
    int32_t dist;            /* worst per-cell L1 to its template (diagnostic) */
    int32_t margin;          /* weakest per-cell runner-up gap (confidence) */
} gp_amp2p_t;

/* Read one amp's score. Returns 0 on a usable frame (inspect out->value), -1 on a
 * bad frame size or side. `value` is -1 — never a guess — when the layout or the
 * match cannot justify a number: a non-right-aligned lit pattern, ink that sits on
 * no known layout, or a cell failing the distance/margin gates.
 *
 * SIX DIGITS: the strip re-lays-out past 99999 and that pitch has never been
 * captured, so it is *bounded* rather than guessed — six cells cannot fit
 * GP_AMP2P_CONTAINER_W at the measured pitch, and gp_amp2p_grid6 holds the layouts
 * that do fit. Such a read carries layout_measured = 0. Treat it as informative,
 * not authoritative, until a real >99999 capture confirms the pitch. */
int gp_read_amp2p(const uint8_t *frame, int width, int height,
                  uint8_t side, gp_amp2p_t *out);

#endif
