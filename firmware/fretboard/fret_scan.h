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

/* Blocking scan of all channels. Call at the desired sample rate. */
void fret_scan_all(void);

uint16_t fret_scan_result(fret_channel_t ch);

#endif
