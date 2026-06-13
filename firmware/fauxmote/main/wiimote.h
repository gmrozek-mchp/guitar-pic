#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Wiimote report state machine: answers the Wii's HID output reports and streams
 * input reports. See docs/journal.md (Phase 2) and wiibrew "Wiimote". */
void Wiimote_Start(void);                              /* init EEPROM + start the input-report sender */
void Wiimote_HandleRx(int fd, const uint8_t *data, int len);  /* feed one HIDP frame from the Wii */
void Wiimote_NotifyDisconnected(void);                 /* link dropped: stop streaming + reset state */

bool Wiimote_IsConnected(void);                        /* HID data channel open */
bool Wiimote_IsAssigned(void);                         /* Wii assigned a player slot (0x11 received) */
int  Wiimote_PlayerSlot(void);                         /* assigned player 1..4, or 0 if none */
uint8_t Wiimote_ReportMode(void);                      /* report ID the Wii last requested (0x12) */

/* Set a core button by name (a, b, one, two, plus, minus, home, up, down, left,
 * right). Returns false if the name is unknown. */
bool Wiimote_SetButton(const char *name, bool pressed);

/* Press a button now and auto-release it shortly after (non-blocking). */
bool Wiimote_TapButton(const char *name);
