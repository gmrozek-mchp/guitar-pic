#ifndef T1S_DETECTOR_H
#define T1S_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>

/* 10BASE-T1S node for the fretboard (LAN8651 MAC-PHY on SERCOM0 SPI).
 *
 * marvin is the PLCA coordinator (node 0); this node is follower id 4. It both
 * senses and drives: it streams the 17-byte phototransistor data frame up to the
 * coordinator (ethertype 0x88B5) for logging, and sends the model's inferred
 * 1-byte button bitmask directly to the guitar node (id 3, ethertype 0x88B5) to
 * actuate — peer-to-peer, marvin coordinates/logs but is out of the command path.
 * Presence is announced with a heartbeat (0x88B6, node_type 1 = detector).
 *
 * marvin can gate this node remotely over the per-node control channel (ethertype
 * 0x88B9, unicast [opcode, arg]): opcode 0x01 arm (arg 0|1) — once a control frame
 * is received the remote arm state is authoritative over the local SW0 gate (this
 * is marvin's active-detector selection over the bus); opcode 0x02 stream (arg 0|1)
 * gates the 0x88B5 data feed to marvin, which boots disabled.
 *
 * Transport is the vendored OPEN Alliance TC6 driver (third_party/oa-tc6-lib)
 * wrapped with the SERCOM0 SPI PLib, a GPIO chip-select held across each transfer,
 * the T1S_RST / T1S_IRQ_N pins, and the SysTick millisecond clock. Bare-metal:
 * serviced from the main loop. See firmware/fretboard/SPEC.md and
 * docs/t1s-podl-link.md.
 *
 * Call T1SDetector_Initialize() once after SYS_Initialize (SERCOM0_SPI/EIC brought
 * up by MCC), then T1SDetector_Tasks() repeatedly from the main loop.
 * T1SDetector_SendFrame() stages a data frame (from the 240 Hz scan tick);
 * T1SDetector_SetCommand() stages the guitar command (from the main loop). */

void T1SDetector_Initialize(void);
void T1SDetector_Tasks(void);

/* True once the MAC-PHY is configured and the data path is enabled. */
bool T1SDetector_IsConnected(void);

/* Stage one assembled data-stream frame for TX to the coordinator (marvin). Safe
 * to call from the TC0 scan ISR: it copies the payload into a staging buffer and
 * flags it; T1SDetector_Tasks() flushes it from the main loop. Latest-wins — an
 * unsent staged frame is overwritten (a main loop that falls behind drops frames,
 * which the host detects as a sample_seq gap). Returns false if the link is down. */
bool T1SDetector_SendFrame(const uint8_t *payload, uint16_t len);

/* Set the latest 1-byte button command to drive the guitar node, gated by the
 * actuation-armed state. Call from the main loop (not the ISR). While `active`,
 * the command is (re)sent to the guitar by T1SDetector_Tasks() on change and every
 * ~50 ms so a dropped frame self-heals. When `active` goes false, one final
 * all-released frame is queued (clearing any held note) and the node then stays
 * silent on the command channel — so a disarmed node never contends for the guitar
 * with another command source (e.g. marvin). */
void T1SDetector_SetCommand(uint8_t mask, bool active);

/* Remote arm state from the control channel (0x88B9, opcode 0x01). *valid is set
 * false until the first control frame arrives; while false the local SW0 gate
 * governs actuation, and once true the returned remote state is authoritative.
 * Read from the main loop (the actuation gate). */
bool T1SDetector_RemoteArm(bool *valid);

/* Last control frame applied (op/arg) + accepted-control count, for the CLI.
 * NULL args are skipped. */
void T1SDetector_LastCtrl(uint8_t *op, uint8_t *arg, uint32_t *count);

/* Data-stream gate (control channel 0x88B9, opcode 0x02). Boots disabled; while
 * false no 0x88B5 data frames are sent to marvin. */
bool T1SDetector_StreamEnabled(void);

/* Diagnostics (boot banner / CLI). */
uint8_t  T1SDetector_ChipRev(void);   /* 0 if the link never came up */
uint32_t T1SDetector_TxCount(void);   /* data frames sent to the coordinator */
uint32_t T1SDetector_CmdCount(void);  /* command frames sent to the guitar */
uint8_t  T1SDetector_LastCmd(void);   /* most recent command bitmask sent */
uint32_t T1SDetector_ErrCount(void);  /* TC6 errors since boot */

/* TC6 link state: protocol sync flag + TX/RX credit counters. NULL args skipped. */
void T1SDetector_GetState(bool *synced, uint8_t *txCredit, uint8_t *rxCredit);

/* Configured PLCA identity. */
uint8_t T1SDetector_NodeId(void);
uint8_t T1SDetector_NodeCount(void);

/* Diagnostics: raw-read + log the MAC-PHY ID registers / PLCA status (async —
 * the values log from the main-loop service a moment later; enqueue-only, so
 * they don't hammer TC6_Service on a live link). */
void T1SDetector_ReadId(void);
void T1SDetector_ReadPlca(void);

#endif /* T1S_DETECTOR_H */
