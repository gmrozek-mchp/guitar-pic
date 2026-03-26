#ifndef DATA_STREAM_H
#define DATA_STREAM_H

#include <stdint.h>
#include <stdbool.h>
#include "fret_scan.h"

void data_stream_init(void);

/* Send one Data Visualizer Data Streamer frame with current fret results.
   Returns true if the frame was queued, false if UART buffer was too full. */
bool data_stream_send(void);

#endif
