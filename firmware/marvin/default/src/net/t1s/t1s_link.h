#ifndef T1S_LINK_H
#define T1S_LINK_H

#include <stdbool.h>
#include <stdint.h>

/* 10BASE-T1S inter-node link (marvin side, LAN8651 MAC-PHY over FLEXCOM4 SPI).
 *
 * marvin is the PLCA coordinator (node ID 0). The transport is the vendored
 * OPEN Alliance TC6 driver (third_party/oa-tc6-lib): SPI chunk protocol +
 * register control, wrapped here with the FLEXCOM4 SPI PLib, the T1S_RST /
 * T1S_IRQ_N GPIOs, and a FreeRTOS service task. See docs/t1s-podl-link.md.
 *
 * Phase 1/2 scope: bring the MAC-PHY up (reset, TC6_Init + TC6Regs_Init with
 * PLCA enabled) and confirm the link by reading the chip revision. L2 framing,
 * node-table demux, and the fretboard data path land in later phases.
 *
 * Initialize after SYS_Initialize (FLEXCOM4_SPI_Initialize must have run). The
 * call only creates the service task + semaphore; the reset pulse and TC6 init
 * run inside the task once the scheduler is up (TC6 init is asynchronous and
 * needs the SPI ISR + service loop). */

void T1SLink_Initialize(void);

/* True once the MAC-PHY has been configured and data path enabled. */
bool T1SLink_IsConnected(void);

#endif /* T1S_LINK_H */
