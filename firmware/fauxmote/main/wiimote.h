#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Wiimote report state machine: answers the Wii's HID output reports and streams
 * input reports. See docs/journal.md (Phase 2) and wiibrew "Wiimote". */
void Wiimote_Start(void);                              /* init EEPROM + start the input-report sender */
void Wiimote_HandleRx(int fd, const uint8_t *data, int len);  /* feed one HIDP frame from the Wii */
void Wiimote_NotifyDisconnected(void);                 /* link dropped: stop streaming + reset state */

/* Adopt fd as the HID data channel and start streaming at once, without waiting for the
 * Wii to send anything. Used on a device-initiated reconnect, where the Wii may never
 * re-run its init sequence — a real Wiimote reports as soon as its link is up. */
void Wiimote_NotifyConnected(int fd);

bool Wiimote_IsConnected(void);                        /* HID data channel open */
bool Wiimote_IsAssigned(void);                         /* Wii assigned a player slot (0x11 received) */
int  Wiimote_PlayerSlot(void);                         /* assigned player 1..4, or 0 if none */
uint8_t Wiimote_ReportMode(void);                      /* report ID the Wii last requested (0x12) */

/* Link liveness. MsSinceRx counts from the last frame from the Wii, or from link-up on a
 * channel adopted without one, and is UINT32_MAX while disconnected — so a large value
 * with the channel open means the Wii is ignoring us. TxStallMs is how long the current
 * write() has been blocked (0 = no write in flight). */
uint32_t Wiimote_MsSinceRx(void);
uint32_t Wiimote_TxStallMs(void);
int      Wiimote_DataFd(void);                         /* data-channel fd, or -1 */

/* Set a core button by name (a, b, one, two, plus, minus, home, up, down, left,
 * right). Returns false if the name is unknown. */
bool Wiimote_SetButton(const char *name, bool pressed);

/* Press a button now and auto-release it shortly after (non-blocking). */
bool Wiimote_TapButton(const char *name);

/* IR pointer position, 0..1 with (0,0) = top-left. Used by every IR-bearing
 * reporting mode. ClearPointer reports no IR dots (cursor off-screen). */
void Wiimote_SetPointer(float x, float y);
void Wiimote_ClearPointer(void);

/* Accelerometer, in signed 1/32-g units per axis (+1 g = +32; X/Y/Z). Rendered into
 * every accel-bearing reporting mode, translated to raw report bytes via this device's
 * advertised calibration. ClearAccel returns to level (0, 0, +1 g), the safe default. */
void Wiimote_SetAccel(int8_t x, int8_t y, int8_t z);
void Wiimote_ClearAccel(void);

/* Extension: a registered extension (e.g. the guitar) also handles its button names
 * via Wiimote_SetButton/TapButton. SetExtension reports it attached/detached. */
void Wiimote_SetExtension(bool connected);
bool Wiimote_ExtAttached(void);                        /* extension reported attached */
