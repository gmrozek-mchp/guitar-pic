#include <stdint.h>
#include <stdbool.h>

#include "esp_log.h"

#include "stack/bt_types.h"
#include "stack/btm_api.h"

#include "bt_role.h"

static const char *TAG = "fauxmote.role";

static const char *role_name(uint8_t r)
{
    switch (r) {
    case BTM_ROLE_MASTER: return "MASTER";
    case BTM_ROLE_SLAVE:  return "SLAVE";
    default:              return "UNKNOWN";
    }
}

static void role_switch_cmpl(void *p)
{
    const tBTM_ROLE_SWITCH_CMPL *r = (const tBTM_ROLE_SWITCH_CMPL *)p;
    if (r == NULL) { return; }
    ESP_LOGW(TAG, "role switch complete: now %s (hci_status=0x%02x)",
             role_name(r->role), r->hci_status);
}

uint8_t BtRole_Get(const uint8_t *bda)
{
    UINT8 role = BTM_ROLE_UNDEFINED;
    if (BTM_GetRole((UINT8 *)bda, &role) != BTM_SUCCESS) {
        return 0xFFu;
    }
    return (uint8_t)role;
}

bool BtRole_Switch(const uint8_t *bda, bool to_slave)
{
    UINT8 want = to_slave ? BTM_ROLE_SLAVE : BTM_ROLE_MASTER;
    tBTM_STATUS st = BTM_SwitchRole((UINT8 *)bda, want, role_switch_cmpl);
    ESP_LOGW(TAG, "role switch to %s: st=%d", role_name(want), (int)st);
    /* BTM_SUCCESS = already in role; BTM_CMD_STARTED = switch issued to controller. */
    return (st == BTM_SUCCESS || st == BTM_CMD_STARTED);
}
