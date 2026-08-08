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
uint32_t T1SLink_RxCount(void);   /* detector frames received from known nodes */
uint32_t T1SLink_ServiceOverruns(void); /* times service_pump hit its iter cap */
uint32_t T1SLink_CtrlTxCount(void); /* controller (0x88B7) frames sent */
uint32_t T1SLink_CtrlRxCount(void); /* controller (0x88B7) frames received */

/* Per-node presence (from follower heartbeats, ethertype 0x88B6). */
typedef struct {
    uint8_t     node_id;
    const char *type;     /* "fretboard" / "guitar" / "fauxmote1" / ... */
    bool        present;  /* a heartbeat was seen within the presence window */
    uint32_t    age_ms;   /* since the last heartbeat (0 if never seen) */
} T1SLink_NodeInfo;

uint8_t T1SLink_NodeTableCount(void);
bool    T1SLink_GetNodeInfo(uint8_t idx, T1SLink_NodeInfo *out);

/* Per-node bus statistics for the diagnostics UI / console. Extends NodeInfo with
 * the traffic + error telemetry a node reports in its extended heartbeat (v2): a
 * legacy (8-byte) heartbeat leaves the tx/rx/err fields 0. Rates are frames/s,
 * recomputed once a second on the coordinator from the reported cumulative deltas
 * (so they only advance as fast as the heartbeat, ~1 Hz). */
typedef struct {
    uint8_t     node_id;
    const char *type;
    bool        present;
    uint32_t    age_ms;      /* since the last heartbeat (also shown as "latency") */
    uint32_t    tx_count;    /* cumulative frames the node has sent     */
    uint32_t    rx_count;    /* cumulative frames the node has received */
    uint32_t    tx_rate;     /* frames/s (delta over the last second)   */
    uint32_t    rx_rate;
    uint16_t    crc_err;     /* cumulative FCS errors at the node       */
    uint16_t    sym_err;     /* cumulative framing (symbol) errors      */
} T1SLink_NodeStats;

bool T1SLink_GetNodeStats(uint8_t idx, T1SLink_NodeStats *out);

/* marvin's own (coordinator) row, filled from its local counters: node_id 0, type
 * "marvin", always present. tx/rx are the sum of its data + controller channels;
 * crc/sym are its own MAC-PHY FCS / framing events. */
bool T1SLink_GetSelfStats(T1SLink_NodeStats *out);

/* Aggregate bus statistics (marvin + all follower rows) for the header tiles.
 *
 * The occupancy figures are measured from wire byte counts, not derived from the
 * per-node frame rates: marvin's MAC is promiscuous, so it observes every frame
 * on the segment directly, and summing node frame rates would count each frame
 * once per party to it. util_permille is the share of line time spent carrying
 * frames — on a PLCA bus the rest goes to the BEACON and the mandated silences
 * whether or not anyone is talking, so an idle bus reads near zero and
 * to_used_permille is the figure that answers "how much room is left".
 * err_rate_ppm = total errors / total frames.
 *
 * wire_bps counts the frame *plus* its 8-byte preamble/SFD, which the MAC
 * generates and no frame length includes — so util_permille is exactly
 * wire_bps · 8 / 10 Mbit, and the two reconcile by hand. */
typedef struct {
    uint32_t util_permille;  /* 0..1000, line time carrying frames */
    uint32_t wire_bps;       /* wire bytes/s incl. preamble, whole segment */
    uint32_t plca_cycles;    /* PLCA bus cycles/s            */
    uint32_t to_used_permille; /* transmit opportunities carrying a frame */
    uint32_t tx_total;       /* frames sent, bus-wide        */
    uint32_t rx_total;       /* frames received, bus-wide    */
    uint32_t crc_total;
    uint32_t sym_total;
    uint32_t err_rate_ppm;   /* errors per million frames    */
    uint32_t uptime_s;       /* since T1SLink_Initialize      */
    uint8_t  nodes_online;   /* present nodes incl. marvin    */
    uint8_t  nodes_total;    /* table + marvin                */
} T1SLink_BusStats;

bool T1SLink_GetBusStats(T1SLink_BusStats *out);

/* Zero every traffic/error counter and restart the uptime window, so the whole
 * bus view reads "since this call". Node counters are the followers' own lifetime
 * totals, so this re-arms their baselines instead: each node re-zeroes on its next
 * heartbeat. Presence survives — this forgets traffic, not who is on the bus.
 * Safe from any task. */
void T1SLink_ResetCounters(void);

/* Latest-wins 1-byte button command to the active guitar (actuator) node.
 * Safe to call from any task; the value is flushed onto the bus by the T1S
 * service task (TC6 access is single-threaded). Returns false if the link is
 * not up. */
bool T1SLink_SendToGuitar(uint8_t mask);

/* Latest-wins combined [neck, jaw] position command to lemmy (animation node),
 * each a signed -127..127 (0 = neutral). Same threading contract as
 * T1SLink_SendToGuitar; returns false if the link is not up. */
bool T1SLink_SendToLemmy(int8_t neck, int8_t jaw);

/* Lemmy control channel (ethertype 0x88B9): typed [opcode, arg] commands that
 * tune the beat nod remotely. Staged per-opcode and flushed by the T1S service
 * task; same threading contract as T1SLink_SendToLemmy. Returns false if the
 * link is not up or the opcode is unknown. */
#define T1S_ANIM_CTRL_NOD_EN     (1u)  /* arg 0|1  : enable/disable the nod        */
#define T1S_ANIM_CTRL_NOD_TRIM   (2u)  /* arg int8 : nod trim / pot offset         */
#define T1S_ANIM_CTRL_NOD_OSC    (3u)  /* arg 0|1  : oscillator (beat-only vs osc) */
#define T1S_ANIM_CTRL_OP_COUNT   (3u)
bool T1SLink_SendLemmyCtrl(uint8_t opcode, uint8_t arg);

/* Lightshow control channel — same 0x88B9 transport + [opcode, arg] grammar as
 * the lemmy control channel, routed to the lightshow node with its own opcode
 * namespace. Same threading contract; returns false if the link is down or the
 * opcode is unknown. */
#define T1S_LIGHT_CTRL_OUTPUT_EN (1u)  /* arg 0|1  : enable/disable the LED output */
#define T1S_LIGHT_CTRL_OP_COUNT  (1u)
bool T1SLink_SendLightshowCtrl(uint8_t opcode, uint8_t arg);

/* Fretboard (detector) control channel — same 0x88B9 transport + [opcode, arg]
 * grammar, routed to the detector node with its own opcode namespace. Arm gates
 * the detector's actuation remotely (active-detector selection over the bus);
 * once received it is authoritative over the node's local SW0. Teacher pushes
 * marvin's CV command so the detector can stamp it into its data frames as the
 * atomic edge-ai training label (commanded_mask) — sent while marvin is the CV
 * teacher during a capture. Same threading contract; returns false if the link
 * is down or the opcode is unknown. */
#define T1S_DET_CTRL_ARM         (1u)  /* arg 0|1  : arm/disarm the actuation gate */
#define T1S_DET_CTRL_STREAM      (2u)  /* arg 0|1  : gate the data stream to marvin */
#define T1S_DET_CTRL_MODEL       (3u)  /* arg 0..4 : inference model select (per difficulty + auto) */
#define T1S_DET_CTRL_TEACHER     (4u)  /* arg mask : CV teacher command, latched as the edge-ai label */
#define T1S_DET_CTRL_OP_COUNT    (4u)
bool T1SLink_SendFretboardCtrl(uint8_t opcode, uint8_t arg);

/* Delivers a received node payload (already demuxed by src MAC) to a consumer.
 * Called from the T1S service task. `detector_id` is the node's bus id. */
typedef void (*T1SLink_FrameHandler)(uint8_t detector_id, const uint8_t *payload,
                                     uint16_t len);
void T1SLink_SetFrameHandler(T1SLink_FrameHandler handler);

/* Controller channel (ethertype 0x88B7): carries the marvin<->fauxmote mf_proto
 * messages over the shared MAC-PHY. Each frame payload is [TYPE][mf payload].
 *
 * TX: stage one mf_proto message to the controller node; the T1S service task
 * frames it ([dst=controller][src=coord][0x88B7][TYPE][payload]) and puts it on
 * the bus. Safe from any task (a static FIFO decouples the producer from the
 * single-threaded TC6 access). Returns false if the link is down or the FIFO is
 * full. Available only when a controller node is configured (fauxmote T1S build). */
bool T1SLink_SendToController(uint8_t type, const uint8_t *payload, uint8_t len);

/* Delivers a received controller-channel message to a consumer. Called from the
 * T1S service task with the mf_proto TYPE and its payload (header stripped). */
typedef void (*T1SLink_ControllerHandler)(uint8_t type, const uint8_t *payload,
                                          uint16_t len);
void T1SLink_SetControllerHandler(T1SLink_ControllerHandler handler);

#endif /* T1S_LINK_H */
