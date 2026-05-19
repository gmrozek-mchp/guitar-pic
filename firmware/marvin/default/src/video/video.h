#ifndef VIDEO_H
#define VIDEO_H

/* Video pipeline: HDMI source → TC358743 bridge → CSI-2 → CSI2DC → ISC →
 * DDR → XLCDC HEO. The module owns its own FreeRTOS task; everything below
 * (tc358743, isc_capture, HEO bind/unbind, NONE↔ACTIVE state machine) runs
 * inside that task. */

void Video_Initialize(void);

#endif
