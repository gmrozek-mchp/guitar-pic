#ifndef ACTUATOR_ENABLE_H
#define ACTUATOR_ENABLE_H

#include <stdbool.h>

#include "net/t1s/t1s_link.h"   /* t1s_actuator_t */

/* Operator-facing output enable for the three actuator nodes — guitar (button
 * GPIOs), lemmy (servos), lightshow (LEDs). Marvin owns the wanted state; each
 * node owns the gate itself and reports it back in its heartbeat, so "enabled"
 * means the node confirmed it rather than that marvin asked.
 *
 * Gating at the node, not on marvin's send path, is what makes it hold in every
 * window: the guitar is also driven peer-to-peer by the fretboard while a song is
 * running (Detector_FretboardDriving), and lemmy's servos are also driven by
 * beatbox — neither passes through marvin.
 *
 * The commanded value is pushed over each node's 0x88B9 control channel. There is
 * no retry here: t1s_link re-pushes on its own whenever a node's heartbeat
 * disagrees with what it was last told, which also covers a node rebooting or
 * having its local CLI poked.
 *
 * All three default enabled, so the rig plays after a cold boot with no operator
 * interaction. Initialize after FretboardLink_Initialize (which brings up the T1S
 * link); the first push happens before the link is up and is delivered by that
 * reconcile on each node's first heartbeat.
 *
 * Two surfaces drive this: the dashboard ACTUATORS toggles and the console's
 * `guitar` / `lemmy output` / `lightshow` commands. */

void ActuatorEnable_Initialize(void);

/* What marvin wants. */
bool ActuatorEnable_Get(t1s_actuator_t act);
void ActuatorEnable_Set(t1s_actuator_t act, bool on);

/* What the node says (heartbeat flags bit1), and whether it is on the bus at all.
 * Reported is meaningless while !Present. */
bool ActuatorEnable_Reported(t1s_actuator_t act);
bool ActuatorEnable_Present(t1s_actuator_t act);

/* Commanded but not yet confirmed by a present node — normally the sub-heartbeat
 * gap after a toggle, and the UI's cue that the state is in flight. */
bool ActuatorEnable_Pending(t1s_actuator_t act);

/* "guitar" / "lemmy" / "lightshow", for console output. */
const char *ActuatorEnable_Name(t1s_actuator_t act);

#endif
