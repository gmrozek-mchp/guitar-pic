#ifndef VU_METER_H
#define VU_METER_H

#include "beat_engine.h"

/* The eight discrete board LEDs (LED0..LED7 = RC8..RC15) as an audio VU meter.
 * The bar fills from LED7 (physically leftmost = bottom of the bar) up to LED0
 * (top). Driven once per beat frame from the frame's raw envelope, with instant
 * attack and a per-frame decay so peaks hold briefly and fall smoothly. */

void VU_Initialize(void);
void VU_Update(const BeatFrame *f);
void VU_Off(void);

#endif /* VU_METER_H */
