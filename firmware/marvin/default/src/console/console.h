#ifndef MARVIN_CONSOLE_H
#define MARVIN_CONSOLE_H

/* Interactive operator console — FLEXCOM2 USART (ring-buffer), 115200 8N1.
 *
 * A human-facing command line (embedded-cli) on a channel separate from the
 * DBGU log/printf chatter: log output stays on DBGU, console I/O stays on
 * FLEXCOM2. Commands set the operating-mode toggles (detect / timing / manual),
 * select the active detector, drive manual fret/strum actuation, and print a
 * status snapshot — see the binding table in console.c.
 *
 * One FreeRTOS task owns the link: it drains the FLEXCOM2 RX ring (woken by a
 * read-threshold notification), feeds bytes to embedded-cli, and runs the line
 * editor / dispatcher. All state is statically allocated; embedded-cli is given
 * a fixed buffer so no malloc is exercised.
 *
 * Call after SYS_Initialize so the FLEXCOM2 peripheral is up, and after the
 * actuator/detector modules so their setters are ready to be driven. */

void Console_Initialize(void);

#endif
