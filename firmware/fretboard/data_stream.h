#ifndef DATA_STREAM_H
#define DATA_STREAM_H

#include <stdint.h>
#include <stdbool.h>
#include "fret_scan.h"

void data_stream_init(void);

/* Stage one 18-byte detector frame — the current ADC scan plus `applied_mask`
   (the bitmask this node drove to the guitar this scan) and `commanded_mask`
   (marvin's teacher command latched this scan, 0 when marvin isn't teaching) —
   for TX to the marvin coordinator over T1S. Returns true if staged, false if
   the link is down. `sample_seq` advances per call so the host detects frames
   dropped in transit. `commanded_mask` is the atomic edge-ai training label
   (paired with the ADC at the source, no cross-stream join). */
bool data_stream_send(uint8_t applied_mask, uint8_t commanded_mask);

#endif
