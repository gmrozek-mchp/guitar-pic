#include "t1s_detector.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "definitions.h"   /* SERCOM0_SPI_*, SERCOM1_USART_*, EIC_*, PORT macros, CMSIS */

#include "tc6.h"
#include "tc6-regs.h"

/* PLCA follower identity (docs/t1s-podl-link.md §7.1). */
#define T1S_NODE_ID         (4u)
#define T1S_NODE_COUNT      (8u)     /* PLCA cycle length (must match the coordinator) */
#define T1S_INSTANCE        (0u)
#define T1S_GUITAR_ID       (3u)     /* actuator node this detector drives */

/* IRQ_N external-interrupt line. Must match the MCC EIC pin wired to T1S_IRQ_N. */
#define T1S_IRQ_EIC_PIN     EIC_PIN_13

#define T1S_ETHERTYPE       (0x88B5u)  /* data / command frames */
#define T1S_ETHERTYPE_HB    (0x88B6u)  /* heartbeat / presence frames */
#define T1S_ETHERTYPE_CTRL  (0x88B9u)  /* per-node control channel (unicast) */
#define T1S_ETH_HDR_LEN     (14u)
#define T1S_CMD_BIT_MASK    (0x7Fu)    /* 5 frets + 2 strum */

/* Control channel (0x88B9): typed [opcode, arg]. Opcode namespace is per-node
 * (routed by dst MAC); the detector's own opcode(s) below. */
#define T1S_CTRL_OP          (0u)     /* payload offset: opcode */
#define T1S_CTRL_ARG         (1u)     /* payload offset: arg    */
#define T1S_CTRL_LEN         (2u)     /* min control payload length */
#define T1S_CTRL_ARM         (0x01u)  /* arg 0|1: gate actuation (marvin selects) */
#define T1S_CTRL_STREAM      (0x02u)  /* arg 0|1: gate the data stream to marvin */

/* Re-send the current guitar command this often even if unchanged, so a dropped
 * command frame self-heals (the guitar applies latest-wins, holds otherwise). */
#define T1S_CMD_REFRESH_MS  (50u)

/* Heartbeat (docs/t1s-podl-link.md §7.2): followers periodically announce
 * presence to the coordinator. Payload: ver, node_type, node_id, flags, seq_u32. */
#define T1S_HB_INTERVAL_MS  (500u)
#define T1S_HB_VERSION      (1u)
#define T1S_HB_TYPE_DETECTOR (1u)    /* 1 = detector, 2 = guitar (shared codes) */
#define T1S_HB_LEN          (8u)

/* PLCA_STATUS register: bit 15 (plca_status) = PLCA operating (coordinator beacon
 * on the wire). Polled in the background so IsConnected / the CLI report real
 * on-bus state, not just local MAC-PHY init. */
#define T1S_PLCA_STATUS_REG (0x0004CA03u)
#define T1S_PLCA_POLL_MS    (250u)

/* Coordinator-assigned MAC for this node: 02:00:00:00:00:04. */
static uint8_t s_mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, (uint8_t)T1S_NODE_ID };

/* Coordinator (marvin) MAC: 02:00:00:00:00:00 — data + heartbeat destination. */
static const uint8_t s_coord_mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u };

/* Guitar (actuator) node MAC: 02:00:00:00:00:03 — command destination. */
static const uint8_t s_guitar_mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, (uint8_t)T1S_GUITAR_ID };

static TC6_t            *s_tc6;
static volatile bool     s_need_service;
static volatile bool     s_initialized;  /* MAC-PHY register bring-up done + data path enabled */
static volatile bool     s_spi_busy;

/* Cached PLCA operating status (PLCA_STATUS bit 15), refreshed by a periodic
 * background register read. Unlike s_initialized (local MAC-PHY config done), this
 * only asserts when a coordinator beacon is on the wire — the real "on the bus"
 * signal. Gates all TX: a follower has no transmit opportunity without the beacon,
 * so sending earlier just queues a frame that never drains. */
static volatile bool     s_plca_op;
static uint32_t          s_plca_poll_ms;

/* Diagnostics (boot banner / CLI). */
static volatile uint32_t s_tx_count;    /* data frames sent to the coordinator */
static volatile uint32_t s_cmd_count;   /* command frames sent to the guitar */
static volatile uint8_t  s_last_cmd;    /* most recent command bitmask sent */
static volatile uint32_t s_err_count;   /* total TC6 errors since boot */
static uint32_t          s_last_diag_ms; /* rate-limit window for diag logs */

/* Actuation-armed state. Written by either the local CLI (T1SDetector_SetArmed)
 * or marvin's control channel (0x88B9 opcode 0x01) — last writer wins, no lockout.
 * Read by the main-loop actuation gate. Boots disarmed. */
static volatile bool     s_armed;
static volatile uint8_t  s_last_ctrl_op;   /* last 0x88B9 opcode applied */
static volatile uint8_t  s_last_ctrl_arg;
static volatile uint32_t s_ctrl_count;     /* accepted control frames */

/* RX is unused on this node (non-promiscuous), but the integrator callbacks must
 * exist; a small buffer absorbs any slice the MAC-PHY delivers. */
static uint8_t           s_rx_buf[64];

/* Data-frame TX (to coordinator). The 240 Hz scan tick stages a frame via
 * T1SDetector_SendFrame (ISR context: writes s_pending + sets s_frame_ready). The
 * main loop copies it into s_tx_frame (header prebuilt) and sends — one in-flight
 * TX guarded by s_tx_busy. Both buffers must stay valid until the TX callback. */
static uint8_t           s_tx_frame[T1S_ETH_HDR_LEN + 64u];
static volatile uint8_t  s_pending[32];
static volatile uint16_t s_pending_len;
static volatile bool     s_frame_ready;
static volatile bool     s_tx_busy;

/* Data stream to marvin (0x88B5) gate. Boots disabled — marvin turns it on over
 * the control channel (opcode 0x02) when it wants the logging / edge-ai feed.
 * sample_seq keeps advancing while disabled, so the first frame after re-enable
 * shows the true gap. */
static volatile bool     s_stream_enabled;

/* Command TX (to guitar). Set from the main loop (T1SDetector_SetCommand); flushed
 * by T1SDetector_Tasks on change + every T1S_CMD_REFRESH_MS while enabled. When the
 * actuation gate opens/closes the enable follows: disarming emits one final
 * all-released frame then goes silent, so a disarmed node never contends for the
 * guitar with another command source (e.g. marvin). */
static uint8_t           s_cmd_frame[T1S_ETH_HDR_LEN + 8u];
static volatile bool     s_cmd_busy;
static uint8_t           s_cmd;          /* latest commanded bitmask */
static bool              s_cmd_dirty;    /* a (re)send is pending */
static bool              s_cmd_enabled;  /* actuation gate open: refresh + send */
static uint32_t          s_cmd_refresh_ms;

/* Heartbeat TX staging. */
static uint8_t           s_hb_frame[T1S_ETH_HDR_LEN + T1S_HB_LEN];
static volatile bool     s_hb_busy;
static uint32_t          s_hb_seq;
static uint32_t          s_hb_last_ms;

/* The 1 ms time base is the MCC SYSTICK plib (SYS_Initialize runs
 * SYSTICK_TimerInitialize; this module starts it). */

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

/* T1S_IRQ_N falling-edge: the MAC-PHY needs servicing. */
static void irq_cb(uintptr_t context)
{
    (void)context;
    s_need_service = true;
}

/* One service pass: drive the protocol stack once and check its timers. Do NOT
 * loop here on s_need_service — with hardware that never syncs the lib re-
 * requests service every pass, which would spin forever. IRQ_N is active-low;
 * TC6_Service treats a false interruptLevel as "interrupt active". */
static void service_pump(void)
{
    s_need_service = false;
    bool no_int = (T1S_IRQ_N_Get() != 0u);
    (void)TC6_Service(s_tc6, no_int);
    TC6Regs_CheckTimers();
}

/* Fill the constant Ethernet header of a staging buffer (dst, src, ethertype). */
static void fill_eth_header(uint8_t *frame, const uint8_t *dst, uint16_t ethertype)
{
    memcpy(&frame[0], dst, 6u);
    memcpy(&frame[6], s_mac, 6u);
    frame[12] = (uint8_t)(ethertype >> 8);
    frame[13] = (uint8_t)(ethertype & 0xFFu);
}

/* TX completion callbacks free the matching staging buffer. */
static void tx_done(TC6_t *p, const uint8_t *t, uint16_t l, void *a, void *b)
{
    (void)p; (void)t; (void)l; (void)a; (void)b;
    s_tx_busy = false;
}
static void cmd_tx_done(TC6_t *p, const uint8_t *t, uint16_t l, void *a, void *b)
{
    (void)p; (void)t; (void)l; (void)a; (void)b;
    s_cmd_busy = false;
}
static void hb_tx_done(TC6_t *p, const uint8_t *t, uint16_t l, void *a, void *b)
{
    (void)p; (void)t; (void)l; (void)a; (void)b;
    s_hb_busy = false;
}

/* Flush the most recent staged data frame to the coordinator. Main loop only. */
static void flush_data_frame(void)
{
    if (!s_frame_ready || s_tx_busy || !s_stream_enabled) {
        return;
    }
    /* Snapshot the ISR-staged payload (the scan tick may overwrite s_pending). */
    __disable_irq();
    uint16_t len = s_pending_len;
    if (len > (uint16_t)(sizeof(s_tx_frame) - T1S_ETH_HDR_LEN)) {
        len = (uint16_t)(sizeof(s_tx_frame) - T1S_ETH_HDR_LEN);
    }
    memcpy(&s_tx_frame[T1S_ETH_HDR_LEN], (const void *)s_pending, len);
    s_frame_ready = false;
    __enable_irq();

    s_tx_busy = true;
    if (TC6_SendRawEthernetPacket(s_tc6, s_tx_frame, (uint16_t)(T1S_ETH_HDR_LEN + len),
                                  0u, tx_done, NULL)) {
        s_tx_count++;
    } else {
        s_tx_busy = false;
    }
}

/* Flush the latest guitar command (1-byte bitmask) if a (re)send is pending. */
static void flush_command(void)
{
    if (!s_cmd_dirty || s_cmd_busy) {
        return;
    }
    uint8_t mask = (uint8_t)(s_cmd & T1S_CMD_BIT_MASK);
    s_cmd_frame[T1S_ETH_HDR_LEN] = mask;

    s_cmd_busy = true;
    if (TC6_SendRawEthernetPacket(s_tc6, s_cmd_frame, T1S_ETH_HDR_LEN + 1u,
                                  0u, cmd_tx_done, NULL)) {
        s_cmd_dirty = false;
        s_last_cmd  = mask;
        s_cmd_count++;
    } else {
        s_cmd_busy = false;   /* retry next pass */
    }
}

/* Announce presence to the coordinator (ethertype 0x88B6). */
static void send_heartbeat(void)
{
    if (s_hb_busy || !s_plca_op) {
        return;
    }
    bool synced = false;
    TC6_GetState(s_tc6, NULL, NULL, &synced);

    s_hb_frame[14] = T1S_HB_VERSION;
    s_hb_frame[15] = T1S_HB_TYPE_DETECTOR;
    s_hb_frame[16] = (uint8_t)T1S_NODE_ID;
    s_hb_frame[17] = synced ? 0x01u : 0x00u;   /* flags: bit0 = synced */
    s_hb_seq++;
    s_hb_frame[18] = (uint8_t)(s_hb_seq);
    s_hb_frame[19] = (uint8_t)(s_hb_seq >> 8);
    s_hb_frame[20] = (uint8_t)(s_hb_seq >> 16);
    s_hb_frame[21] = (uint8_t)(s_hb_seq >> 24);

    s_hb_busy = true;
    if (!TC6_SendRawEthernetPacket(s_tc6, s_hb_frame, T1S_ETH_HDR_LEN + T1S_HB_LEN,
                                   0u, hb_tx_done, NULL)) {
        s_hb_busy = false;
    }
}

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>  Public API  >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

void T1SDetector_Initialize(void)
{
    SYSTICK_TimerStart();    /* MCC inits the timer; the app enables it */

    log_str("fretboard: boot - t1s detector + actuator\r\n");  /* one-time banner */

    /* Prebuild the constant Ethernet headers (payload filled per send). */
    fill_eth_header(s_tx_frame,  s_coord_mac,  T1S_ETHERTYPE);
    fill_eth_header(s_cmd_frame, s_guitar_mac, T1S_ETHERTYPE);
    fill_eth_header(s_hb_frame,  s_coord_mac,  T1S_ETHERTYPE_HB);

    /* Hardware reset pulse (T1S_RST active-low, idle high). */
    T1S_CS_Set();
    T1S_RST_Clear();
    SYSTICK_DelayMs(10u);
    T1S_RST_Set();
    SYSTICK_DelayMs(10u);

    SERCOM0_SPI_CallbackRegister(spi_done_cb, 0u);  /* Mode 0 set by MCC init */

    s_tc6 = TC6_Init(NULL);
    if (s_tc6 == NULL) {
        log_str("fretboard: TC6_Init failed\r\n");
        return;
    }

    EIC_CallbackRegister(T1S_IRQ_EIC_PIN, irq_cb, 0u);  /* enabled in EIC_Initialize */

    /* Configure the LAN8651 + PLCA as follower id 4. Not promiscuous — the
     * MAC-PHY filters to this node's MAC + broadcast. Non-blocking: the
     * register sequence finishes in the background via T1SDetector_Tasks. */
    if (!TC6Regs_Init(s_tc6, NULL, s_mac, true, T1S_NODE_ID, T1S_NODE_COUNT,
                      0u, 0u, false, false, false)) {
        log_str("fretboard: TC6Regs_Init rejected\r\n");
    }
}

static void on_plca_status(TC6_t *pInst, bool success, uint32_t addr, uint32_t value,
                           void *pTag, void *pGlobalTag);

void T1SDetector_Tasks(void)
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
                       "fretboard: LAN8651 up - chipRev=%u, MAC=02:00:00:00:00:%02X, "
                       "PLCA follower id=%u/%u\r\n",
                       (unsigned)TC6Regs_GetChipRevision(s_tc6),
                       (unsigned)T1S_NODE_ID, (unsigned)T1S_NODE_ID,
                       (unsigned)T1S_NODE_COUNT);
        log_str(buf);
    }

    /* Refresh cached PLCA operating status in the background so IsConnected / the
     * CLI report real on-bus state, not just init-done. */
    if (s_initialized) {
        uint32_t now = SYSTICK_GetTickCounter();
        if ((now - s_plca_poll_ms) >= T1S_PLCA_POLL_MS) {
            s_plca_poll_ms = now;
            (void)TC6_ReadRegister(s_tc6, T1S_PLCA_STATUS_REG, true, on_plca_status, NULL);
        }
    }

    /* TX only once PLCA is operating — a follower has no transmit opportunity
     * without the coordinator's beacon, so sending earlier queues a frame that
     * never drains (and stalls the *_busy guards). */
    if (s_plca_op) {
        flush_data_frame();

        /* Periodic command refresh so a dropped command frame self-heals — only
         * while the gate is open. A pending final release (queued on disarm) still
         * flushes below even after the gate closes. */
        uint32_t now = SYSTICK_GetTickCounter();
        if (s_cmd_enabled && (now - s_cmd_refresh_ms) >= T1S_CMD_REFRESH_MS) {
            s_cmd_refresh_ms = now;
            s_cmd_dirty = true;
        }
        flush_command();

        if ((now - s_hb_last_ms) >= T1S_HB_INTERVAL_MS) {
            s_hb_last_ms = now;
            send_heartbeat();
        }
    }
}

bool T1SDetector_IsConnected(void)
{
    return s_plca_op;
}

bool T1SDetector_SendFrame(const uint8_t *payload, uint16_t len)
{
    if (!s_plca_op) {
        return false;
    }
    if (len > sizeof(s_pending)) {
        len = sizeof(s_pending);
    }
    /* Single ISR writer; the main loop snapshots under __disable_irq. Latest-
     * wins: overwrite any frame not yet flushed (shows as a sample_seq gap). */
    memcpy((void *)s_pending, payload, len);
    s_pending_len = len;
    s_frame_ready = true;
    return true;
}

void T1SDetector_SetCommand(uint8_t mask, bool active)
{
    mask = (uint8_t)(mask & T1S_CMD_BIT_MASK);

    if (!active) {
        /* Gate closed. On the arm->disarm edge, queue one final all-released
         * frame so the guitar clears any held note, then go silent (no refresh,
         * no further sends) so we don't contend with another command source. */
        if (s_cmd_enabled) {
            s_cmd_enabled = false;
            s_cmd = 0u;
            s_cmd_dirty = true;
        }
        return;
    }

    if (!s_cmd_enabled) {
        s_cmd_enabled = true;   /* gate opened: resume sending */
        s_cmd_dirty = true;     /* push the current mask promptly */
    }
    if (mask != s_cmd) {
        s_cmd = mask;
        s_cmd_dirty = true;     /* send the edge promptly */
    }
}

uint8_t  T1SDetector_ChipRev(void)  { return (s_tc6 != NULL) ? TC6Regs_GetChipRevision(s_tc6) : 0u; }
uint32_t T1SDetector_TxCount(void)  { return s_tx_count; }
uint32_t T1SDetector_CmdCount(void) { return s_cmd_count; }
uint8_t  T1SDetector_LastCmd(void)  { return s_last_cmd; }
uint32_t T1SDetector_ErrCount(void) { return s_err_count; }

void T1SDetector_SetArmed(bool armed) { s_armed = armed; }
bool T1SDetector_Armed(void)          { return s_armed; }

void T1SDetector_LastCtrl(uint8_t *op, uint8_t *arg, uint32_t *count)
{
    if (op    != NULL) { *op    = s_last_ctrl_op; }
    if (arg   != NULL) { *arg   = s_last_ctrl_arg; }
    if (count != NULL) { *count = s_ctrl_count; }
}

void T1SDetector_SetStream(bool enabled) { s_stream_enabled = enabled; }
bool T1SDetector_StreamEnabled(void)     { return s_stream_enabled; }

void T1SDetector_GetState(bool *synced, uint8_t *txCredit, uint8_t *rxCredit)
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

uint8_t T1SDetector_NodeId(void)    { return (uint8_t)T1S_NODE_ID; }
uint8_t T1SDetector_NodeCount(void) { return (uint8_t)T1S_NODE_COUNT; }

/* Diagnostic: log the raw value of a control register (async — the result
 * prints from the service loop a moment later). */
static void on_id_read(TC6_t *pInst, bool success, uint32_t addr, uint32_t value,
                       void *pTag, void *pGlobalTag)
{
    (void)pInst; (void)pTag; (void)pGlobalTag;
    char buf[88];
    if (addr == 0x00000001u) {
        /* PHY id register — oui/model are meaningful here (lib wants 0x1F0/0x1B). */
        (void)snprintf(buf, sizeof(buf),
                       "fretboard: reg 0x%08lX = 0x%08lX (ok=%d, oui=0x%03lX model=0x%02lX)\r\n",
                       (unsigned long)addr, (unsigned long)value, (int)success,
                       (unsigned long)(value >> 10), (unsigned long)((value >> 4) & 0x3FFu));
    } else {
        (void)snprintf(buf, sizeof(buf), "fretboard: reg 0x%08lX = 0x%08lX (ok=%d)\r\n",
                       (unsigned long)addr, (unsigned long)value, (int)success);
    }
    log_str(buf);
}

void T1SDetector_ReadId(void)
{
    /* 0x00 = OA IDVER, 0x01 = PHY id (lib expects oui 0x1F0 / model 0x1B),
     * 0x000A0094 = chip rev. */
    static const uint32_t addrs[3] = { 0x00000000u, 0x00000001u, 0x000A0094u };

    if (s_tc6 == NULL) {
        log_str("fretboard: t1s not initialized\r\n");
        return;
    }
    /* Enqueue only; the main service loop completes the reads and on_id_read
     * logs each result. Don't hammer TC6_Service here — doing so while the link
     * is live can trip a transient Loss_of_Framing. */
    for (uint8_t i = 0u; i < 3u; i++) {
        (void)TC6_ReadRegister(s_tc6, addrs[i], false, on_id_read, NULL);
    }
}

/* Background PLCA_STATUS poll result: cache the operating bit (bit 15) for
 * IsConnected / the CLI. Scheduled from T1SDetector_Tasks every T1S_PLCA_POLL_MS. */
static void on_plca_status(TC6_t *pInst, bool success, uint32_t addr, uint32_t value,
                           void *pTag, void *pGlobalTag)
{
    (void)pInst; (void)addr; (void)pTag; (void)pGlobalTag;
    s_plca_op = success && ((value & (1uL << 15)) != 0u);
}

/* Async read of the PLCA status register (bit 15 = plca_status). */
static void on_plca_read(TC6_t *pInst, bool success, uint32_t addr, uint32_t value,
                         void *pTag, void *pGlobalTag)
{
    (void)pInst; (void)addr; (void)pTag; (void)pGlobalTag;
    char buf[72];
    (void)snprintf(buf, sizeof(buf),
                   "fretboard: PLCA status = 0x%08lX (plca_status=%d)\r\n",
                   (unsigned long)value,
                   (int)(success && ((value & (1uL << 15)) != 0u)));
    log_str(buf);
}

void T1SDetector_ReadPlca(void)
{
    if (s_tc6 == NULL) {
        log_str("fretboard: t1s not initialized\r\n");
        return;
    }
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
    /* This node consumes no RX; absorb the slice to satisfy the driver. */
    if (((uint32_t)offset + len) <= sizeof(s_rx_buf)) {
        memcpy(&s_rx_buf[offset], pRx, len);
    }
}

void TC6_CB_OnRxEthernetPacket(TC6_t *pInst, bool success, uint16_t len,
                               uint64_t *rxTimestamp, void *pGlobalTag)
{
    (void)pInst; (void)rxTimestamp; (void)pGlobalTag;

    if (!success || (len < (T1S_ETH_HDR_LEN + T1S_CTRL_LEN))) {
        return;
    }
    uint16_t ethertype = (uint16_t)((s_rx_buf[12] << 8) | s_rx_buf[13]);
    if (ethertype != T1S_ETHERTYPE_CTRL) {
        return;   /* this node only consumes the control channel */
    }
    /* Typed control [opcode, arg]. Guard with `<` (never ==: a min-frame is
     * zero-padded past the payload). */
    uint8_t op  = s_rx_buf[T1S_ETH_HDR_LEN + T1S_CTRL_OP];
    uint8_t arg = s_rx_buf[T1S_ETH_HDR_LEN + T1S_CTRL_ARG];
    switch (op) {
        case T1S_CTRL_ARM:
            s_armed = (arg != 0u);
            break;
        case T1S_CTRL_STREAM:
            s_stream_enabled = (arg != 0u);
            break;
        default:
            return;   /* unknown opcode: ignore, don't count */
    }
    s_last_ctrl_op  = op;
    s_last_ctrl_arg = arg;
    s_ctrl_count++;
}

void TC6_CB_OnError(TC6_t *pInst, TC6_Error_t err, void *pGlobalTag)
{
    (void)pGlobalTag;
    s_err_count++;
    diag_log("fretboard: t1s error: ", TC6_GetErrorStr(err));
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
    diag_log("fretboard: t1s event: ", TC6Regs_GetEventStr(event));
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
