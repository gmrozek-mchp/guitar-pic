#ifndef T1S_FOLLOWER_H
#define T1S_FOLLOWER_H

#include <stdbool.h>
#include <stdint.h>

/* 10BASE-T1S follower for the guitar node (LAN8651 MAC-PHY on SERCOM0 SPI).
 *
 * marvin is the PLCA coordinator (node 0); this node is follower id 2. It
 * receives marvin's 1-byte button bitmask (ethertype 0x88B5) over T1S and
 * drives the 7 Wii-guitar GPIOs. Transport is the vendored OPEN Alliance TC6
 * driver (third_party/oa-tc6-lib) wrapped with the SERCOM0 SPI PLib, a
 * GPIO chip-select held across each transfer, the T1S_RST / T1S_IRQ_N pins
 * (EIC EXTINT13), and a SysTick-based millisecond clock. Bare-metal: the
 * protocol is serviced from the main loop, woken by IRQ_N. See
 * firmware/guitar/SPEC.md and docs/t1s-podl-link.md.
 *
 * Call T1SFollower_Initialize() once after SYS_Initialize (SERCOM0_SPI/EIC
 * brought up by MCC), then T1SFollower_Tasks() repeatedly from the main loop. */

void T1SFollower_Initialize(void);
void T1SFollower_Tasks(void);

/* True once the MAC-PHY is configured and the data path is enabled. */
bool T1SFollower_IsConnected(void);

/* Status accessors (for the CLI / diagnostics). */
uint8_t  T1SFollower_ChipRev(void);   /* 0 if the link never came up */
uint8_t  T1SFollower_LastCmd(void);   /* most recent applied button bitmask */
uint32_t T1SFollower_RxCount(void);   /* count of accepted command frames */
uint32_t T1SFollower_ErrCount(void);  /* count of TC6 errors since boot */

/* Drive the Wii-guitar GPIOs directly (manual test, e.g. the CLI's btn/tap).
 * Note: a subsequent T1S command will overwrite this. */
void T1SFollower_ApplyButtons(uint8_t mask);
void T1SFollower_ReleaseButtons(void);

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
