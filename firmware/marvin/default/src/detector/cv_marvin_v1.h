#ifndef CV_MARVIN_V1_H
#define CV_MARVIN_V1_H

#include <stdint.h>

#include "fret.h"

/* Reference vision detector. Owns its FreeRTOS task; subscribes to the video
 * module's frame queue; publishes detector_state_t records onto the detector-
 * state bus (spec §4.2).
 *
 * Geometry (per-fret sample points + host-viewer strip regions) is a passed-in
 * config so the one detector can read either the single-player centered highway
 * or the 2-player left (robot) highway. Note colors and thresholds are shared
 * across configs (the gems are identical); they stay module-internal. */

/* Per-fret sample points in native 720×480 capture space: (hx,hy) = brightness
 * sensor, (ex,ey) = color-filtered edge sensor. */
typedef struct { uint16_t hx, hy, ex, ey; } cv_sensor_xy_t;

/* Play difficulty index. 0..3 are the same values as game_difficulty_t
 * (GAME_DIFF_EASY..GAME_DIFF_EXPERT) — the detector takes a bare index rather
 * than including game/game_selection.h, since game/ already consumes this
 * header. */
#define CV_DIFF_COUNT   4u

/* Which lead row a geometry config reads. One row per highway: the sensor and
 * strike rows happen to match across 1p and 2p-left today, but the leads are
 * calibrated independently and nothing may assume they agree. */
typedef enum
{
    CV_LEAD_SLOT_1P = 0,
    CV_LEAD_SLOT_2P_LEFT,
    CV_LEAD_SLOT_COUNT
} cv_lead_slot_t;

typedef struct
{
    const char     *name;         /* "1p" / "2p-left" — logged, not on the wire */
    cv_sensor_xy_t  sensor[FRET_COUNT];
    /* Host-viewer strips (capture-frame coords). SENSING spans the sensor row
     * (rings painted here); STRIKE spans the strum zone below (no rings). */
    uint16_t sensing_x, sensing_y, sensing_w, sensing_h;
    uint16_t strike_x,  strike_y,  strike_w,  strike_h;
    /* perf_strip_kind_t the two bands ship under. Per-highway, so a capture
     * records which geometry produced them and the host can place both
     * highways' bands in one session. */
    uint8_t  sensing_kind, strike_kind;
    /* cv_lead_slot_t: which row of the observation-lead table this highway
     * reads. The lead itself is runtime state (per difficulty), not geometry. */
    uint8_t  lead_slot;
} cv_marvin_v1_config_t;

extern const cv_marvin_v1_config_t CV_MARVIN_CFG_1P;
extern const cv_marvin_v1_config_t CV_MARVIN_CFG_2P_LEFT;

void CvMarvinV1_Initialize(void);

/* Swap the active geometry. Takes effect on the next processed frame; the
 * detector resets its per-fret latch state and re-publishes config on change.
 * Safe to call from another task. */
void CvMarvinV1_SetConfig(const cv_marvin_v1_config_t *cfg);
const cv_marvin_v1_config_t *CvMarvinV1_GetConfig(void);

/* Observation lead (ms): travel time for a note from the sensor row down to the
 * strike line. The detector adds it to each frame's timestamp to stamp
 * detector_state_t.strike_at_ms; the timing pipeline schedules in that
 * strike-line time base and holds no delay constant of its own.
 *
 * The highway's scroll speed rises with play difficulty, so the lead falls with
 * it — hence a lead per (highway, difficulty) rather than one constant. The
 * active difficulty comes from the committed GameSelection (game_controller
 * applies it at run start); the console `cvdiff` command overrides it and
 * retunes individual cells for calibration.
 *
 * Out-of-range indices are ignored / read back as 0. Difficulty and lead
 * changes take effect on the next processed frame and re-publish the detector
 * config; unlike a geometry swap they leave per-fret latch state alone (the
 * sensors have not moved). Safe to call from another task. */
void    CvMarvinV1_SetDifficulty(uint8_t difficulty);
uint8_t CvMarvinV1_GetDifficulty(void);
uint16_t CvMarvinV1_GetLeadMs(uint8_t slot, uint8_t difficulty);
void     CvMarvinV1_SetLeadMs(uint8_t slot, uint8_t difficulty, uint16_t ms);

#endif
