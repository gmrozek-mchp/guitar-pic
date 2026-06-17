#ifndef CLI_H
#define CLI_H

/* Operator CLI on the SERCOM1 debug UART (embedded-cli, static allocation).
 * Bare-metal: CLI_Tasks() polls the RX ring each main-loop pass — no task.
 * Commands: status, btn <mask>, tap <mask> [ms] (manual Wii-guitar actuation,
 * useful for verifying the controller wiring before the T1S link is up).
 *
 * Call CLI_Initialize() after SYS_Initialize (SERCOM1 brought up by MCC), then
 * CLI_Tasks() repeatedly from the main loop. */

void CLI_Initialize(void);
void CLI_Tasks(void);

#endif /* CLI_H */
