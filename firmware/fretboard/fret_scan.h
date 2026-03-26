#ifndef FRET_SCAN_H
#define FRET_SCAN_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    FRET_GREEN,
    FRET_RED,
    FRET_YELLOW,
    FRET_BLUE,
    FRET_ORANGE,
    FRET_COUNT
} fret_channel_t;

void fret_scan_init(void);

/* Call repeatedly from main loop. Returns true when a full sweep
   of all channels has just completed. */
bool fret_scan_task(void);

uint16_t fret_scan_result(fret_channel_t ch);

/* Monotonically increasing counter of completed full sweeps. */
uint32_t fret_scan_sweep_count(void);

#endif
