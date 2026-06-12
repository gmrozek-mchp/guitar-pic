#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Wiimote report state machine: answers the Wii's HID output reports and streams
 * input reports. See docs/journal.md (Phase 2) and wiibrew "Wiimote". */
void Wiimote_Start(void);                              /* init EEPROM + start the input-report sender */
void Wiimote_HandleRx(int fd, const uint8_t *data, int len);  /* feed one HIDP frame from the Wii */
void Wiimote_NotifyFdClosed(int fd);                   /* a HID channel closed */

bool Wiimote_IsConnected(void);                        /* HID data channel open */
bool Wiimote_IsAssigned(void);                         /* Wii assigned a player slot (0x11 received) */
