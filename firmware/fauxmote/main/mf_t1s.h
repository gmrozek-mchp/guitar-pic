#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Diagnostics for the T1S transport, read by the console CLI. Only meaningful
 * when the T1S transport is compiled (CONFIG_FAUXMOTE_LINK_TRANSPORT_T1S). The
 * transport itself is started through MarvinLink_Start() (marvin_link.h). */

bool     MfT1s_IsUp(void);        /* PLCA / MAC-PHY link initialized */
bool     MfT1s_IsSynced(void);    /* PLCA synced to the coordinator's beacon */
uint8_t  MfT1s_NodeId(void);
uint8_t  MfT1s_ChipRev(void);
uint32_t MfT1s_RxCount(void);     /* mf-channel frames received */
uint32_t MfT1s_TxCount(void);     /* frames sent (STATUS + heartbeat) */
uint32_t MfT1s_ErrCount(void);
uint32_t MfT1s_HbSeq(void);       /* last heartbeat sequence number */
