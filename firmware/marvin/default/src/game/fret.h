#ifndef GAME_FRET_H
#define GAME_FRET_H

/* Guitar Hero / Rock Band fret colors — a game-domain primitive shared by
 * detector, actuator, and UI layers. Lives here (not under detector/ or
 * actuator/) so neither has to depend on the other for the type. The
 * future game-state controller (spec §4.8) lands in this same namespace. */

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
