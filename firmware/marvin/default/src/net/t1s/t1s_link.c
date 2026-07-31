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
#include "detector/detector.h"  /* DETECTOR_ADC_FRETBOARD */
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
#define T1S_ETHERTYPE_ANIM_CTRL (0x88B9u) /* lemmy (animation) control channel */
#define T1S_ETH_HDR_LEN      (14u)
#define T1S_MAC_LEN          (6u)
#define T1S_HB_LEN           (8u)        /* ver, type, id, flags, seq_u32 */
#define T1S_PRESENCE_TIMEOUT_MS (2000u) /* node "present" if a HB seen within this */

/* Locally administered coordinator MAC (02:00:00:00:00:00). */
static uint8_t s_mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, (uint8_t)T1S_NODE_ID };

/* Static node directory. No discovery — adding a node is a table entry.
 * Detector nodes feed a detector-state bus id (RX source); guitar (actuator)
 * nodes are command TX targets (detector_id unused, set to 0xFF). */
#define T1S_NO_DETECTOR     (0xFFu)

typedef enum
{
    T1S_NODE_FRETBOARD,      /* detector: photo-ADC stream -> detector bus */
    T1S_NODE_PHOTODETECTOR,  /* detector (future variants) */
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
    { 4u, (uint8_t)DETECTOR_ADC_FRETBOARD, T1S_NODE_FRETBOARD },  /* detector (RX) */
    { 3u, T1S_NO_DETECTOR,                 T1S_NODE_GUITAR },     /* actuator (TX target) */
    { 6u, T1S_NO_DETECTOR,                 T1S_NODE_ANIMATION },  /* lemmy (heartbeat) */
    { 7u, T1S_NO_DETECTOR,                 T1S_NODE_LIGHTSHOW },  /* lightshow (heartbeat) */
    { 5u, T1S_NO_DETECTOR,                 T1S_NODE_BEATSOURCE }, /* beatbox (heartbeat) */
#if T1S_CTRL_ENABLED
    { 1u, T1S_NO_DETECTOR,                 T1S_NODE_CONTROLLER }, /* fauxmote (0x88B7) */
#endif
};

#define T1S_NODE_TABLE_LEN  (sizeof(s_nodes) / sizeof(s_nodes[0]))

/* Per-node runtime presence (parallel to s_nodes), updated on heartbeat RX. */
static struct {
    uint32_t last_seen_tick;
    uint32_t last_seq;
    bool     seen;
} s_node_rt[T1S_NODE_TABLE_LEN];

static const char *node_type_name(t1s_node_type_t t)
{
    switch (t) {
        case T1S_NODE_FRETBOARD:     return "detector";
        case T1S_NODE_PHOTODETECTOR: return "detector";
        case T1S_NODE_GUITAR:        return "guitar";
        case T1S_NODE_CONTROLLER:    return "controller";
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

/* Lemmy control channel (0x88B9): typed [opcode, arg] commands that tune the
 * beat nod. Staged per-opcode (indexed by opcode-1) rather than latest-wins so
 * distinct commands can't drop each other; one frame flushed per service pass. */
static volatile uint8_t s_lemmy_ctrl_arg[T1S_ANIM_CTRL_OP_COUNT];
static volatile bool    s_lemmy_ctrl_dirty[T1S_ANIM_CTRL_OP_COUNT];

static T1SLink_FrameHandler s_frame_handler;

/* Traffic counters (read by the console t1s/nodes commands). */
static volatile uint32_t s_tx_count;   /* command frames sent */
static volatile uint32_t s_rx_count;   /* frames received from a known node */
static volatile uint32_t s_service_overruns; /* service_pump hit its iter cap (stuck MAC-PHY) */

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

        /* Flush one staged lemmy control command (0x88B9). One opcode per pass;
         * the rest drain on the next service wake. Shares the single in-flight TX. */
        if (!s_tx_busy) {
            for (uint8_t i = 0u; i < T1S_ANIM_CTRL_OP_COUNT; i++) {
                if (!s_lemmy_ctrl_dirty[i]) { continue; }
                const t1s_node_t *lemmy = node_for_type(T1S_NODE_ANIMATION);
                if (lemmy == NULL) { break; }
                s_lemmy_ctrl_dirty[i] = false;
                uint8_t frame[2] = { (uint8_t)(i + 1u), s_lemmy_ctrl_arg[i] };
                if (send_to_node(lemmy->node_id, T1S_ETHERTYPE_ANIM_CTRL, frame, 2u)) {
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
    out->type    = node_type_name(s_nodes[idx].type);
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

bool T1SLink_SendLemmyCtrl(uint8_t opcode, uint8_t arg)
{
    if (!s_link_up) {
        return false;
    }
    if ((opcode < 1u) || (opcode > T1S_ANIM_CTRL_OP_COUNT)) {
        return false;
    }
    s_lemmy_ctrl_arg[opcode - 1u]   = arg;
    s_lemmy_ctrl_dirty[opcode - 1u] = true;
    (void)xSemaphoreGive(s_svc_sem);  /* wake the service task to flush */
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
        s_node_rt[idx].last_seen_tick = xTaskGetTickCount();
        s_node_rt[idx].seen = true;
        if (payload_len >= T1S_HB_LEN) {
            s_node_rt[idx].last_seq = (uint32_t)payload[4]
                                    | ((uint32_t)payload[5] << 8)
                                    | ((uint32_t)payload[6] << 16)
                                    | ((uint32_t)payload[7] << 24);
        }
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
    (void)pTag;
//    LOG_INFO("T1S: event: %s\r\n", TC6Regs_GetEventStr(event));
    switch (event) {
        case TC6Regs_Event_Loss_of_Framing_Error:
        case TC6Regs_Event_RX_Non_Recoverable_Error:
        case TC6Regs_Event_TX_Non_Recoverable_Error:
            TC6Regs_Reinit(pInst);
            break;
        default:
            break;
    }
}
