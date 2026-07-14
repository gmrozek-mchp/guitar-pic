#ifndef BT_ROLE_H
#define BT_ROLE_H

#include <stdint.h>
#include <stdbool.h>

/* Classic-BT ACL role helpers for the link to the Wii.
 *
 * A real Wiimote is always the Bluetooth *slave* (the Wii is master of the
 * piconet and schedules all its Wiimotes). If fauxmote ends up master, the Wii
 * becomes a scatternet node once a second (real) Wiimote joins — its radio must
 * hop between piconets, which starves fauxmote's link and adds noticeable input
 * lag. These helpers read / correct the role.
 *
 * Isolated in its own translation unit because they call Bluedroid's internal
 * BTM_* API (stack/btm_api.h), which clashes with the public esp_* Bluetooth
 * headers if both are included in one file (same pattern as wiimote_sdp.c). */

/* ACL role for the link to `bda`: 0 = master, 1 = slave, 0xFF = unknown/no link. */
uint8_t BtRole_Get(const uint8_t *bda);

/* Request an ACL role switch (to_slave = become slave, like a real Wiimote).
 * Returns true if already in the target role or the switch was started; the
 * outcome is logged on completion. */
bool BtRole_Switch(const uint8_t *bda, bool to_slave);

#endif /* BT_ROLE_H */
