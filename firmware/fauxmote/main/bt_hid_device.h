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

/* The Wii's one-time sync screen (1+2 on a real remote) instead of the red SYNC button.
 * Same handshake but the PIN is our own BD_ADDR reversed rather than the Wii's, and the
 * Wii does not bond — it hands out a player slot for this session only, without touching
 * its permanent registration table. Refuses while discoverable or connected, so re-slotting
 * a live link means Fauxmote_Disconnect() first.
 *
 * scan_general: use general instead of limited discovery. Whether the one-time sync screen
 * scans with a limited inquiry (as the red-SYNC scan does) is unverified on hardware; if
 * fauxmote is never found, that is the thing to try before suspecting the PIN. */
void Fauxmote_EnterPairingTemp(bool scan_general);

/* Link teardown, cheapest first. A teardown already restarts the L2CAP layer on its own
 * (a session's leftovers otherwise make the *next* connect look healthy while the Wii
 * ignores it — see docs/journal.md), so a plain Fauxmote_Reconnect() is the normal
 * recovery and these are for when it isn't enough.
 *
 * Disconnect: close both HID channels. Leaves us idle, bonded and reconnectable.
 * BtReset:    the above, plus an explicit L2CAP deinit + re-init.
 * Reboot:     restart the ESP32. Last resort. The bond lives in NVS and survives. */
void Fauxmote_Disconnect(void);
void Fauxmote_BtReset(void);
void Fauxmote_Reboot(void);

bool Fauxmote_IsDiscoverable(void);
bool Fauxmote_IsConnecting(void);        /* a device-initiated reconnect is in flight */
bool Fauxmote_IsPairingTemp(void);       /* the armed window is the temporary/guest flow */
const uint8_t *Fauxmote_WiiAddr(void);   /* last bonded Wii address, or NULL */

/* HID L2CAP channels the stack has opened and not yet finished closing. Channels stuck in
 * "closing" are a teardown that never completed — the state that stops us reconnecting. */
int Fauxmote_ChannelsOpen(void);
int Fauxmote_ChannelsClosing(void);
