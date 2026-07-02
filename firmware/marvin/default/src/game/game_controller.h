#ifndef MARVIN_GAME_CONTROLLER_H
#define MARVIN_GAME_CONTROLLER_H

#include <stdbool.h>

/* M10 — GH3 game-state controller. On Start it reads the committed Selection
 * (game/selection.h), navigates the Training/Practice menu path to a FULL SONG /
 * FULL SPEED playthrough of the selected song at the selected difficulty, then
 * enables the CV timing pipeline so the detector plays it (the timing pipeline's
 * in-song gate arms actuation automatically once the observer sees GP_SCREEN_in_song).
 *
 * Closed-loop and observation-driven: every step verifies the screen the gameplay
 * engine reports and recovers with RED on a mismatch — a port of the offline
 * NavController in tools/gameplay. Menu inputs are guitar fret/strum masks sent via
 * FretboardLink_Send, so they drive both the T1S guitar and fauxmote. See
 * firmware/marvin/docs/gh3_navigation.md.
 *
 * Initialize once in App_StartServices, after the gameplay engine, timing pipeline,
 * and manual_control (it drives their setters). */

void GameController_Initialize(void);   /* spawn the controller task (idle) */
void GameController_Start(void);        /* begin a run from the committed Selection */
void GameController_Stop(void);         /* abort/stop: release the guitar, CV off */
bool GameController_IsBusy(void);       /* true while a run is navigating/playing */

/* Single status-text observer (e.g. the dashboard Status label), invoked on each
 * phase change with a short string ("READY"/"NAVIGATING"/"PLAYING"/"FAILED"/…).
 * NULL clears it. Mirrors Selection_SetObserver. */
void GameController_SetStatusObserver(void (*cb)(const char *text));

#endif /* MARVIN_GAME_CONTROLLER_H */
