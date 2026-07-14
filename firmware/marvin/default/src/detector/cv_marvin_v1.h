#ifndef CV_MARVIN_V1_H
#define CV_MARVIN_V1_H

#include <stdint.h>

#include "game/fret.h"

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

typedef struct
{
    const char     *name;         /* "1p" / "2p-left" — logged, not on the wire */
    cv_sensor_xy_t  sensor[FRET_COUNT];
    /* Host-viewer strips (capture-frame coords). SENSING spans the sensor row
     * (rings painted here); STRIKE spans the strum zone below (no rings). */
    uint16_t sensing_x, sensing_y, sensing_w, sensing_h;
    uint16_t strike_x,  strike_y,  strike_w,  strike_h;
} cv_marvin_v1_config_t;

extern const cv_marvin_v1_config_t CV_MARVIN_CFG_1P;
extern const cv_marvin_v1_config_t CV_MARVIN_CFG_2P_LEFT;

void CvMarvinV1_Initialize(void);

/* Swap the active geometry. Takes effect on the next processed frame; the
 * detector resets its per-fret latch state and re-publishes config on change.
 * Safe to call from another task. */
void CvMarvinV1_SetConfig(const cv_marvin_v1_config_t *cfg);
const cv_marvin_v1_config_t *CvMarvinV1_GetConfig(void);

#endif
