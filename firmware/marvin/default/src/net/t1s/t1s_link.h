#ifndef T1S_LINK_H
#define T1S_LINK_H

#include <stdbool.h>
#include <stdint.h>

/* 10BASE-T1S inter-node link (marvin side, LAN8651 MAC-PHY over FLEXCOM3 SPI).
 *
 * marvin is the PLCA coordinator (node ID 0). The transport is the vendored
 * OPEN Alliance TC6 driver (third_party/oa-tc6-lib): SPI chunk protocol +
 * register control, wrapped here with the FLEXCOM3 SPI PLib, the T1S_RST /
 * T1S_IRQ_N GPIOs, and a FreeRTOS service task. See docs/t1s-podl-link.md.
 *
 * Phase 1/2 scope: bring the MAC-PHY up (reset, TC6_Init + TC6Regs_Init with
 * PLCA enabled) and confirm the link by reading the chip revision. L2 framing,
 * node-table demux, and the fretboard data path land in later phases.
 *
 * Initialize after SYS_Initialize (FLEXCOM3_SPI_Initialize must have run). The
 * call only creates the service task + semaphore; the reset pulse and TC6 init
 * run inside the task once the scheduler is up (TC6 init is asynchronous and
 * needs the SPI ISR + service loop). */

void T1SLink_Initialize(void);

/* True once the MAC-PHY has been configured and data path enabled. */
bool T1SLink_IsConnected(void);

/* TC6 link state (sync flag + TX/RX credits) and diagnostics, for the console. */
void     T1SLink_GetState(bool *synced, uint8_t *txCredit, uint8_t *rxCredit);
uint8_t  T1SLink_ChipRev(void);
uint8_t  T1SLink_NodeId(void);    /* PLCA coordinator id (0) */
uint8_t  T1SLink_NodeCount(void); /* configured PLCA node count */
uint32_t T1SLink_TxCount(void);   /* command frames sent */
uint32_t T1SLink_RxCount(void);   /* frames received from known nodes */
uint32_t T1SLink_ServiceOverruns(void); /* times service_pump hit its iter cap */

/* Per-node presence (from follower heartbeats, ethertype 0x88B6). */
typedef struct {
    uint8_t     node_id;
    const char *type;     /* "detector" / "guitar" / ... */
    bool        present;  /* a heartbeat was seen within the presence window */
    uint32_t    age_ms;   /* since the last heartbeat (0 if never seen) */
} T1SLink_NodeInfo;

uint8_t T1SLink_NodeTableCount(void);
bool    T1SLink_GetNodeInfo(uint8_t idx, T1SLink_NodeInfo *out);

/* Latest-wins 1-byte button command to the active guitar (actuator) node.
 * Safe to call from any task; the value is flushed onto the bus by the T1S
 * service task (TC6 access is single-threaded). Returns false if the link is
 * not up. */
bool T1SLink_SendToGuitar(uint8_t mask);

/* Delivers a received node payload (already demuxed by src MAC) to a consumer.
 * Called from the T1S service task. `detector_id` is the node's bus id. */
typedef void (*T1SLink_FrameHandler)(uint8_t detector_id, const uint8_t *payload,
                                     uint16_t len);
void T1SLink_SetFrameHandler(T1SLink_FrameHandler handler);

#endif /* T1S_LINK_H */
