#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Bring up the Bluetooth-Classic controller + Bluedroid, register the Wiimote SDP
 * record, and start listening on the HID L2CAP PSMs. Boots idle (not discoverable)
 * — call Fauxmote_EnterPairing() to open the sync window. */
void Fauxmote_BtStart(void);

void Fauxmote_EnterPairing(void);   /* become limited-discoverable so a Wii can sync to us */
void Fauxmote_StopPairing(void);    /* leave discoverable/connectable (idle) */
void Fauxmote_Reconnect(void);      /* device-initiated reconnect to the last bonded Wii */
void Fauxmote_Unlink(void);         /* erase the bond (link key) from NVS */

bool Fauxmote_IsDiscoverable(void);
const uint8_t *Fauxmote_WiiAddr(void);   /* last bonded Wii address, or NULL */
