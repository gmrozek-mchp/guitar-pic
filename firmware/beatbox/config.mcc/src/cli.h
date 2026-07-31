#ifndef CLI_H
#define CLI_H

/* Operator CLI on the UART2 debug console (embedded-cli, static allocation).
 * Bare-metal: CLI_Tasks() polls the RX ring each main-loop pass — no task.
 * Commands: info, reset. Peripheral commands (rgb, pot, t1s, beat) are added as
 * those subsystems land.
 *
 * Call CLI_Initialize() after SYSTEM_Initialize (UART2 brought up by MCC), then
 * CLI_Tasks() repeatedly from the main loop. */

void CLI_Initialize(void);
void CLI_Tasks(void);

#endif /* CLI_H */
