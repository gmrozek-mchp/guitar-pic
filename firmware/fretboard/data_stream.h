#ifndef DATA_STREAM_H
#define DATA_STREAM_H

#include <stdint.h>
#include <stdbool.h>
#include "fret_scan.h"

void data_stream_init(void);

/* Send one Data Visualizer Data Streamer frame with current fret results.
   Returns true if the frame was queued, false if UART buffer was too full. */
bool data_stream_send(void);

/* MODEL_DRIVEN variant: appends the running model inference count (21-byte frame)
   so the host can measure the actual inference rate vs the 240 Hz sample rate. */
bool data_stream_send_model(uint32_t infer_count);

#endif
