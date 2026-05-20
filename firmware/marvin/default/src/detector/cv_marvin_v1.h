#ifndef CV_MARVIN_V1_H
#define CV_MARVIN_V1_H

/* Reference vision detector. Owns its FreeRTOS task; subscribes to the video
 * module's frame queue; publishes detector_state_t records onto the detector-
 * state bus (spec §4.2). M1 stub: forwards frame metadata only — real
 * detection logic (Q8) lands incrementally on top. */

void CvMarvinV1_Initialize(void);

#endif
