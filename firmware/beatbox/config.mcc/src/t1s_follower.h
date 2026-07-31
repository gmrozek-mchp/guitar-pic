#ifndef T1S_FOLLOWER_H
#define T1S_FOLLOWER_H

#include <stdbool.h>
#include <stdint.h>

/* 10BASE-T1S follower for the beatbox node (LAN8651 MAC-PHY on SPI1).
 *
 * marvin is the PLCA coordinator (node 0); this node is follower id 5. beatbox
 * is the beat-source publisher: it announces presence to the coordinator and
 * (from milestone B4) publishes position commands to lemmy + a beat frame to
 * lightshow. It drives no actuator on the bus, so received frames are only
 * counted for diagnostics at this stage. Transport is the vendored OPEN
 * Alliance TC6 driver (third_party/oa-tc6-lib) wrapped with the blocking SPI1
 * host driver, a GPIO chip-select held across each transfer, the T1S_RST /
 * T1S_IRQ_N pins (change-notice IRQ), and a TMR1-based millisecond clock.
 * Bare-metal: the protocol is serviced from the main loop, woken by IRQ_N. See
 * firmware/beatbox/SPEC.md and docs/t1s-podl-link.md.
 *
 * Call T1SFollower_Initialize() once after SYSTEM_Initialize (SPI1/TMR1/pins
 * brought up by MCC), then T1SFollower_Tasks() repeatedly from the main loop. */

void T1SFollower_Initialize(void);
void T1SFollower_Tasks(void);

/* True while PLCA is operating — the coordinator's beacon is on the wire and
 * the node can actually transmit/receive. This is the real on-bus signal;
 * refreshed by a periodic background read of PLCA_STATUS. Distinct from
 * IsInitialized (which is local config only and does not imply a live link). */
bool T1SFollower_IsConnected(void);

/* True once the MAC-PHY register bring-up completed and the data path is
 * enabled. Local state — set even with nothing connected to the SPI MAC-PHY. */
bool T1SFollower_IsInitialized(void);

/* Status accessors (for the CLI / diagnostics). */
uint8_t  T1SFollower_ChipRev(void);   /* 0 if the bring-up never completed */
uint8_t  T1SFollower_LastCmd(void);   /* first payload byte of the last accepted frame */
uint32_t T1SFollower_RxCount(void);   /* count of accepted data frames */
uint32_t T1SFollower_ErrCount(void);  /* count of TC6 errors since boot */

/* Publish the compact lightshow beat frame as a broadcast (ethertype 0x88B8,
 * dst FF:FF:FF:FF:FF:FF) so lightshow — and any later consumer — receives it in
 * one transmit opportunity. payload is the serialized frame (see publish.h).
 * Returns true if it was queued; false if the bus isn't operating or a prior
 * beat frame is still in flight (the frame is dropped — its seq lets the
 * consumer spot the gap). Safe to call at the ~23 Hz frame rate. */
bool T1SFollower_SendBeatFrame(const uint8_t *payload, uint16_t len);

uint32_t T1SFollower_BeatTxCount(void); /* count of beat frames queued to the bus */

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
