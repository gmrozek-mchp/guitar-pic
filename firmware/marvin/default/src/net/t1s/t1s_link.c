#include "t1s_link.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

#include "definitions.h"   /* FLEXCOM3_SPI_*, PIO_*, T1S_* pin macros */
#include "log.h"
#include "detector/detector.h"  /* DETECTOR_FRETBOARD */
#include "net/fauxmote/fauxmote_link.h"  /* MARVIN_FAUXMOTE_TRANSPORT */

#include "tc6.h"
#include "tc6-regs.h"

/* The controller channel (fauxmote over 0x88B7) is compiled in only when the
 * fauxmote link selects the T1S transport; otherwise there is no controller node
 * on the bus and the channel would be dead weight (and a phantom `nodes` row). */
#define T1S_CTRL_ENABLED (MARVIN_FAUXMOTE_TRANSPORT == FAUXMOTE_TRANSPORT_T1S)

/* marvin is the PLCA coordinator. Node IDs and MACs follow the addressing
 * scheme in docs/t1s-podl-link.md §7.1: coordinator = ID 0, MAC ...00. */
#define T1S_NODE_ID         (0u)
#define T1S_NODE_COUNT      (8u)     /* PLCA cycle length; headroom for nodes 1..7 */
#define T1S_PLCA_ENABLE     (true)
#define T1S_SPI_HZ          (15000000u)
#define T1S_INSTANCE        (0u)

#define T1S_TASK_STACK_WORDS (1024u)
#define T1S_TASK_PRIORITY    (5u)

/* Upper bound on TC6_Service calls per service_pump. The MAC-PHY can keep the
 * "need service" flag asserted indefinitely (e.g. a persistent IRQ_N with no
 * PLCA peer on the bus), which would spin this prio-5 task at 100% CPU and
 * starve every lower task — the whole UI/console/detector stack. Cap the pump
 * so it always returns to the task loop (which then blocks on s_svc_sem),
 * yielding the CPU; any still-pending work is picked up on the next wake. */
#define T1S_SERVICE_MAX_ITERS (8u)

/* MAC-PHY bring-up bounds. If the LAN8651 isn't populated/responding, the task
 * backs off and retries every T1S_ABSENT_RETRY_MS instead of servicing a dead
 * SPI bus — ~0% CPU with no chip, and it self-heals if one is attached later. */
#define T1S_BRINGUP_ACCEPT_MS   (500u)    /* deadline for TC6Regs_Init to be accepted */
#define T1S_BRINGUP_INITDONE_MS (3000u)   /* deadline for the async reg writes to finish */
#define T1S_ABSENT_RETRY_MS     (1000u)   /* backoff between bring-up attempts when absent */

/* L2 framing (docs/t1s-podl-link.md §7.1): a custom ethertype carries the
 * existing fretboard payloads verbatim inside a 14-byte Ethernet header. */
#define T1S_ETHERTYPE        (0x88B5u)   /* data / command frames */
#define T1S_ETHERTYPE_HB     (0x88B6u)   /* heartbeat / presence frames */
#define T1S_ETHERTYPE_CTRL   (0x88B7u)   /* controller (fauxmote mf_proto) frames */
#define T1S_ETHERTYPE_NODE_CTRL (0x88B9u) /* per-node typed [opcode,arg] control (lemmy, lightshow) */
#define T1S_ETH_HDR_LEN      (14u)
#define T1S_MAC_LEN          (6u)
#define T1S_HB_LEN           (8u)        /* legacy (v1): ver, type, id, flags, seq_u32 */
#define T1S_HB_EXT_LEN       (20u)       /* v2: + tx_u32, rx_u32, crc_u16, sym_u16 */
#define T1S_PRESENCE_TIMEOUT_MS (2000u) /* node "present" if a HB seen within this */

/* Wire timing, in bit times (1 BT = 100 ns at 10 Mbit/s), for the bus-occupancy
 * figures. A PLCA bus cycle (802.3cg Clause 148) is a BEACON followed by one
 * transmit opportunity per configured node: an unused TO is TO_TIMER of silence,
 * a used one is the transmission plus a short silence. A short silence also
 * follows the BEACON. See docs/t1s-podl-link.md §9. */
#define T1S_BT_PER_MS         (10000u)
#define T1S_PREAMBLE_LEN      (8u)    /* 7-byte preamble + SFD (SSD substituted in) */
#define T1S_BT_PREAMBLE       (T1S_PREAMBLE_LEN * 8u)
#define T1S_BT_BEACON         (20u)   /* five N symbols = 2 µs */
#define T1S_BT_SHORT_SILENCE  (16u)   /* after a BEACON, and after every transmission */
#define T1S_BT_TO_TIMER       (32u)   /* PLCA_TOTMR default: an unused TO, 3.2 µs */
#define T1S_ETH_MIN_LEN       (60u)   /* MAC pads to this before appending the FCS */
#define T1S_ETH_FCS_LEN       (4u)

/* Locally administered coordinator MAC (02:00:00:00:00:00). */
static uint8_t s_mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, (uint8_t)T1S_NODE_ID };

/* Static node directory. No discovery — adding a node is a table entry.
 * Detector nodes feed a detector-state bus id (RX source); guitar (actuator)
 * nodes are command TX targets (detector_id unused, set to 0xFF). */
#define T1S_NO_DETECTOR     (0xFFu)

typedef enum
{
    T1S_NODE_FRETBOARD,      /* detector: photo-ADC stream -> detector bus */
    T1S_NODE_GUITAR,         /* actuator: receives the button bitmask */
    T1S_NODE_CONTROLLER,     /* controller: fauxmote mf_proto channel (0x88B7) */
    T1S_NODE_ANIMATION,      /* animation: lemmy puppet (heartbeat-only for now) */
    T1S_NODE_LIGHTSHOW,      /* lightshow: LED lighting node (heartbeat-only for now) */
    T1S_NODE_BEATSOURCE,     /* beat source: beatbox audio/FFT node (heartbeat-only for now) */
} t1s_node_type_t;

typedef struct
{
    uint8_t         node_id;      /* PLCA id, also the MAC low byte */
    uint8_t         detector_id;  /* detector_state_t.detector_id (T1S_NO_DETECTOR for actuators) */
    t1s_node_type_t type;
} t1s_node_t;

static const t1s_node_t s_nodes[] = {
    /* Ordered by node id (docs/t1s-podl-link.md §7.1). */
#if T1S_CTRL_ENABLED
    { 1u, T1S_NO_DETECTOR,                 T1S_NODE_CONTROLLER }, /* fauxmote (0x88B7) */
#endif
    { 3u, T1S_NO_DETECTOR,                 T1S_NODE_GUITAR },     /* actuator (TX target) */
    { 4u, (uint8_t)DETECTOR_FRETBOARD, T1S_NODE_FRETBOARD },  /* fretboard (RX) */
    { 5u, T1S_NO_DETECTOR,                 T1S_NODE_BEATSOURCE }, /* beatbox (heartbeat) */
    { 6u, T1S_NO_DETECTOR,                 T1S_NODE_ANIMATION },  /* lemmy (heartbeat) */
    { 7u, T1S_NO_DETECTOR,                 T1S_NODE_LIGHTSHOW },  /* lightshow (heartbeat) */
};

#define T1S_NODE_TABLE_LEN  (sizeof(s_nodes) / sizeof(s_nodes[0]))

/* Per-node runtime presence + telemetry (parallel to s_nodes), updated on
 * heartbeat RX. The tx/rx/crc/sym fields are reported by the node in its extended
 * (v2) heartbeat and stay 0 for a legacy node; the rates are derived on the
 * coordinator once a second from the reported cumulative deltas.
 *
 * A node reports its own lifetime totals, which outlive marvin restarting — so
 * the first heartbeat seen establishes a baseline that is subtracted from every
 * later report. Everything on the bus screen is then "since marvin came up",
 * matching the uptime tile, and T1SLink_ResetCounters re-arms the baselines. */
static struct {
    uint32_t last_seen_tick;
    uint32_t last_seq;
    bool     seen;
    uint32_t tx_count, rx_count;   /* baselined, from the extended heartbeat  */
    uint16_t crc_err, sym_err;
    uint32_t tx_rate, rx_rate;     /* frames/s, recomputed once a second       */
    uint32_t prev_tx, prev_rx;     /* snapshot for the rate delta              */
    uint32_t base_tx, base_rx;     /* the node's totals when first seen        */
    uint16_t base_crc, base_sym;
    bool     based;                /* a baseline has been captured             */
    bool     out_en;               /* heartbeat flags bit1: node's output gate */
    uint32_t reconcile_tick;       /* last OUTPUT_EN re-push (rate limit)      */
    uint8_t  reconcile_hbs;        /* consecutive heartbeats in disagreement   */
    bool     warned_no_echo;       /* logged "not confirming" once             */
} s_node_rt[T1S_NODE_TABLE_LEN];

/* Saturating subtract: a node that restarts reports totals below its baseline,
 * which must read as zero rather than wrap to four billion. */
static uint32_t sub_base(uint32_t raw, uint32_t base)
{
    return (raw >= base) ? (raw - base) : 0u;
}

static const char *node_type_name(t1s_node_type_t t, uint8_t node_id)
{
    switch (t) {
        case T1S_NODE_FRETBOARD:     return "fretboard";
        case T1S_NODE_GUITAR:        return "guitar";
        case T1S_NODE_CONTROLLER:
            /* One controller node, id 1 (docs/t1s-podl-link.md §7.1). */
            (void)node_id;
            return "fauxmote";
        case T1S_NODE_ANIMATION:     return "lemmy";
        case T1S_NODE_LIGHTSHOW:     return "lightshow";
        case T1S_NODE_BEATSOURCE:    return "beatbox";
        default:                     return "?";
    }
}

/* Fill a follower MAC for a node id: 02:00:00:00:00:<id>. */
static void node_mac(uint8_t out[T1S_MAC_LEN], uint8_t node_id)
{
    out[0] = 0x02u;
    out[1] = 0x00u;
    out[2] = 0x00u;
    out[3] = 0x00u;
    out[4] = 0x00u;
    out[5] = node_id;
}

/* Look up a node by type (NULL if none of that type is configured). */
static const t1s_node_t *node_for_type(t1s_node_type_t type)
{
    for (uint8_t i = 0u; i < (sizeof(s_nodes) / sizeof(s_nodes[0])); i++) {
        if (s_nodes[i].type == type) {
            return &s_nodes[i];
        }
    }
    return NULL;
}

/* Look up a node by its source MAC (NULL if unknown). */
static const t1s_node_t *node_for_mac(const uint8_t mac[T1S_MAC_LEN])
{
    if ((mac[0] != 0x02u) || (mac[1] | mac[2] | mac[3] | mac[4])) {
        return NULL;
    }
    for (uint8_t i = 0u; i < (sizeof(s_nodes) / sizeof(s_nodes[0])); i++) {
        if (s_nodes[i].node_id == mac[5]) {
            return &s_nodes[i];
        }
    }
    return NULL;
}

/* TX frame staging: TC6_SendRawEthernetPacket keeps a pointer to the buffer
 * until its TX callback fires, so the buffer must stay valid meanwhile. One
 * in-flight frame at a time (guarded by s_tx_busy) suffices for the link's
 * rates. */
static uint8_t       s_tx_frame[T1S_ETH_HDR_LEN + 64u];
static volatile bool s_tx_busy;

/* Latest-wins outbound command to the guitar, flushed by the service task. */
static volatile uint8_t s_cmd;
static volatile bool    s_cmd_dirty;

/* Latest-wins outbound position command to lemmy (animation): [neck, jaw] as
 * int8 bit patterns, flushed by the service task alongside the guitar command. */
static volatile uint8_t s_lemmy_cmd[2];
static volatile bool    s_lemmy_dirty;

/* Node control channels (0x88B9): typed [opcode, arg] commands, one per-node
 * opcode namespace. Staged per-opcode (indexed by opcode-1) rather than
 * latest-wins so distinct commands can't drop each other; one frame flushed per
 * service pass. guitar = output enable; lemmy = beat-nod tuning + servo output
 * enable; lightshow = LED output enable; fretboard = arm/stream/model/teacher.
 *
 * `arg` doubles as the desired state: it holds the last value commanded whether
 * or not the wire carried it, which is what lets a resync (node restart) or a
 * reconcile (node's report disagrees) re-push without asking the layer above. */
#define T1S_CTRL_OP_MAX  (5u)   /* widest opcode namespace (lemmy) */

typedef struct {
    t1s_node_type_t  type;
    uint8_t          op_count;
    volatile uint8_t arg[T1S_CTRL_OP_MAX];
    volatile bool    dirty[T1S_CTRL_OP_MAX];  /* staged, not yet on the wire      */
    volatile bool    known[T1S_CTRL_OP_MAX];  /* ever commanded -> resyncable     */
} t1s_ctrl_chan_t;

static t1s_ctrl_chan_t s_ctrl_chan[] = {
    { T1S_NODE_GUITAR,    T1S_GUITAR_CTRL_OP_COUNT, {0}, {0}, {0} },
    { T1S_NODE_ANIMATION, T1S_ANIM_CTRL_OP_COUNT,   {0}, {0}, {0} },
    { T1S_NODE_LIGHTSHOW, T1S_LIGHT_CTRL_OP_COUNT,  {0}, {0}, {0} },
    { T1S_NODE_FRETBOARD, T1S_DET_CTRL_OP_COUNT,    {0}, {0}, {0} },
};

#define T1S_CTRL_CHAN_LEN  (sizeof(s_ctrl_chan) / sizeof(s_ctrl_chan[0]))

/* The OUTPUT_EN opcode in each actuator's namespace, indexed by t1s_actuator_t.
 * They are not the same number — lemmy's control channel already owned 1..3 for
 * the nod before the servo gate was added. */
static const struct {
    t1s_node_type_t type;
    uint8_t         output_en_op;
} s_actuators[T1S_ACT_COUNT] = {
    { T1S_NODE_GUITAR,    T1S_GUITAR_CTRL_OUTPUT_EN },
    { T1S_NODE_ANIMATION, T1S_ANIM_CTRL_OUTPUT_EN   },
    { T1S_NODE_LIGHTSHOW, T1S_LIGHT_CTRL_OUTPUT_EN  },
};

/* Look up a node's control channel (NULL if that node type has none). */
static t1s_ctrl_chan_t *chan_for_type(t1s_node_type_t type)
{
    for (uint8_t i = 0u; i < T1S_CTRL_CHAN_LEN; i++) {
        if (s_ctrl_chan[i].type == type) {
            return &s_ctrl_chan[i];
        }
    }
    return NULL;
}

static T1SLink_FrameHandler s_frame_handler;

/* Traffic counters (read by the console t1s/nodes commands). */
static volatile uint32_t s_tx_count;   /* command frames sent */
static volatile uint32_t s_rx_count;   /* detector frames received from a known node */
static volatile uint32_t s_hb_rx_count; /* presence/telemetry heartbeats received */
static volatile uint32_t s_service_overruns; /* service_pump hit its iter cap (stuck MAC-PHY) */

/* marvin's own MAC-PHY error tallies (from TC6Regs_CB_OnEvent), for the self row. */
static volatile uint32_t s_self_crc_err;   /* FCS errors */
static volatile uint32_t s_self_sym_err;   /* loss-of-framing (symbol) errors */

/* Wire accounting for the bus-occupancy figures, counted as bytes on the medium
 * rather than as frames. marvin's MAC is promiscuous (see t1s_try_bringup), so
 * every frame any node transmits is counted here on receive, and marvin never
 * hears its own transmissions — so tx + rx is the whole segment's traffic,
 * counted exactly once, with no dependence on what the followers report. */
static volatile uint32_t s_wire_tx_frames, s_wire_tx_bytes;
static volatile uint32_t s_wire_rx_frames, s_wire_rx_bytes;

/* Rate state: per-node rates live in s_node_rt; these hold marvin's own rate plus
 * the once-a-second recompute bookkeeping. Rates are frames/s. */
static uint32_t  s_self_tx_rate, s_self_rx_rate;
static uint32_t  s_prev_self_tx, s_prev_self_rx;
static uint32_t  s_prev_wire_frames, s_prev_wire_bytes;
static uint32_t  s_util_permille;      /* medium time spent carrying frames */
static uint32_t  s_wire_bps;           /* wire bytes/s, whole segment        */
static uint32_t  s_plca_cycles;        /* PLCA bus cycles/s                  */
static uint32_t  s_to_used_permille;   /* transmit opportunities that carried a frame */
static TickType_t s_rate_tick;   /* last rate recompute */
static TickType_t s_boot_tick;   /* T1SLink_Initialize — uptime origin */

#if T1S_CTRL_ENABLED
/* Controller channel (fauxmote, 0x88B7). Producers stage one mf_proto message
 * at a time into a static FIFO; the service task frames + flushes them through
 * the shared single-in-flight TX path, interleaved with the guitar command. */
#define T1S_CTRL_ITEM_MAX   (2u + 8u)   /* type + len + max mf payload */
#define T1S_CTRL_QUEUE_DEPTH (8u)

static QueueHandle_t s_ctrl_queue;
static StaticQueue_t s_ctrl_queue_buf;
static uint8_t       s_ctrl_queue_storage[T1S_CTRL_QUEUE_DEPTH * T1S_CTRL_ITEM_MAX];

static T1SLink_ControllerHandler s_ctrl_handler;
static volatile uint32_t s_ctrl_tx_count;
static volatile uint32_t s_ctrl_rx_count;
#endif

/* Every frame marvin puts on or takes off the wire, across all channels it
 * terminates. Node rows report the node's own all-ethertype totals, so the self
 * row has to be counted the same way or the two disagree. */
static uint32_t self_tx_total(void) { return s_tx_count + T1SLink_CtrlTxCount(); }
static uint32_t self_rx_total(void)
{
    return s_rx_count + T1SLink_CtrlRxCount() + s_hb_rx_count;
}

static TC6_t            *s_tc6;
static volatile bool     s_need_service;
static volatile bool     s_link_up;

static SemaphoreHandle_t s_svc_sem;
static StaticSemaphore_t s_svc_sem_buf;

static StackType_t       s_task_stack[T1S_TASK_STACK_WORDS];
static StaticTask_t      s_task_tcb;

/* RX reassembly: TC6 delivers an Ethernet frame as slices; collect them here
 * and process on the OnRxEthernetPacket completion callback. */
static uint8_t           s_rx_buf[1518];

/* FLEXCOM3 SPI completion ISR callback: the chunk transfer is done, hand the
 * buffer back to the TC6 driver and wake the service task. */
static void spi_done_cb(uintptr_t context)
{
    (void)context;
    BaseType_t hpw = pdFALSE;
    TC6_SpiBufferDone(T1S_INSTANCE, true);
    (void)xSemaphoreGiveFromISR(s_svc_sem, &hpw);
    portYIELD_FROM_ISR(hpw);
}

/* T1S_IRQ_N falling-edge PIO ISR callback: the MAC-PHY needs servicing. */
static void irq_cb(PIO_PIN pin, uintptr_t context)
{
    (void)pin;
    (void)context;
    BaseType_t hpw = pdFALSE;
    s_need_service = true;
    (void)xSemaphoreGiveFromISR(s_svc_sem, &hpw);
    portYIELD_FROM_ISR(hpw);
}

/* TX completion: the staged frame buffer is free for reuse. */
static void tx_done_cb(TC6_t *pInst, const uint8_t *pTx, uint16_t len,
                       void *pTag, void *pGlobalTag)
{
    (void)pInst;
    (void)pTx;
    (void)len;
    (void)pTag;
    (void)pGlobalTag;
    s_tx_busy = false;
}

/* Frame a payload to a node (dst = 02:00:00:00:00:<node_id>) under `ethertype`
 * and queue it. Returns false if a TX is already in flight or the driver
 * rejected it. */
static bool send_to_node(uint8_t node_id, uint16_t ethertype,
                         const uint8_t *payload, uint16_t payload_len)
{
    if (s_tx_busy || !s_link_up) {
        return false;
    }
    if (payload_len > (sizeof(s_tx_frame) - T1S_ETH_HDR_LEN)) {
        return false;
    }
    node_mac(&s_tx_frame[0], node_id);          /* dest MAC */
    memcpy(&s_tx_frame[6], s_mac, T1S_MAC_LEN);  /* src MAC  */
    s_tx_frame[12] = (uint8_t)(ethertype >> 8);
    s_tx_frame[13] = (uint8_t)(ethertype & 0xFFu);
    memcpy(&s_tx_frame[T1S_ETH_HDR_LEN], payload, payload_len);

    s_tx_busy = true;
    bool ok = TC6_SendRawEthernetPacket(s_tc6, s_tx_frame,
                                        (uint16_t)(T1S_ETH_HDR_LEN + payload_len),
                                        0u, tx_done_cb, NULL);
    if (!ok) {
        s_tx_busy = false;
    } else {
        /* Wire size, not the length handed to the driver: the MAC pads short
         * frames to 60 bytes and appends the FCS itself (QTXCFG.MACFCSDIS is 0),
         * so grow the count to match what a receiver reports for the same frame. */
        uint32_t wire = T1S_ETH_HDR_LEN + payload_len;
        if (wire < T1S_ETH_MIN_LEN) { wire = T1S_ETH_MIN_LEN; }
        s_wire_tx_bytes += wire + T1S_ETH_FCS_LEN;
        s_wire_tx_frames++;
    }
    return ok;
}

/* Run the protocol stack until it has no immediately pending work. IRQ_N is
 * active-low; TC6_Service treats a false interruptLevel as "interrupt active". */
/* Returns true if it bailed with work still pending (hit the iteration cap) —
 * the caller must then yield so a stuck MAC-PHY can't starve other tasks. */
static bool service_pump(void)
{
    unsigned iters = 0u;
    do {
        s_need_service = false;
        bool no_int = (T1S_IRQ_N_Get() != 0u);
        (void)TC6_Service(s_tc6, no_int);
    } while (s_need_service && (++iters < T1S_SERVICE_MAX_ITERS));

    TC6Regs_CheckTimers();

    /* Persistent need-service = the MAC-PHY isn't quiescing (stuck IRQ / no
     * peer). Don't spin on it; count it and tell the caller to yield. */
    if (s_need_service) { s_service_overruns++; return true; }
    return false;
}

/* One MAC-PHY bring-up attempt: reset pulse, push the register + PLCA config,
 * and drive the async writes to completion within a deadline. Returns true only
 * if the chip acked init done. False = absent/unresponsive → the caller backs
 * off and retries (so an unpopulated LAN8651 never turns into a dead-bus spin).
 * Re-pulsing reset each attempt also resets a chip attached after boot. */
static bool t1s_try_bringup(void)
{
    /* Hardware reset pulse (T1S_RST is active-low, idle high). */
    T1S_RST_Clear();
    vTaskDelay(pdMS_TO_TICKS(10));
    T1S_RST_Set();
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Configure the LAN8651 registers + PLCA (coordinator, id 0). promiscuous
     * during bring-up so RX isn't filtered before the node table exists.
     * TC6Regs_Init only queues the writes — bounded accept retry. */
    TickType_t accept_dl = xTaskGetTickCount() + pdMS_TO_TICKS(T1S_BRINGUP_ACCEPT_MS);
    while (!TC6Regs_Init(s_tc6, NULL, s_mac, T1S_PLCA_ENABLE, T1S_NODE_ID,
                         T1S_NODE_COUNT, 0u, 0u, true, false, false)) {
        if ((int32_t)(accept_dl - xTaskGetTickCount()) <= 0) { return false; }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    /* Drive the async register writes to completion (bounded). */
    TickType_t done_dl = xTaskGetTickCount() + pdMS_TO_TICKS(T1S_BRINGUP_INITDONE_MS);
    while (!TC6Regs_GetInitDone(s_tc6) &&
           ((int32_t)(done_dl - xTaskGetTickCount()) > 0)) {
        (void)xSemaphoreTake(s_svc_sem, pdMS_TO_TICKS(2));
        (void)service_pump();
    }
    return TC6Regs_GetInitDone(s_tc6);
}

static volatile bool     s_probe_done;
static volatile bool     s_probe_ok;
static volatile uint32_t s_probe_val;

static void probe_cb(TC6_t *pInst, bool success, uint32_t addr, uint32_t value,
                     void *pTag, void *pGlobalTag)
{
    (void)pInst; (void)addr; (void)pTag; (void)pGlobalTag;
    s_probe_ok   = success;
    s_probe_val  = value;
    s_probe_done = true;
}

/* One raw unprotected control read, bounded so a dead MAC-PHY can't hang the
 * task. Same access the TC6 lib uses for its identity gate. */
static bool probe_reg(uint32_t addr, uint32_t *out)
{
    TickType_t dl = xTaskGetTickCount() + pdMS_TO_TICKS(100u);

    s_probe_done = false;
    s_probe_ok   = false;
    s_probe_val  = 0u;

    while (!TC6_ReadRegister(s_tc6, addr, false, probe_cb, NULL)) {
        if ((int32_t)(dl - xTaskGetTickCount()) <= 0) { return false; }
        (void)TC6_Service(s_tc6, true);
    }
    while (!s_probe_done) {
        if ((int32_t)(dl - xTaskGetTickCount()) <= 0) { return false; }
        (void)TC6_Service(s_tc6, true);
    }
    *out = s_probe_val;
    return s_probe_ok;
}

/* Dump the registers the TC6 lib gates bring-up on, plus two whose contents are
 * known independently, so a bad read distinguishes a dead chip from a broken
 * control transaction. PHYID is read twice: identical wrong values mean a
 * deterministic framing fault, differing ones mean marginal SPI. If every
 * address returns the same word, the address field isn't reaching the chip. */
static void log_identity_probe(void)
{
    static const struct {
        uint32_t    addr;
        const char *name;
        const char *expect;
    } probes[] = {
        { 0x00000000u, "MMS0.0x00  OA_ID  ", "0x00000011"                    },
        { 0x00000001u, "MMS0.0x01  OA_PHYID", "0x0007C1B3"                  },
        { 0x00000008u, "MMS0.0x08  STATUS0", "bit6 RESETC set after reset"   },
        { 0x000A0094u, "MMS10.0x94 DEVID  ", "0x00086512 (LAN8651, si rev 2)"},
        { 0x00000001u, "MMS0.0x01  OA_PHYID", "re-read, expect same as above"},
    };

    for (unsigned i = 0u; i < (sizeof(probes) / sizeof(probes[0])); i++) {
        uint32_t val = 0u;
        bool ok = probe_reg(probes[i].addr, &val);
        LOG_ERROR("T1S: id probe: %s = 0x%08X%s (%s)\r\n",
                  probes[i].name, (unsigned)val,
                  ok ? "" : " [READ FAILED]", probes[i].expect);
    }
}

/* Once a second, refresh the frames/s rates from the cumulative-count deltas —
 * marvin's own counters plus each node's last-reported extended-heartbeat totals.
 * Runs in the service task; a no-op until a full second has elapsed. */
static void recompute_rates(void)
{
    TickType_t now = xTaskGetTickCount();
    if ((uint32_t)(now - s_rate_tick) < pdMS_TO_TICKS(1000u)) { return; }
    uint32_t dt_ms = (uint32_t)((now - s_rate_tick) * portTICK_PERIOD_MS);
    s_rate_tick = now;
    if (dt_ms == 0u) { return; }

    uint32_t self_tx = self_tx_total();
    uint32_t self_rx = self_rx_total();
    s_self_tx_rate = (self_tx - s_prev_self_tx) * 1000u / dt_ms;
    s_self_rx_rate = (self_rx - s_prev_self_rx) * 1000u / dt_ms;
    s_prev_self_tx = self_tx;
    s_prev_self_rx = self_rx;

    for (uint8_t i = 0u; i < T1S_NODE_TABLE_LEN; i++) {
        s_node_rt[i].tx_rate = (s_node_rt[i].tx_count - s_node_rt[i].prev_tx) * 1000u / dt_ms;
        s_node_rt[i].rx_rate = (s_node_rt[i].rx_count - s_node_rt[i].prev_rx) * 1000u / dt_ms;
        s_node_rt[i].prev_tx = s_node_rt[i].tx_count;
        s_node_rt[i].prev_rx = s_node_rt[i].rx_count;
    }

    /* Bus occupancy, from the wire byte counts rather than a per-frame guess.
     * Frame counts must not be summed across nodes for this: one frame occupies
     * the medium once but appears in its sender's tx and in every listener's rx. */
    uint32_t wire_frames = s_wire_tx_frames + s_wire_rx_frames;
    uint32_t wire_bytes  = s_wire_tx_bytes  + s_wire_rx_bytes;
    uint32_t frames = wire_frames - s_prev_wire_frames;
    uint32_t bytes  = wire_bytes  - s_prev_wire_bytes;
    s_prev_wire_frames = wire_frames;
    s_prev_wire_bytes  = wire_bytes;

    /* Bit times overflow a uint32 past ~7 minutes, and the bring-up retry loop
     * skips this function entirely — so a gap that long resynchronizes rather
     * than reporting an interval it can't represent. */
    if (dt_ms > 10000u) {
        s_util_permille    = 0u;
        s_wire_bps         = 0u;
        s_plca_cycles      = 0u;
        s_to_used_permille = 0u;
        return;
    }

    /* Preamble + SFD is 8 bytes per frame that the MAC generates itself, so it
     * appears in no frame length and has to be added here — otherwise the byte
     * rate understates what the medium carries and can't be reconciled against
     * the percentage by hand. */
    uint32_t elapsed_bt = dt_ms * T1S_BT_PER_MS;
    uint32_t wire_bt    = (bytes + (frames * T1S_PREAMBLE_LEN)) * 8u;
    if (wire_bt > elapsed_bt) { wire_bt = elapsed_bt; }

    /* Utilization is the share of line time carrying frames. The remainder is
     * the BEACON and the mandated silences, which a PLCA bus spends whether or
     * not anyone is talking — so this reads near zero on an idle bus by design,
     * and the headroom question is answered by s_to_used_permille below.
     *
     * Both figures derive from the same clamped wire_bt, so the identity
     * util = wire_bps · 8 / 10 Mbit holds exactly, at every traffic level. */
    s_util_permille = wire_bt / (dt_ms * (T1S_BT_PER_MS / 1000u));
    s_wire_bps      = (uint32_t)(((uint64_t)(wire_bt / 8u) * 1000u) / dt_ms);

    /* Cycles in the interval, from the cycle structure:
     *   elapsed = cycles·(BEACON + SHORT) + unused_TOs·TO_TIMER + Σ(frame + SHORT)
     * with unused_TOs = cycles·NODE_COUNT − frames. Solving for cycles leaves the
     * frames' short silences netted against the TO_TIMER slots they displaced. */
    uint32_t cycle_bt = T1S_BT_BEACON + T1S_BT_SHORT_SILENCE
                      + (T1S_NODE_COUNT * T1S_BT_TO_TIMER);
    uint32_t cycles_bt = elapsed_bt - wire_bt
                       + (frames * (T1S_BT_TO_TIMER - T1S_BT_SHORT_SILENCE));
    uint32_t cycles = cycles_bt / cycle_bt;
    s_plca_cycles = (uint32_t)(((uint64_t)cycles * 1000u) / dt_ms);

    /* The real saturation measure: a PLCA bus runs out of room when nodes want to
     * transmit more often than their transmit opportunity comes around, not when
     * the bit rate nears 10 Mbit/s. */
    uint32_t tos = cycles * T1S_NODE_COUNT;
    s_to_used_permille = (tos != 0u)
        ? (uint32_t)(((uint64_t)frames * 1000u) / tos) : 1000u;
    /* Offered demand beyond what the line can carry clamps frame_bt above, which
     * collapses the derived cycle count while the frame count stays high — so the
     * ratio can exceed unity on a bus that is simply oversubscribed. */
    if (s_to_used_permille > 1000u) { s_to_used_permille = 1000u; }
}

static void t1s_task(void *param)
{
    (void)param;

    /* SPI: Mode 0 (CPOL=0 idle low, CPHA=0 leading edge), 8-bit, 15 MHz.
     * Source clock 0 => PLib uses the FLEXCOM3 peripheral clock. One-time. */
    FLEXCOM_SPI_TRANSFER_SETUP setup = {
        .clockFrequency = T1S_SPI_HZ,
        .clockPhase     = FLEXCOM_SPI_CLOCK_PHASE_LEADING_EDGE,
        .clockPolarity  = FLEXCOM_SPI_CLOCK_POLARITY_IDLE_LOW,
        .dataBits       = FLEXCOM_SPI_DATA_BITS_8,
    };
    (void)FLEXCOM3_SPI_TransferSetup(&setup, 0u);
    FLEXCOM3_SPI_CallbackRegister(spi_done_cb, 0u);

    s_tc6 = TC6_Init(NULL);
    if (s_tc6 == NULL) {
        LOG_ERROR("T1S: TC6_Init failed\r\n");
        vTaskDelete(NULL);
        return;
    }

    (void)PIO_PinInterruptCallbackRegister(T1S_IRQ_N_PIN, irq_cb, 0u);
    PIO_PinInterruptEnable(T1S_IRQ_N_PIN);

    bool warned_absent = false;

    for (;;) {
        /* Bring up (or re-bring-up) the MAC-PHY. Absent/unresponsive → warn once
         * and retry with backoff instead of servicing a dead bus. */
        if (!s_link_up) {
            if (!t1s_try_bringup()) {
                if (!warned_absent) {
                    LOG_WARN("T1S: MAC-PHY not responding (LAN8651 populated? wiring?); "
                             "retry every %ums\r\n", (unsigned)T1S_ABSENT_RETRY_MS);
                    warned_absent = true;
                    log_identity_probe();
                }
                vTaskDelay(pdMS_TO_TICKS(T1S_ABSENT_RETRY_MS));
                continue;
            }
            s_link_up      = true;
            warned_absent  = false;
            TC6_EnableData(s_tc6, true);
            LOG_INFO("T1S: LAN8651 up — chipRev=%u, MAC=%02X:%02X:%02X:%02X:%02X:%02X, "
                     "PLCA coord id=%u/%u\r\n",
                     (unsigned)TC6Regs_GetChipRevision(s_tc6),
                     s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5],
                     (unsigned)T1S_NODE_ID, (unsigned)T1S_NODE_COUNT);
        }

        (void)xSemaphoreTake(s_svc_sem, pdMS_TO_TICKS(1));
        /* If the pump bailed with work still pending, force a yield so this
         * prio-5 task can never monopolize the CPU on a stuck MAC-PHY. */
        if (service_pump()) { vTaskDelay(pdMS_TO_TICKS(1)); }

        recompute_rates();   /* once-a-second frames/s from cumulative-count deltas */

        /* Flush the latest pending command to the active guitar (actuator)
         * node. All TC6 access stays in this task; producers only stash via
         * the API. (Single guitar today; an active-guitar selector goes here
         * when multiple guitar nodes share the bus.) */
        if (s_cmd_dirty && !s_tx_busy) {
            const t1s_node_t *guitar = node_for_type(T1S_NODE_GUITAR);
            if (guitar != NULL) {
                /* Clear before reading so a concurrent update re-arms dirty
                 * rather than being dropped (latest-wins). */
                s_cmd_dirty = false;
                uint8_t mask = s_cmd;
                if (send_to_node(guitar->node_id, T1S_ETHERTYPE, &mask, 1u)) {
                    s_tx_count++;
                }
            }
        }

        /* Flush the latest pending position command to lemmy (animation node).
         * Combined [neck, jaw] int8 payload, one frame; shares the single
         * in-flight TX with the guitar command above. */
        if (s_lemmy_dirty && !s_tx_busy) {
            const t1s_node_t *lemmy = node_for_type(T1S_NODE_ANIMATION);
            if (lemmy != NULL) {
                s_lemmy_dirty = false;
                uint8_t cmd[2] = { s_lemmy_cmd[0], s_lemmy_cmd[1] };
                if (send_to_node(lemmy->node_id, T1S_ETHERTYPE, cmd, 2u)) {
                    s_tx_count++;
                }
            }
        }

        /* Flush one staged node-control command (0x88B9) across every channel.
         * One frame per service pass, so the channels drain fairly across wakes
         * (each TX-done gives s_svc_sem) and no opcode can starve another.
         *
         * `dirty` is cleared only on a successful send: a refused TX leaves the
         * opcode staged and the next pass retries it, so a command can't be lost
         * to a busy MAC-PHY while marvin believes the node was told. */
        for (uint8_t c = 0u; (c < T1S_CTRL_CHAN_LEN) && !s_tx_busy; c++) {
            t1s_ctrl_chan_t  *chan = &s_ctrl_chan[c];
            const t1s_node_t *node = node_for_type(chan->type);
            if (node == NULL) { continue; }

            for (uint8_t i = 0u; i < chan->op_count; i++) {
                if (!chan->dirty[i]) { continue; }
                uint8_t frame[2] = { (uint8_t)(i + 1u), chan->arg[i] };
                if (send_to_node(node->node_id, T1S_ETHERTYPE_NODE_CTRL, frame, 2u)) {
                    chan->dirty[i] = false;
                    s_tx_count++;
                }
                break;   /* one frame per pass (send_to_node set s_tx_busy) */
            }
        }

#if T1S_CTRL_ENABLED
        /* Flush one staged controller (fauxmote) message onto the bus. Shares
         * the single in-flight TX with the guitar command above; across service
         * wakes (each TX-done gives s_svc_sem) both channels drain fairly. */
        if (!s_tx_busy) {
            uint8_t item[T1S_CTRL_ITEM_MAX];
            if (xQueueReceive(s_ctrl_queue, item, 0) == pdTRUE) {
                const t1s_node_t *ctrl = node_for_type(T1S_NODE_CONTROLLER);
                uint8_t len = item[1];
                if ((ctrl != NULL) && (len <= 8u)) {
                    /* item = [TYPE][LEN][payload]; frame body is [TYPE][payload]. */
                    uint8_t body[1u + 8u];
                    body[0] = item[0];
                    if (len != 0u) { memcpy(&body[1], &item[2], len); }
                    if (send_to_node(ctrl->node_id, T1S_ETHERTYPE_CTRL,
                                     body, (uint16_t)(1u + len))) {
                        s_ctrl_tx_count++;
                    }
                }
            }
        }
#endif
    }
}

void T1SLink_Initialize(void)
{
    /* Idempotent: both the fretboard (T1S) and fauxmote (T1S) links share this
     * single MAC-PHY and each calls Initialize; the first wins. */
    static bool s_initialized;
    if (s_initialized) { return; }
    s_initialized = true;

    s_boot_tick = xTaskGetTickCount();   /* uptime origin */
    s_rate_tick = s_boot_tick;

    s_svc_sem = xSemaphoreCreateBinaryStatic(&s_svc_sem_buf);
    configASSERT(s_svc_sem != NULL);

#if T1S_CTRL_ENABLED
    s_ctrl_queue = xQueueCreateStatic(T1S_CTRL_QUEUE_DEPTH, T1S_CTRL_ITEM_MAX,
                                      s_ctrl_queue_storage, &s_ctrl_queue_buf);
    configASSERT(s_ctrl_queue != NULL);
#endif

    (void)xTaskCreateStatic(t1s_task, "T1SLink", T1S_TASK_STACK_WORDS, NULL,
                            T1S_TASK_PRIORITY, s_task_stack, &s_task_tcb);
}

bool T1SLink_IsConnected(void)
{
    return s_link_up;
}

void T1SLink_GetState(bool *synced, uint8_t *txCredit, uint8_t *rxCredit)
{
    uint8_t tx = 0u, rx = 0u;
    bool    sy = false;
    if (s_tc6 != NULL) {
        TC6_GetState(s_tc6, &tx, &rx, &sy);
    }
    if (synced   != NULL) { *synced   = sy; }
    if (txCredit != NULL) { *txCredit = tx; }
    if (rxCredit != NULL) { *rxCredit = rx; }
}

uint8_t  T1SLink_ChipRev(void)   { return (s_tc6 != NULL) ? TC6Regs_GetChipRevision(s_tc6) : 0u; }
uint8_t  T1SLink_NodeId(void)    { return (uint8_t)T1S_NODE_ID; }
uint8_t  T1SLink_NodeCount(void) { return (uint8_t)T1S_NODE_COUNT; }
uint32_t T1SLink_TxCount(void)   { return s_tx_count; }
uint32_t T1SLink_RxCount(void)   { return s_rx_count; }
uint32_t T1SLink_ServiceOverruns(void) { return s_service_overruns; }

#if T1S_CTRL_ENABLED
uint32_t T1SLink_CtrlTxCount(void) { return s_ctrl_tx_count; }
uint32_t T1SLink_CtrlRxCount(void) { return s_ctrl_rx_count; }
#else
uint32_t T1SLink_CtrlTxCount(void) { return 0u; }
uint32_t T1SLink_CtrlRxCount(void) { return 0u; }
#endif

uint8_t T1SLink_NodeTableCount(void) { return (uint8_t)T1S_NODE_TABLE_LEN; }

bool T1SLink_GetNodeInfo(uint8_t idx, T1SLink_NodeInfo *out)
{
    if ((idx >= T1S_NODE_TABLE_LEN) || (out == NULL)) {
        return false;
    }
    out->node_id = s_nodes[idx].node_id;
    out->type    = node_type_name(s_nodes[idx].type, s_nodes[idx].node_id);
    if (s_node_rt[idx].seen) {
        uint32_t age = xTaskGetTickCount() - s_node_rt[idx].last_seen_tick;
        out->age_ms  = (uint32_t)(age * portTICK_PERIOD_MS);
        out->present = (age < pdMS_TO_TICKS(T1S_PRESENCE_TIMEOUT_MS));
    } else {
        out->age_ms  = 0u;
        out->present = false;
    }
    return true;
}

/* Fill presence (node_id, type, present, age_ms) for a node index — shared by
 * GetNodeStats and the aggregate presence count. */
static void fill_presence(uint8_t idx, T1SLink_NodeStats *out)
{
    out->node_id = s_nodes[idx].node_id;
    out->type    = node_type_name(s_nodes[idx].type, s_nodes[idx].node_id);
    if (s_node_rt[idx].seen) {
        uint32_t age = xTaskGetTickCount() - s_node_rt[idx].last_seen_tick;
        out->age_ms  = (uint32_t)(age * portTICK_PERIOD_MS);
        out->present = (age < pdMS_TO_TICKS(T1S_PRESENCE_TIMEOUT_MS));
    } else {
        out->age_ms  = 0u;
        out->present = false;
    }
}

bool T1SLink_GetNodeStats(uint8_t idx, T1SLink_NodeStats *out)
{
    if ((idx >= T1S_NODE_TABLE_LEN) || (out == NULL)) {
        return false;
    }
    fill_presence(idx, out);
    out->tx_count = s_node_rt[idx].tx_count;
    out->rx_count = s_node_rt[idx].rx_count;
    out->tx_rate  = s_node_rt[idx].tx_rate;
    out->rx_rate  = s_node_rt[idx].rx_rate;
    out->crc_err  = s_node_rt[idx].crc_err;
    out->sym_err  = s_node_rt[idx].sym_err;
    return true;
}

bool T1SLink_GetSelfStats(T1SLink_NodeStats *out)
{
    if (out == NULL) { return false; }
    out->node_id  = (uint8_t)T1S_NODE_ID;
    out->type     = "marvin";
    out->present  = true;
    out->age_ms   = 0u;
    out->tx_count = self_tx_total();
    out->rx_count = self_rx_total();
    out->tx_rate  = s_self_tx_rate;
    out->rx_rate  = s_self_rx_rate;
    out->crc_err  = (uint16_t)s_self_crc_err;
    out->sym_err  = (uint16_t)s_self_sym_err;
    return true;
}

void T1SLink_ResetCounters(void)
{
    /* Runs on the caller's task (console / UI) while the service task is still
     * counting. Every field is a single aligned word, so a concurrent frame can
     * only cost this reset one count — not worth a lock on the RX hot path. */
    s_tx_count     = 0u;
    s_rx_count     = 0u;
    s_hb_rx_count  = 0u;
    s_self_crc_err = 0u;
    s_self_sym_err = 0u;
#if T1S_CTRL_ENABLED
    s_ctrl_tx_count = 0u;
    s_ctrl_rx_count = 0u;
#endif
    s_wire_tx_frames = 0u;
    s_wire_tx_bytes  = 0u;
    s_wire_rx_frames = 0u;
    s_wire_rx_bytes  = 0u;

    s_prev_self_tx     = 0u;
    s_prev_self_rx     = 0u;
    s_prev_wire_frames = 0u;
    s_prev_wire_bytes  = 0u;
    s_self_tx_rate     = 0u;
    s_self_rx_rate     = 0u;
    s_util_permille    = 0u;
    s_wire_bps         = 0u;
    s_plca_cycles      = 0u;
    s_to_used_permille = 0u;

    for (uint8_t i = 0u; i < T1S_NODE_TABLE_LEN; i++) {
        /* Re-arm the baseline: the node's next heartbeat re-zeroes it against
         * its totals as of now, rather than against its own boot. Presence
         * (seen / last_seen_tick) deliberately survives — this resets traffic
         * counters, it doesn't forget who is on the bus. */
        s_node_rt[i].based    = false;
        s_node_rt[i].tx_count = 0u;
        s_node_rt[i].rx_count = 0u;
        s_node_rt[i].crc_err  = 0u;
        s_node_rt[i].sym_err  = 0u;
        s_node_rt[i].tx_rate  = 0u;
        s_node_rt[i].rx_rate  = 0u;
        s_node_rt[i].prev_tx  = 0u;
        s_node_rt[i].prev_rx  = 0u;
    }

    /* Uptime is the window these totals cover, so it restarts with them —
     * otherwise the screen pairs a fresh count with a stale window, which is
     * the incoherence this whole change exists to remove. */
    TickType_t now = xTaskGetTickCount();
    s_rate_tick = now;
    s_boot_tick = now;
}

bool T1SLink_GetBusStats(T1SLink_BusStats *out)
{
    if (out == NULL) { return false; }

    uint32_t tx_total  = self_tx_total();
    uint32_t rx_total  = self_rx_total();
    uint32_t crc_total = s_self_crc_err;
    uint32_t sym_total = s_self_sym_err;
    uint8_t  online    = 1u;   /* marvin is always up */

    for (uint8_t i = 0u; i < T1S_NODE_TABLE_LEN; i++) {
        tx_total  += s_node_rt[i].tx_count;
        rx_total  += s_node_rt[i].rx_count;
        crc_total += s_node_rt[i].crc_err;
        sym_total += s_node_rt[i].sym_err;
        if (s_node_rt[i].seen) {
            uint32_t age = xTaskGetTickCount() - s_node_rt[i].last_seen_tick;
            if (age < pdMS_TO_TICKS(T1S_PRESENCE_TIMEOUT_MS)) { online++; }
        }
    }

    uint32_t permille = s_util_permille;
    if (permille > 1000u) { permille = 1000u; }

    uint32_t frames = tx_total + rx_total;

    out->util_permille = permille;
    out->wire_bps      = s_wire_bps;
    out->plca_cycles   = s_plca_cycles;
    out->to_used_permille = s_to_used_permille;
    out->tx_total      = tx_total;
    out->rx_total      = rx_total;
    out->crc_total     = crc_total;
    out->sym_total     = sym_total;
    out->err_rate_ppm  = (frames != 0u)
        ? (uint32_t)(((uint64_t)(crc_total + sym_total) * 1000000u) / frames) : 0u;
    out->uptime_s      = (uint32_t)(((xTaskGetTickCount() - s_boot_tick) * portTICK_PERIOD_MS) / 1000u);
    out->nodes_online  = online;
    out->nodes_total   = (uint8_t)(T1S_NODE_TABLE_LEN + 1u);
    return true;
}

bool T1SLink_SendToGuitar(uint8_t mask)
{
    if (!s_link_up) {
        return false;
    }
    s_cmd = mask;
    s_cmd_dirty = true;
    (void)xSemaphoreGive(s_svc_sem);  /* wake the service task to flush */
    return true;
}

bool T1SLink_SendToLemmy(int8_t neck, int8_t jaw)
{
    if (!s_link_up) {
        return false;
    }
    s_lemmy_cmd[0] = (uint8_t)neck;
    s_lemmy_cmd[1] = (uint8_t)jaw;
    s_lemmy_dirty  = true;
    (void)xSemaphoreGive(s_svc_sem);  /* wake the service task to flush */
    return true;
}

/* Stage one control opcode for a node. The value and its `known` flag are recorded
 * whether or not the link is up, so a command placed before bring-up (the boot
 * actuator defaults) is still the desired state a later resync can push — only the
 * dirty flag, which is what the flush acts on, waits for the link. */
static bool ctrl_stage(t1s_node_type_t type, uint8_t opcode, uint8_t arg)
{
    t1s_ctrl_chan_t *chan = chan_for_type(type);
    if (chan == NULL) {
        return false;
    }
    if ((opcode < 1u) || (opcode > chan->op_count)) {
        return false;
    }
    uint8_t i = (uint8_t)(opcode - 1u);
    chan->arg[i]   = arg;
    chan->known[i] = true;
    if (!s_link_up) {
        return false;
    }
    chan->dirty[i] = true;
    (void)xSemaphoreGive(s_svc_sem);  /* wake the service task to flush */
    return true;
}

/* Re-stage every opcode this channel has ever carried, so a node that just came
 * back (first sight, reconnect, or restart) is told marvin's state instead of
 * running on its own compiled-in defaults. */
static void ctrl_resync(t1s_ctrl_chan_t *chan)
{
    if (chan == NULL) { return; }
    for (uint8_t i = 0u; i < chan->op_count; i++) {
        if (chan->known[i]) { chan->dirty[i] = true; }
    }
    (void)xSemaphoreGive(s_svc_sem);
}

/* Keep a node's output gate in step with what marvin commanded. Called on every
 * heartbeat, with two triggers:
 *
 *   arriving — the node is fresh (first sight, reconnect, or a restart caught by
 *              its counters going backwards), so it is running its own defaults:
 *              re-push every opcode it has ever been told, including the ones it
 *              cannot echo (the fretboard's arm/model/teacher).
 *   mismatch — the node reports an output gate other than the commanded one, so
 *              either a frame was lost or something moved it locally. Re-push that
 *              one opcode, rate-limited to T1S_RECONCILE_MIN_MS so a node that
 *              never confirms costs a frame a second, not one per heartbeat.
 *
 * With everything agreeing this sends nothing at all, which is what keeps the
 * control channel off the bus in steady state. */
#define T1S_RECONCILE_MIN_MS    (1000u) /* min gap between re-pushes to one node  */
#define T1S_RECONCILE_WARN_HBS  (6u)    /* ~3 s of disagreement before complaining */

static void hb_reconcile_ctrl(uint8_t idx, t1s_node_type_t type, bool arriving,
                              uint32_t now_tick)
{
    t1s_ctrl_chan_t *chan = chan_for_type(type);
    if (chan == NULL) { return; }

    if (arriving) {
        ctrl_resync(chan);
        s_node_rt[idx].reconcile_tick = now_tick;
        return;   /* the resync covers OUTPUT_EN along with everything else */
    }

    /* Only the actuator channels carry an output gate the node can echo back. */
    uint8_t op = 0u;
    for (uint8_t a = 0u; a < T1S_ACT_COUNT; a++) {
        if (s_actuators[a].type == type) {
            op = s_actuators[a].output_en_op;
            break;
        }
    }
    if (op == 0u) { return; }

    uint8_t i = (uint8_t)(op - 1u);
    if (!chan->known[i]) { return; }

    if ((chan->arg[i] != 0u) == s_node_rt[idx].out_en) {
        s_node_rt[idx].reconcile_hbs  = 0u;
        s_node_rt[idx].warned_no_echo = false;
        return;
    }

    /* Disagreement. One heartbeat of it is the normal gap between a command and
     * its confirmation; a run of them means the node isn't taking it. */
    if (s_node_rt[idx].reconcile_hbs < UINT8_MAX) { s_node_rt[idx].reconcile_hbs++; }
    if ((s_node_rt[idx].reconcile_hbs >= T1S_RECONCILE_WARN_HBS) &&
        !s_node_rt[idx].warned_no_echo) {
        s_node_rt[idx].warned_no_echo = true;
        LOG_WARN("T1S: %s not confirming output enable (want %u) — node firmware "
                 "without the heartbeat echo?\r\n",
                 node_type_name(type, 0u), (unsigned)(chan->arg[i] != 0u));
    }

    if ((now_tick - s_node_rt[idx].reconcile_tick) < pdMS_TO_TICKS(T1S_RECONCILE_MIN_MS)) {
        return;
    }
    s_node_rt[idx].reconcile_tick = now_tick;
    chan->dirty[i] = true;
    (void)xSemaphoreGive(s_svc_sem);
}

bool T1SLink_SendLemmyCtrl(uint8_t opcode, uint8_t arg)
{
    return ctrl_stage(T1S_NODE_ANIMATION, opcode, arg);
}

bool T1SLink_GetLemmyCtrl(uint8_t opcode, uint8_t *arg)
{
    t1s_ctrl_chan_t *chan = chan_for_type(T1S_NODE_ANIMATION);
    if ((chan == NULL) || (opcode == 0u) || (opcode > chan->op_count)) { return false; }

    uint8_t i = (uint8_t)(opcode - 1u);
    if (!chan->known[i]) { return false; }
    if (arg != NULL) { *arg = chan->arg[i]; }
    return true;
}

bool T1SLink_SendLightshowCtrl(uint8_t opcode, uint8_t arg)
{
    return ctrl_stage(T1S_NODE_LIGHTSHOW, opcode, arg);
}

bool T1SLink_SendGuitarCtrl(uint8_t opcode, uint8_t arg)
{
    return ctrl_stage(T1S_NODE_GUITAR, opcode, arg);
}

bool T1SLink_SendFretboardCtrl(uint8_t opcode, uint8_t arg)
{
    return ctrl_stage(T1S_NODE_FRETBOARD, opcode, arg);
}

bool T1SLink_SendActuatorCtrl(t1s_actuator_t act, bool on)
{
    if (act >= T1S_ACT_COUNT) {
        return false;
    }
    return ctrl_stage(s_actuators[act].type, s_actuators[act].output_en_op,
                      on ? 1u : 0u);
}

bool T1SLink_GetActuatorState(t1s_actuator_t act, bool *present, bool *output_on)
{
    if (act >= T1S_ACT_COUNT) {
        return false;
    }
    const t1s_node_t *node = node_for_type(s_actuators[act].type);
    if (node == NULL) {
        return false;
    }
    uint8_t idx = (uint8_t)(node - s_nodes);

    if (present != NULL) {
        uint32_t age = xTaskGetTickCount() - s_node_rt[idx].last_seen_tick;
        *present = s_node_rt[idx].seen &&
                   (age < pdMS_TO_TICKS(T1S_PRESENCE_TIMEOUT_MS));
    }
    if (output_on != NULL) { *output_on = s_node_rt[idx].out_en; }
    return true;
}

void T1SLink_SetFrameHandler(T1SLink_FrameHandler handler)
{
    s_frame_handler = handler;
}

#if T1S_CTRL_ENABLED

bool T1SLink_SendToController(uint8_t type, const uint8_t *payload, uint8_t len)
{
    if (!s_link_up || (s_ctrl_queue == NULL) || (len > 8u)) {
        return false;
    }
    uint8_t item[T1S_CTRL_ITEM_MAX];
    item[0] = type;
    item[1] = len;
    if (len != 0u) { memcpy(&item[2], payload, len); }
    if (xQueueSend(s_ctrl_queue, item, 0) != pdTRUE) {
        return false;   /* FIFO full — drop (latest producer state re-sends) */
    }
    (void)xSemaphoreGive(s_svc_sem);  /* wake the service task to flush */
    return true;
}

void T1SLink_SetControllerHandler(T1SLink_ControllerHandler handler)
{
    s_ctrl_handler = handler;
}

#else  /* controller channel not compiled in (fauxmote not on T1S) */

bool T1SLink_SendToController(uint8_t type, const uint8_t *payload, uint8_t len)
{
    (void)type; (void)payload; (void)len;
    return false;
}

void T1SLink_SetControllerHandler(T1SLink_ControllerHandler handler)
{
    (void)handler;
}

#endif

/*>>>>>>>>>>>>>>>>>>>>  TC6 driver callbacks (integrator)  >>>>>>>>>>>>>>>>>>>>*/

bool TC6_CB_OnSpiTransaction(uint8_t tc6instance, uint8_t *pTx, uint8_t *pRx,
                             uint16_t len, void *pGlobalTag)
{
    (void)tc6instance;
    (void)pGlobalTag;
    /* Non-blocking full-duplex transfer; spi_done_cb calls TC6_SpiBufferDone.
     * Returns false if the PLib is busy — the driver retries. */
    return FLEXCOM3_SPI_WriteRead(pTx, len, pRx, len);
}

void TC6_CB_OnNeedService(TC6_t *pInst, void *pGlobalTag)
{
    (void)pInst;
    (void)pGlobalTag;
    /* May be called from task or ISR context; just flag. ISR sources also give
     * the semaphore, so the service task always wakes. */
    s_need_service = true;
}

void TC6_CB_OnRxEthernetSlice(TC6_t *pInst, const uint8_t *pRx, uint16_t offset,
                              uint16_t len, void *pGlobalTag)
{
    (void)pInst;
    (void)pGlobalTag;
    if (((uint32_t)offset + len) <= sizeof(s_rx_buf)) {
        memcpy(&s_rx_buf[offset], pRx, len);
    }
}

void TC6_CB_OnRxEthernetPacket(TC6_t *pInst, bool success, uint16_t len,
                               uint64_t *rxTimestamp, void *pGlobalTag)
{
    (void)pInst;
    (void)rxTimestamp;
    (void)pGlobalTag;

    if (!success || (len < T1S_ETH_HDR_LEN)) {
        return;
    }

    /* Wire accounting happens here, ahead of every filter below: promiscuous RX
     * sees the whole segment, so this is the only place that observes traffic
     * marvin isn't a party to. `len` is already the full wire frame including
     * pad and FCS — MAC_NCFGR.RFCS defaults to 0, so the MAC relays the FCS to
     * the host rather than stripping it. */
    s_wire_rx_frames++;
    s_wire_rx_bytes += len;

    uint16_t ethertype = (uint16_t)((s_rx_buf[12] << 8) | s_rx_buf[13]);
    if ((ethertype != T1S_ETHERTYPE) && (ethertype != T1S_ETHERTYPE_HB)
#if T1S_CTRL_ENABLED
        && (ethertype != T1S_ETHERTYPE_CTRL)
#endif
       ) {
        return;  /* not ours (promiscuous RX during bring-up) */
    }

    const uint8_t *src = &s_rx_buf[6];
    const t1s_node_t *node = node_for_mac(src);
    if (node == NULL) {
        LOG_DEBUG("T1S: rx from unknown node %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                  src[0], src[1], src[2], src[3], src[4], src[5]);
        return;
    }
    uint8_t idx = (uint8_t)(node - s_nodes);

    const uint8_t *payload = &s_rx_buf[T1S_ETH_HDR_LEN];
    uint16_t       payload_len = (uint16_t)(len - T1S_ETH_HDR_LEN);

    if (ethertype == T1S_ETHERTYPE_HB) {
        /* Presence heartbeat: stamp last-seen; capture the seq if present. */
        s_hb_rx_count++;
        uint32_t now_tick = xTaskGetTickCount();

        /* A node that was never seen, or whose presence window had lapsed, is
         * arriving fresh — so it is running its own compiled-in defaults and needs
         * to be told marvin's control state. Sampled before last_seen_tick moves. */
        bool arriving = !s_node_rt[idx].seen ||
                        ((now_tick - s_node_rt[idx].last_seen_tick) >=
                         pdMS_TO_TICKS(T1S_PRESENCE_TIMEOUT_MS));

        s_node_rt[idx].last_seen_tick = now_tick;
        s_node_rt[idx].seen = true;
        if (payload_len >= T1S_HB_LEN) {
            s_node_rt[idx].last_seq = (uint32_t)payload[4]
                                    | ((uint32_t)payload[5] << 8)
                                    | ((uint32_t)payload[6] << 16)
                                    | ((uint32_t)payload[7] << 24);
            /* flags bit0 = TC6 sync (unused here); bit1 = the node's own view of
             * its output gate. Only the actuator nodes set bit1; for everyone else
             * it reads 0 and nothing consults it. */
            s_node_rt[idx].out_en = (payload[3] & 0x02u) != 0u;
        }
        /* Extended (v2) heartbeat: the node's own traffic + error telemetry. A
         * legacy 8-byte heartbeat leaves these fields untouched (stay 0). */
        if (payload_len >= T1S_HB_EXT_LEN) {
            uint32_t raw_tx  = (uint32_t)payload[8]
                             | ((uint32_t)payload[9]  << 8)
                             | ((uint32_t)payload[10] << 16)
                             | ((uint32_t)payload[11] << 24);
            uint32_t raw_rx  = (uint32_t)payload[12]
                             | ((uint32_t)payload[13] << 8)
                             | ((uint32_t)payload[14] << 16)
                             | ((uint32_t)payload[15] << 24);
            uint16_t raw_crc = (uint16_t)(payload[16] | (payload[17] << 8));
            uint16_t raw_sym = (uint16_t)(payload[18] | (payload[19] << 8));

            /* Capture the baseline on first sight, and recapture if the node's
             * totals went backwards — that only happens when the node itself
             * restarted, and carrying the stale baseline would peg it at zero. */
            if (!s_node_rt[idx].based ||
                (raw_tx < s_node_rt[idx].base_tx) ||
                (raw_rx < s_node_rt[idx].base_rx)) {
                /* Counters going backwards means the node restarted, which is the
                 * one restart marvin can see without the presence window lapsing
                 * (a node that reboots fast enough never looks absent). */
                if (s_node_rt[idx].based) { arriving = true; }
                s_node_rt[idx].based    = true;
                s_node_rt[idx].base_tx  = raw_tx;
                s_node_rt[idx].base_rx  = raw_rx;
                s_node_rt[idx].base_crc = raw_crc;
                s_node_rt[idx].base_sym = raw_sym;
                /* Rate baselines too, or the next interval reports the whole
                 * jump from zero as one second's traffic. */
                s_node_rt[idx].prev_tx = 0u;
                s_node_rt[idx].prev_rx = 0u;
            }

            s_node_rt[idx].tx_count = sub_base(raw_tx, s_node_rt[idx].base_tx);
            s_node_rt[idx].rx_count = sub_base(raw_rx, s_node_rt[idx].base_rx);
            s_node_rt[idx].crc_err  = (uint16_t)sub_base(raw_crc, s_node_rt[idx].base_crc);
            s_node_rt[idx].sym_err  = (uint16_t)sub_base(raw_sym, s_node_rt[idx].base_sym);
        }

        hb_reconcile_ctrl(idx, node->type, arriving, now_tick);
        return;
    }

#if T1S_CTRL_ENABLED
    if (ethertype == T1S_ETHERTYPE_CTRL) {
        /* Controller (fauxmote) frame: payload is [TYPE][mf payload]. */
        if (payload_len >= 1u) {
            s_ctrl_rx_count++;
            if (s_ctrl_handler != NULL) {
                s_ctrl_handler(payload[0], &payload[1], (uint16_t)(payload_len - 1u));
            }
        }
        return;
    }
#endif

    s_rx_count++;
    if (s_frame_handler != NULL) {
        s_frame_handler(node->detector_id, payload, payload_len);
    }
}

void TC6_CB_OnError(TC6_t *pInst, TC6_Error_t err, void *pGlobalTag)
{
    (void)pGlobalTag;
    LOG_WARN("T1S: error: %s\r\n", TC6_GetErrorStr(err));
    switch (err) {
        case TC6Error_NoHardware:
        case TC6Error_BadChecksum:
        case TC6Error_UnexpectedCtrl:
        case TC6Error_BadTxData:
        case TC6Error_SyncLost:
        case TC6Error_SpiError:
            TC6Regs_Reinit(pInst);
            break;
        default:
            break;
    }
}

/*>>>>>>>>>>>>>>>>>>>>  TC6Regs callbacks (integrator)  >>>>>>>>>>>>>>>>>>>>>>>*/

uint32_t TC6Regs_CB_GetTicksMs(void)
{
    /* configTICK_RATE_HZ is 1000, so one tick is one millisecond. */
    return (uint32_t)xTaskGetTickCount();
}

void TC6Regs_CB_OnEvent(TC6_t *pInst, TC6Regs_Event_t event, void *pTag)
{
    /* Each fatal bring-up rejection logs once: both clear the TC6 lib's
     * initialized flag, so bring-up fails and retries every T1S_ABSENT_RETRY_MS. */
    static bool logged_unsupported_hw;
    static bool logged_chip_error;

    (void)pTag;
//    LOG_INFO("T1S: event: %s\r\n", TC6Regs_GetEventStr(event));
    switch (event) {
        case TC6Regs_Event_Unsupported_Hardware:
            if (!logged_unsupported_hw) {
                logged_unsupported_hw = true;
                LOG_ERROR("T1S: unsupported MAC-PHY — PHYID OUI/model mismatch or "
                          "chipRev 0. Not a LAN865x, or SPI reads are garbage.\r\n");
            }
            break;
        case TC6Regs_Event_Chip_Error:
            if (!logged_chip_error) {
                logged_chip_error = true;
                LOG_ERROR("T1S: MAC-PHY OTP config invalid (trim registers out of "
                          "range) — unsupported silicon revision, or marginal SPI "
                          "at %u Hz.\r\n", (unsigned)T1S_SPI_HZ);
            }
            break;
        case TC6Regs_Event_Transmit_Frame_Check_Sequence_Error:
            s_self_crc_err++;   /* marvin's own FCS error tally (self row) */
            break;
        case TC6Regs_Event_Loss_of_Framing_Error:
            s_self_sym_err++;   /* framing (symbol) error — also fatal, reinit below */
            TC6Regs_Reinit(pInst);
            break;
        case TC6Regs_Event_RX_Non_Recoverable_Error:
        case TC6Regs_Event_TX_Non_Recoverable_Error:
            TC6Regs_Reinit(pInst);
            break;
        default:
            break;
    }
}
