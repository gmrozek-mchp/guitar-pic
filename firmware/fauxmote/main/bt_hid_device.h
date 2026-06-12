#pragma once

/* Bring up the Bluetooth-Classic controller + Bluedroid and start the HID device
 * advertising the Wiimote identity (see firmware/fauxmote/docs/journal.md). */
void Fauxmote_BtStart(void);
