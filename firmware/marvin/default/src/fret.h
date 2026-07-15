#ifndef MARVIN_FRET_H
#define MARVIN_FRET_H

/* Guitar Hero / Rock Band fret colors — a cross-cutting domain primitive shared
 * by the detector, actuator, timing, perf-log, and game layers. Lives at the src
 * root (not under any one subsystem) so none of them has to depend on another for
 * the type. */

typedef enum
{
    FRET_GREEN  = 0,
    FRET_RED    = 1,
    FRET_YELLOW = 2,
    FRET_BLUE   = 3,
    FRET_ORANGE = 4,
    FRET_COUNT  = 5,
} fret_t;

#endif
