#ifndef DATA_STREAM_H
#define DATA_STREAM_H

#include <stdint.h>
#include <stdbool.h>
#include "fret_scan.h"

void data_stream_init(void);

/* Stage one 17-byte detector frame — the current ADC scan plus `applied_mask`
   (the bitmask currently driven to the guitar) — for TX to the marvin coordinator
   over T1S. Returns true if staged, false if the link is down. `sample_seq`
   advances per call so the host detects frames dropped in transit. */
bool data_stream_send(uint8_t applied_mask);

#endif
