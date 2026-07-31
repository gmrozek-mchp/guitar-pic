#ifndef T1S_FOLLOWER_H
#define T1S_FOLLOWER_H

#include <stdbool.h>
#include <stdint.h>

/* 10BASE-T1S follower for the lemmy animation node (LAN8651 MAC-PHY on SERCOM0
 * SPI).
 *
 * marvin is the PLCA coordinator (node 0); this node is follower id 6. It syncs
 * the MAC-PHY as PLCA follower id 6/8, announces presence to the coordinator
 * (ethertype 0x88B6, node_type = 4 animation), and drives the two servos from
 * command frames (ethertype 0x88B5, payload = two int8 positions [neck, jaw]).
 * Transport is the vendored OPEN Alliance TC6 driver (third_party/oa-tc6-lib)
 * wrapped with the SERCOM0 SPI PLib, a GPIO chip-select held across each
 * transfer, the T1S_RST / T1S_IRQ_N pins (EIC EXTINT2), and a SysTick-based
 * millisecond clock. Bare-metal: the protocol is serviced from the main loop,
 * woken by IRQ_N. See firmware/lemmy/SPEC.md and docs/t1s-podl-link.md.
 *
 * Call T1SFollower_Initialize() once after SYS_Initialize (SERCOM0_SPI/EIC
 * brought up by MCC), then T1SFollower_Tasks() repeatedly from the main loop. */

void T1SFollower_Initialize(void);
void T1SFollower_Tasks(void);

/* True once the MAC-PHY is configured and the data path is enabled. */
bool T1SFollower_IsConnected(void);

/* Status accessors (for the CLI / diagnostics). */
uint8_t  T1SFollower_ChipRev(void);   /* 0 if the link never came up */
void     T1SFollower_LastCmd(int8_t *neck, int8_t *jaw);  /* last commanded positions; NULL args skipped */
void     T1SFollower_LastCtrl(uint8_t *op, uint8_t *arg, uint32_t *count); /* last 0x88B9 control cmd + count; NULL args skipped */
uint32_t T1SFollower_RxCount(void);   /* count of accepted command frames */
uint32_t T1SFollower_ErrCount(void);  /* count of TC6 errors since boot */

/* Diagnostic: raw-read the MAC-PHY ID registers and log the values (async). */
void T1SFollower_ReadId(void);

/* TC6 link state: protocol sync flag + TX/RX credit counters. NULL args skipped. */
void T1SFollower_GetState(bool *synced, uint8_t *txCredit, uint8_t *rxCredit);

/* Configured PLCA identity. */
uint8_t T1SFollower_NodeId(void);
uint8_t T1SFollower_NodeCount(void);

/* Diagnostic: read + log the PLCA status register (async). */
void T1SFollower_ReadPlca(void);

#endif /* T1S_FOLLOWER_H */
