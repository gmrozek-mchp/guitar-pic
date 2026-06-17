#ifndef CLI_H
#define CLI_H

/* Operator CLI on the SERCOM1 debug UART (embedded-cli, static allocation).
 * SERCOM1 is free for an interactive console (the data stream is on the T1S bus).
 * Bare-metal: CLI_Tasks() polls the RX ring each main-loop pass — no task.
 *
 * Commands: t1s (link/sync/chipRev/PLCA/counters, data+command tx), adc (current
 * scan), id, plca.
 *
 * Call CLI_Initialize() after SYS_Initialize + T1SDetector_Initialize (SERCOM1
 * brought up by MCC), then CLI_Tasks() repeatedly from the main loop. */

void CLI_Initialize(void);
void CLI_Tasks(void);

#endif /* CLI_H */
