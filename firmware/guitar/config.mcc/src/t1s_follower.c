#include "t1s_follower.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "definitions.h"   /* SERCOM0_SPI_*, SERCOM1_USART_*, EIC_*, PORT macros, CMSIS */

#include "tc6.h"
#include "tc6-regs.h"

/* PLCA follower identity (docs/t1s-podl-link.md §7.1). */
#define T1S_NODE_ID         (3u)
#define T1S_NODE_COUNT      (8u)     /* PLCA cycle length (must match the coordinator) */
#define T1S_INSTANCE        (0u)

/* PLCA_STATUS register (MMS 4, addr 0xCA03); bit 15 = plca_status: PLCA is
 * operating (coordinator beacon seen on the wire). This is the real "on the
 * bus" signal, distinct from local MAC-PHY init-done. Polled in the background. */
#define T1S_PLCA_STATUS_REG (0x0004CA03u)
#define T1S_PLCA_POLL_MS    (250u)

#define T1S_ETHERTYPE       (0x88B5u)  /* data / command frames */
#define T1S_ETHERTYPE_HB    (0x88B6u)  /* heartbeat / presence frames */
#define T1S_ETHERTYPE_CTRL  (0x88B9u)  /* per-node control channel (unicast) */
#define T1S_ETH_HDR_LEN     (14u)
#define T1S_CMD_BIT_MASK    (0x7Fu)  /* 5 frets + 2 strum */

/* Control channel (0x88B9): typed [opcode, arg]. Opcode namespace is per-node
 * (routed by dst MAC); the guitar's own opcode(s) below. */
#define T1S_CTRL_OP          (0u)     /* payload offset: opcode */
#define T1S_CTRL_ARG         (1u)     /* payload offset: arg    */
#define T1S_CTRL_LEN         (2u)     /* min control payload length */
#define T1S_CTRL_OUTPUT_EN   (0x01u)  /* arg 0|1: enable/disable the button outputs */

/* Heartbeat (docs/t1s-podl-link.md §7.2): followers periodically announce
 * presence to the coordinator. v2 payload (20 B): ver, node_type, node_id, flags,
 * seq_u32, then telemetry the coordinator's bus-stats UI reads —
 * tx_count_u32, rx_count_u32, crc_err_u16, sym_err_u16 (all little-endian). */
#define T1S_HB_INTERVAL_MS  (500u)
#define T1S_HB_VERSION      (2u)
#define T1S_HB_TYPE_GUITAR  (2u)     /* 1 = detector, 2 = guitar (shared codes) */
#define T1S_HB_LEN          (20u)

/* Coordinator-assigned MAC for this node: 02:00:00:00:00:03. */
static uint8_t s_mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, (uint8_t)T1S_NODE_ID };

/* Coordinator (marvin) MAC: 02:00:00:00:00:00 — heartbeat destination. */
static const uint8_t s_coord_mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u };

static TC6_t            *s_tc6;
static volatile bool     s_need_service;
static volatile bool     s_initialized;  /* MAC-PHY register bring-up done + data path enabled */
static volatile bool     s_plca_op;      /* cached PLCA_STATUS bit 15 (PLCA operating on the bus) */
static uint32_t          s_plca_poll_ms;
static volatile bool     s_spi_busy;

/* Output gate: while clear, a received mask is recorded but not driven onto the
 * button GPIOs. Authoritative wherever the command comes from — marvin's 0x88B5
 * frames or the fretboard driving the guitar peer-to-peer. Default on so the node
 * plays with no coordinator; marvin pushes its own state over 0x88B9. */
static volatile bool     s_output_en = true;

/* Diagnostics (read by the CLI + reported in the extended heartbeat). */
static volatile uint8_t  s_last_cmd;
static volatile uint8_t  s_last_ctrl_op;   /* last 0x88B9 control opcode applied */
static volatile uint8_t  s_last_ctrl_arg;
static volatile uint32_t s_ctrl_count;     /* accepted control frames */
static volatile uint32_t s_rx_count;    /* all frames received (any ethertype) */
static volatile uint32_t s_tx_count;    /* frames this node has completed sending */
static volatile uint32_t s_err_count;   /* total TC6 driver errors since boot */
static volatile uint16_t s_crc_err;     /* MAC-PHY FCS errors (TC6Regs event) */
static volatile uint16_t s_sym_err;     /* MAC-PHY loss-of-framing (symbol) errors */
static uint32_t          s_last_diag_ms; /* rate-limit window for diag logs */

/* Command frames are ~60 B after min-frame padding; this only needs the header
 * plus the first payload byte, but size for a padded frame. */
static uint8_t           s_rx_buf[64];

/* Heartbeat TX staging (buffer must stay valid until the TX callback fires). */
static uint8_t           s_hb_frame[T1S_ETH_HDR_LEN + T1S_HB_LEN];
static volatile bool     s_hb_busy;
static uint32_t          s_hb_seq;
static uint32_t          s_hb_last_ms;

/* The 1 ms time base is the MCC SYSTICK plib: SYS_Initialize runs
 * SYSTICK_TimerInitialize; this module starts it and reads
 * SYSTICK_GetTickCounter() (milliseconds) / SYSTICK_DelayMs(). */

static void log_str(const char *s)
{
    (void)SERCOM1_USART_Write((uint8_t *)s, strlen(s));
}

/* Rate-limited diagnostic line (<= ~1/sec) so a disconnected/erroring link
 * can't flood the console — TC6_Service raises an error every pass when no
 * MAC-PHY answers. Errors/events share the window. */
static void diag_log(const char *prefix, const char *msg)
{
    uint32_t now = SYSTICK_GetTickCounter();
    if ((now - s_last_diag_ms) < 1000u) { return; }
    s_last_diag_ms = now;
    log_str(prefix);
    log_str(msg);
    log_str("\r\n");
}

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>  Status indicators  >>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

/* Active-high LED indicators: assert = drive high (Set), release = drive low
 * (Clear). Pins boot as outputs driving low (off) per PORT_Initialize. */
#define BTN_APPLY(mask, bit, NAME)                          \
    do {                                                    \
        if ((mask) & (1u << (bit))) {                       \
            NAME##_Set();                                   \
        } else {                                            \
            NAME##_Clear();                                 \
        }                                                   \
    } while (0)

static void buttons_release_all(void)
{
    FRET_GREEN_Clear();
    FRET_RED_Clear();
    FRET_YELLOW_Clear();
    FRET_BLUE_Clear();
    FRET_ORANGE_Clear();
    STRUM_Clear();
}

static void buttons_apply_mask(uint8_t mask)
{
    if (!s_output_en) {
        buttons_release_all();
        return;
    }
    BTN_APPLY(mask, 0u, FRET_GREEN);
    BTN_APPLY(mask, 1u, FRET_RED);
    BTN_APPLY(mask, 2u, FRET_YELLOW);
    BTN_APPLY(mask, 3u, FRET_BLUE);
    BTN_APPLY(mask, 4u, FRET_ORANGE);
    /* Doc collapses strum up/down to a single STRUM strobe indicator. */
    if ((mask) & ((1u << 5) | (1u << 6))) {
        STRUM_Set();
    } else {
        STRUM_Clear();
    }
}

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>  SPI + IRQ  >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

/* SERCOM0 SPI completion ISR: the chunk transfer is done — deassert the GPIO
 * chip-select, hand the buffer back to the driver, and flag for servicing. */
static void spi_done_cb(uintptr_t context)
{
    (void)context;
    T1S_CS_Set();            /* CS high — end of transaction */
    s_spi_busy = false;
    s_need_service = true;
    TC6_SpiBufferDone(T1S_INSTANCE, true);
}

/* T1S_IRQ_N falling-edge (EIC EXTINT2): the MAC-PHY needs servicing. */
static void irq_cb(uintptr_t context)
{
    (void)context;
    s_need_service = true;
}

/* One service pass: drive the protocol stack once and check its timers. The
 * caller (main loop, or the bounded loops in init/ReadId) invokes this
 * repeatedly. Do NOT loop here on s_need_service — with hardware that never
 * syncs the lib re-requests service every pass, which would spin forever.
 * IRQ_N is active-low; TC6_Service treats a false interruptLevel as
 * "interrupt active". */
static void service_pump(void)
{
    s_need_service = false;
    bool no_int = (T1S_IRQ_N_Get() != 0u);
    (void)TC6_Service(s_tc6, no_int);
    TC6Regs_CheckTimers();
}

/* Heartbeat TX completion: free the staging buffer. */
static void hb_tx_done(TC6_t *pInst, const uint8_t *pTx, uint16_t len,
                       void *pTag, void *pGlobalTag)
{
    (void)pInst;
    (void)pTx;
    (void)len;
    (void)pTag;
    (void)pGlobalTag;
    s_hb_busy = false;
    s_tx_count++;   /* count completed transmits (heartbeats — this node's only TX) */
}

/* Announce presence to the coordinator (ethertype 0x88B6). */
static void send_heartbeat(void)
{
    if (s_hb_busy || !s_plca_op) {
        return;
    }
    bool synced = false;
    TC6_GetState(s_tc6, NULL, NULL, &synced);

    memcpy(&s_hb_frame[0], s_coord_mac, 6u);   /* dst = coordinator */
    memcpy(&s_hb_frame[6], s_mac, 6u);         /* src = this node   */
    s_hb_frame[12] = (uint8_t)(T1S_ETHERTYPE_HB >> 8);
    s_hb_frame[13] = (uint8_t)(T1S_ETHERTYPE_HB & 0xFFu);
    s_hb_frame[14] = T1S_HB_VERSION;
    s_hb_frame[15] = T1S_HB_TYPE_GUITAR;
    s_hb_frame[16] = (uint8_t)T1S_NODE_ID;
    /* flags: bit0 = TC6 synced, bit1 = output gate. The coordinator reconciles
     * bit1 against what it last commanded, so this is how a local `output` change
     * or a reboot gets corrected instead of silently disagreeing. */
    s_hb_frame[17] = (uint8_t)((synced ? 0x01u : 0x00u) |
                              (s_output_en ? 0x02u : 0x00u));
    s_hb_seq++;
    s_hb_frame[18] = (uint8_t)(s_hb_seq);
    s_hb_frame[19] = (uint8_t)(s_hb_seq >> 8);
    s_hb_frame[20] = (uint8_t)(s_hb_seq >> 16);
    s_hb_frame[21] = (uint8_t)(s_hb_seq >> 24);

    /* v2 telemetry (payload offsets 8..19), little-endian. Snapshot the counters
     * before this heartbeat's own TX completes (its off-by-one is negligible). */
    uint32_t tx = s_tx_count, rx = s_rx_count;
    uint16_t crc = s_crc_err, sym = s_sym_err;
    s_hb_frame[22] = (uint8_t)(tx);
    s_hb_frame[23] = (uint8_t)(tx >> 8);
    s_hb_frame[24] = (uint8_t)(tx >> 16);
    s_hb_frame[25] = (uint8_t)(tx >> 24);
    s_hb_frame[26] = (uint8_t)(rx);
    s_hb_frame[27] = (uint8_t)(rx >> 8);
    s_hb_frame[28] = (uint8_t)(rx >> 16);
    s_hb_frame[29] = (uint8_t)(rx >> 24);
    s_hb_frame[30] = (uint8_t)(crc);
    s_hb_frame[31] = (uint8_t)(crc >> 8);
    s_hb_frame[32] = (uint8_t)(sym);
    s_hb_frame[33] = (uint8_t)(sym >> 8);

    s_hb_busy = true;
    if (!TC6_SendRawEthernetPacket(s_tc6, s_hb_frame, T1S_ETH_HDR_LEN + T1S_HB_LEN,
                                   0u, hb_tx_done, NULL)) {
        s_hb_busy = false;
    }
}

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>  Public API  >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

void T1SFollower_Initialize(void)
{
    SYSTICK_TimerStart();    /* MCC inits the timer; the app enables it */

    log_str("guitar: boot - t1s follower + cli\r\n");  /* one-time banner */

    buttons_release_all();   /* indicators off (drive low) at boot */

    /* Hardware reset pulse (T1S_RST active-low, idle high). */
    T1S_CS_Set();
    T1S_RST_Clear();
    SYSTICK_DelayMs(10u);
    T1S_RST_Set();
    SYSTICK_DelayMs(10u);

    SERCOM0_SPI_CallbackRegister(spi_done_cb, 0u);  /* Mode 0 set by MCC init */

    s_tc6 = TC6_Init(NULL);
    if (s_tc6 == NULL) {
        log_str("guitar: TC6_Init failed\r\n");
        return;
    }

    EIC_CallbackRegister(EIC_PIN_2, irq_cb, 0u);   /* EXTINT2 enabled in EIC_Initialize */

    /* Configure the LAN8651 + PLCA as follower id 3. Not promiscuous — the
     * MAC-PHY filters to this node's MAC + broadcast. Non-blocking: the
     * register sequence finishes in the background via T1SFollower_Tasks, so
     * the CLI is never gated behind the link coming up. */
    if (!TC6Regs_Init(s_tc6, NULL, s_mac, true, T1S_NODE_ID, T1S_NODE_COUNT,
                      0u, 0u, false, false, false)) {
        log_str("guitar: TC6Regs_Init rejected\r\n");
    }
}

/* Background PLCA_STATUS poll result: cache bit 15 as the on-bus flag. */
static void on_plca_status(TC6_t *pInst, bool success, uint32_t addr, uint32_t value,
                           void *pTag, void *pGlobalTag)
{
    (void)pInst;
    (void)addr;
    (void)pTag;
    (void)pGlobalTag;
    s_plca_op = success && ((value & (1uL << 15)) != 0u);
}

void T1SFollower_Tasks(void)
{
    if (s_tc6 == NULL) {
        return;
    }
    service_pump();
    if (!s_initialized && TC6Regs_GetInitDone(s_tc6)) {
        s_initialized = true;
        TC6_EnableData(s_tc6, true);
        char buf[80];
        (void)snprintf(buf, sizeof(buf),
                       "guitar: LAN8651 up - chipRev=%u, MAC=02:00:00:00:00:%02X, "
                       "PLCA follower id=%u/%u\r\n",
                       (unsigned)TC6Regs_GetChipRevision(s_tc6),
                       (unsigned)T1S_NODE_ID, (unsigned)T1S_NODE_ID,
                       (unsigned)T1S_NODE_COUNT);
        log_str(buf);
    }

    /* Once initialized, poll PLCA_STATUS in the background so IsConnected /
     * heartbeat reflect real on-bus state (coordinator beacon present), not
     * just local init. A follower has no transmit slot until PLCA operates. */
    if (s_initialized) {
        uint32_t now = SYSTICK_GetTickCounter();
        if ((now - s_plca_poll_ms) >= T1S_PLCA_POLL_MS) {
            s_plca_poll_ms = now;
            (void)TC6_ReadRegister(s_tc6, T1S_PLCA_STATUS_REG, true, on_plca_status, NULL);
        }
    }

    /* Periodic presence heartbeat to the coordinator — gated on PLCA operating. */
    if (s_plca_op) {
        uint32_t now = SYSTICK_GetTickCounter();
        if ((now - s_hb_last_ms) >= T1S_HB_INTERVAL_MS) {
            s_hb_last_ms = now;
            send_heartbeat();
        }
    }
}

bool T1SFollower_IsConnected(void)
{
    return s_plca_op;
}

uint8_t T1SFollower_ChipRev(void)
{
    return (s_tc6 != NULL) ? TC6Regs_GetChipRevision(s_tc6) : 0u;
}

uint8_t T1SFollower_LastCmd(void)
{
    return s_last_cmd;
}

uint32_t T1SFollower_RxCount(void)
{
    return s_rx_count;
}

uint32_t T1SFollower_ErrCount(void)
{
    return s_err_count;
}

void T1SFollower_ApplyButtons(uint8_t mask)
{
    buttons_apply_mask((uint8_t)(mask & T1S_CMD_BIT_MASK));
}

void T1SFollower_ReleaseButtons(void)
{
    buttons_release_all();
}

void T1SFollower_SetOutputEnabled(bool en)
{
    s_output_en = en;
    if (!en) {
        /* Release now rather than waiting for the next command, so a mask that was
         * asserted when the gate closed can't stay held on the guitar. */
        buttons_release_all();
    } else {
        buttons_apply_mask(s_last_cmd);
    }
}

bool T1SFollower_IsOutputEnabled(void)
{
    return s_output_en;
}

void T1SFollower_LastCtrl(uint8_t *op, uint8_t *arg, uint32_t *count)
{
    if (op    != NULL) { *op    = s_last_ctrl_op; }
    if (arg   != NULL) { *arg   = s_last_ctrl_arg; }
    if (count != NULL) { *count = s_ctrl_count; }
}

/* Diagnostic: log the raw value of a control register (async — the result
 * prints from the service loop a moment later). */
static void on_id_read(TC6_t *pInst, bool success, uint32_t addr, uint32_t value,
                       void *pTag, void *pGlobalTag)
{
    (void)pInst;
    (void)pTag;
    (void)pGlobalTag;
    char buf[88];
    if (addr == 0x00000001u) {
        /* PHY id register — oui/model are meaningful here (lib wants 0x1F0/0x1B). */
        (void)snprintf(buf, sizeof(buf),
                       "guitar: reg 0x%08lX = 0x%08lX (ok=%d, oui=0x%03lX model=0x%02lX)\r\n",
                       (unsigned long)addr, (unsigned long)value, (int)success,
                       (unsigned long)(value >> 10), (unsigned long)((value >> 4) & 0x3FFu));
    } else {
        (void)snprintf(buf, sizeof(buf), "guitar: reg 0x%08lX = 0x%08lX (ok=%d)\r\n",
                       (unsigned long)addr, (unsigned long)value, (int)success);
    }
    log_str(buf);
}

void T1SFollower_ReadId(void)
{
    /* 0x00 = OA IDVER, 0x01 = PHY id (lib expects oui 0x1F0 / model 0x1B),
     * 0x000A0094 = chip rev. */
    static const uint32_t addrs[3] = { 0x00000000u, 0x00000001u, 0x000A0094u };

    if (s_tc6 == NULL) {
        log_str("guitar: t1s not initialized\r\n");
        return;
    }
    /* Just enqueue; the main service loop completes the reads and on_id_read
     * logs each result a moment later. Don't hammer TC6_Service here — doing so
     * while the link is live can trip a transient Loss_of_Framing. */
    for (uint8_t i = 0u; i < 3u; i++) {
        (void)TC6_ReadRegister(s_tc6, addrs[i], false, on_id_read, NULL);
    }
}

void T1SFollower_GetState(bool *synced, uint8_t *txCredit, uint8_t *rxCredit)
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

uint8_t T1SFollower_NodeId(void)    { return (uint8_t)T1S_NODE_ID; }
uint8_t T1SFollower_NodeCount(void) { return (uint8_t)T1S_NODE_COUNT; }

/* Async read of the PLCA status register (bit 15 = plca_status). Result logs
 * from the service loop a moment later. */
static void on_plca_read(TC6_t *pInst, bool success, uint32_t addr, uint32_t value,
                         void *pTag, void *pGlobalTag)
{
    (void)pInst;
    (void)addr;
    (void)pTag;
    (void)pGlobalTag;
    char buf[72];
    (void)snprintf(buf, sizeof(buf),
                   "guitar: PLCA status = 0x%08lX (plca_status=%d)\r\n",
                   (unsigned long)value,
                   (int)(success && ((value & (1uL << 15)) != 0u)));
    log_str(buf);
}

void T1SFollower_ReadPlca(void)
{
    if (s_tc6 == NULL) {
        log_str("guitar: t1s not initialized\r\n");
        return;
    }
    /* Enqueue only; the main service loop completes it (see T1SFollower_ReadId). */
    (void)TC6_ReadRegister(s_tc6, T1S_PLCA_STATUS_REG, true, on_plca_read, NULL);
}

/*>>>>>>>>>>>>>>>>>>>>  TC6 driver callbacks (integrator)  >>>>>>>>>>>>>>>>>>>>*/

bool TC6_CB_OnSpiTransaction(uint8_t tc6instance, uint8_t *pTx, uint8_t *pRx,
                             uint16_t len, void *pGlobalTag)
{
    (void)tc6instance;
    (void)pGlobalTag;
    if (s_spi_busy) {
        return false;
    }
    s_spi_busy = true;
    T1S_CS_Clear();          /* CS low — start of transaction (held across the chunk) */
    if (!SERCOM0_SPI_WriteRead(pTx, len, pRx, len)) {
        T1S_CS_Set();
        s_spi_busy = false;
        return false;
    }
    return true;
}

void TC6_CB_OnNeedService(TC6_t *pInst, void *pGlobalTag)
{
    (void)pInst;
    (void)pGlobalTag;
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
    s_rx_count++;   /* total frames received (any ethertype), for bus-stats RX total */
    if (len < (T1S_ETH_HDR_LEN + 1u)) {
        return;
    }
    uint16_t ethertype = (uint16_t)((s_rx_buf[12] << 8) | s_rx_buf[13]);
    if (ethertype == T1S_ETHERTYPE_CTRL) {
        /* Typed control command [opcode, arg]. Guard with `<` (the MAC pads short
         * frames to the 60-byte minimum, so len counts trailing pad and never
         * equals the payload length), read fixed offsets, dispatch. */
        if (len < (T1S_ETH_HDR_LEN + T1S_CTRL_LEN)) {
            return;
        }
        uint8_t op  = s_rx_buf[T1S_ETH_HDR_LEN + T1S_CTRL_OP];
        uint8_t arg = s_rx_buf[T1S_ETH_HDR_LEN + T1S_CTRL_ARG];
        switch (op) {
            case T1S_CTRL_OUTPUT_EN:  T1SFollower_SetOutputEnabled(arg != 0u); break;
            default: return;   /* unknown opcode: ignore, don't count */
        }
        s_last_ctrl_op  = op;
        s_last_ctrl_arg = arg;
        s_ctrl_count++;
        return;
    }
    if (ethertype != T1S_ETHERTYPE) {
        return;
    }
    /* Command byte is the first payload octet; trailing min-frame padding is
     * ignored. Recorded even while the output is gated, so the CLI still shows
     * what the coordinator is asking for. Apply directly (latest-wins). */
    uint8_t mask = (uint8_t)(s_rx_buf[T1S_ETH_HDR_LEN] & T1S_CMD_BIT_MASK);
    s_last_cmd = mask;
    buttons_apply_mask(mask);
}

void TC6_CB_OnError(TC6_t *pInst, TC6_Error_t err, void *pGlobalTag)
{
    (void)pGlobalTag;
    s_err_count++;
    diag_log("guitar: t1s error: ", TC6_GetErrorStr(err));
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
    return SYSTICK_GetTickCounter();
}

void TC6Regs_CB_OnEvent(TC6_t *pInst, TC6Regs_Event_t event, void *pTag)
{
    (void)pTag;
    diag_log("guitar: t1s event: ", TC6Regs_GetEventStr(event));
    switch (event) {
        case TC6Regs_Event_Transmit_Frame_Check_Sequence_Error:
            s_crc_err++;   /* reported in the extended heartbeat */
            break;
        case TC6Regs_Event_Loss_of_Framing_Error:
            s_sym_err++;
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
