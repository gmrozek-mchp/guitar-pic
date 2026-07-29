#ifndef CLI_H
#define CLI_H

/* Operator CLI on the SERCOM1 debug UART (embedded-cli, static allocation).
 * Bare-metal: CLI_Tasks() polls the RX ring each main-loop pass — no task.
 * Commands: info, t1s / id / plca (T1S link + MAC-PHY diagnostics), servo (raw
 * pulse-width), pos (calibrated position), cal (servo calibration), reset. The
 * nod/jaw motion-envelope commands are added when that layer lands.
 *
 * Call CLI_Initialize() after SYS_Initialize (SERCOM1 brought up by MCC), then
 * CLI_Tasks() repeatedly from the main loop. */

void CLI_Initialize(void);
void CLI_Tasks(void);

#endif /* CLI_H */
