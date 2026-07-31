#include "t1s_follower.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <xc.h>

#define FCY 100000000UL   /* conservative Fcy estimate for __delay_ms (reset pulse only) */
#include <libpic30.h>

#include "tc6.h"
#include "tc6-regs.h"

#include "../mcc_generated_files/system/pins.h"
#include "../mcc_generated_files/spi_host/spi1.h"
#include "../mcc_generated_files/timer/tmr1.h"
#include "../mcc_generated_files/uart/uart2.h"

/* PLCA follower identity (docs/t1s-podl-link.md §7.1). */
#define T1S_NODE_ID         (5u)
#define T1S_NODE_COUNT      (8u)     /* PLCA cycle length (must match the coordinator) */
#define T1S_INSTANCE        (0u)

#define T1S_ETHERTYPE       (0x88B5u)  /* data / command frames */
#define T1S_ETHERTYPE_HB    (0x88B6u)  /* heartbeat / presence frames */
#define T1S_ETH_HDR_LEN     (14u)

/* Heartbeat (docs/t1s-podl-link.md §7.2): followers periodically announce
 * presence to the coordinator. Payload: ver, node_type, node_id, flags, seq_u32. */
#define T1S_HB_INTERVAL_MS  (500u)
#define T1S_HB_VERSION      (1u)
#define T1S_HB_TYPE_BEATBOX (6u)     /* 6 = beat source (beatbox) */
#define T1S_HB_LEN          (8u)

/* PLCA_STATUS register: bit 15 (plca_status) = PLCA operating (beacon seen).
 * Polled in the background so the CLI can report real on-bus state. */
#define T1S_PLCA_STATUS_REG (0x0004CA03u)
#define T1S_PLCA_POLL_MS    (250u)

/* Coordinator-assigned MAC for this node: 02:00:00:00:00:05. */
static uint8_t s_mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, (uint8_t)T1S_NODE_ID };

/* Coordinator (marvin) MAC: 02:00:00:00:00:00 — heartbeat destination. */
static const uint8_t s_coord_mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u };

static TC6_t            *s_tc6;
static volatile bool     s_need_service;
static volatile bool     s_initialized;  /* MAC-PHY register bring-up completed + data path enabled */
static volatile bool     s_spi_busy;

/* Cached PLCA operating status (PLCA_STATUS bit 15), refreshed by a periodic
 * background register read. Unlike s_initialized (local config done) or the
 * config-sync footer bit, this only asserts when a coordinator beacon is on the
 * wire — the real "on the bus" signal. */
static volatile bool     s_plca_op;
static uint32_t          s_plca_poll_ms;

/* Diagnostics (read by the CLI). */
static volatile uint8_t  s_last_cmd;
static volatile uint32_t s_rx_count;
static volatile uint32_t s_err_count;    /* total TC6 errors since boot */
static uint32_t          s_last_diag_ms; /* rate-limit window for diag logs */

/* Command frames are ~60 B after min-frame padding; this only needs the header
 * plus the first payload byte, but size for a padded frame. */
static uint8_t           s_rx_buf[64];

/* Heartbeat TX staging (buffer must stay valid until the TX callback fires). */
static uint8_t           s_hb_frame[T1S_ETH_HDR_LEN + T1S_HB_LEN];
static volatile bool     s_hb_busy;
static uint32_t          s_hb_seq;
static uint32_t          s_hb_last_ms;

/* Millisecond time base: TMR1 fires every 1 ms (MCC config) and dispatches to
 * tick_cb via the registered timeout callback. */
static volatile uint32_t s_ticks_ms;

static void tick_cb(void)
{
    s_ticks_ms++;
}

/* 32-bit reads are not atomic on this core; the TMR1 ISR can update s_ticks_ms
 * mid-read at the low/high-word carry. Read until two samples agree. */
static uint32_t now_ms(void)
{
    uint32_t a;
    uint32_t b;
    do {
        a = s_ticks_ms;
        b = s_ticks_ms;
    } while (a != b);
    return a;
}

static void log_str(const char *s)
{
    for (const char *p = s; *p != '\0'; p++) {
        UART2_Write((uint8_t)*p);
    }
}

/* Rate-limited diagnostic line (<= ~1/sec) so a disconnected/erroring link
 * can't flood the console — TC6_Service raises an error every pass when no
 * MAC-PHY answers. Errors/events share the window. */
static void diag_log(const char *prefix, const char *msg)
{
    uint32_t now = now_ms();
    if ((now - s_last_diag_ms) < 1000u) {
        return;
    }
    s_last_diag_ms = now;
    log_str(prefix);
    log_str(msg);
    log_str("\r\n");
}

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>  SPI + IRQ  >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

/* T1S_IRQ_N change-notice (falling edge): the MAC-PHY needs servicing. */
static void irq_cb(void)
{
    s_need_service = true;
}

/* One service pass: drive the protocol stack once and check its timers. The
 * caller (main loop) invokes this repeatedly. Do NOT loop here on
 * s_need_service — with hardware that never syncs the lib re-requests service
 * every pass, which would spin forever. IRQ_N is active-low; TC6_Service treats
 * a false interruptLevel as "interrupt active".
 *
 * SPI completion is settled inline in TC6_CB_OnSpiTransaction (SPI1 is a
 * blocking driver), so nothing is owed here. */
static void service_pump(void)
{
    s_need_service = false;
    bool no_int = (T1S_IRQ_N_GetValue() != 0u);
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
    s_hb_frame[15] = T1S_HB_TYPE_BEATBOX;
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

/* Background PLCA_STATUS poll result: cache the operating bit for the CLI. */
static void on_plca_status(TC6_t *pInst, bool success, uint32_t addr, uint32_t value,
                           void *pTag, void *pGlobalTag)
{
    (void)pInst;
    (void)addr;
    (void)pTag;
    (void)pGlobalTag;
    s_plca_op = success && ((value & (1uL << 15)) != 0u);
}

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>  Public API  >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

void T1SFollower_Initialize(void)
{
    TMR1_TimeoutCallbackRegister(tick_cb);   /* TMR1 already started by SYSTEM_Initialize */

    (void)SPI1_Open(0);   /* config[0]: 12.5 MHz, mode 0 — MCC leaves the module OFF */

    /* Hardware reset pulse (T1S_RST active-low, idle high). Uses __delay_ms
     * (cycle loop) rather than the TMR1 tick so the pulse can't stall on the
     * interrupt clock. */
    T1S_CS_SetHigh();
    T1S_RST_SetLow();
    __delay_ms(10);
    T1S_RST_SetHigh();
    __delay_ms(50);

    s_tc6 = TC6_Init(NULL);
    if (s_tc6 == NULL) {
        log_str("beatbox: t1s TC6_Init failed\r\n");
        return;
    }

    T1S_IRQ_N_SetInterruptHandler(irq_cb);   /* change-notice enabled in PINS_Initialize */

    /* Configure the LAN8651 + PLCA as follower id 5. Not promiscuous — the
     * MAC-PHY filters to this node's MAC + broadcast. The register sequence
     * drives SPI synchronously (TC6Regs_Init pumps TC6_Service internally), so
     * the link is up by the time this returns. */
    if (!TC6Regs_Init(s_tc6, NULL, s_mac, true, T1S_NODE_ID, T1S_NODE_COUNT,
                      0u, 0u, false, false, false)) {
        log_str("beatbox: t1s TC6Regs_Init rejected\r\n");
    }
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
        char buf[88];
        (void)snprintf(buf, sizeof(buf),
                       "beatbox: LAN8651 configured (rev %u), MAC 02:00:00:00:00:%02X, "
                       "PLCA follower %u/%u\r\n",
                       (unsigned)TC6Regs_GetChipRevision(s_tc6),
                       (unsigned)T1S_NODE_ID,
                       (unsigned)T1S_NODE_ID, (unsigned)T1S_NODE_COUNT);
        log_str(buf);
    }

    /* Refresh cached PLCA operating status in the background so the CLI reports
     * real on-bus state, not just init-done. */
    if (s_initialized) {
        uint32_t now = now_ms();
        if ((now - s_plca_poll_ms) >= T1S_PLCA_POLL_MS) {
            s_plca_poll_ms = now;
            (void)TC6_ReadRegister(s_tc6, T1S_PLCA_STATUS_REG, true, on_plca_status, NULL);
        }
    }

    /* Presence heartbeat — only while PLCA is operating: a follower has no
     * transmit opportunity without the coordinator's beacon, so sending earlier
     * would queue a frame that never drains and stall s_hb_busy. */
    if (s_plca_op) {
        uint32_t now = now_ms();
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

bool T1SFollower_IsInitialized(void)
{
    return s_initialized;
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
                       "beatbox: reg 0x%08lX = 0x%08lX (ok=%d, oui=0x%03lX model=0x%02lX)\r\n",
                       (unsigned long)addr, (unsigned long)value, (int)success,
                       (unsigned long)(value >> 10), (unsigned long)((value >> 4) & 0x3FFu));
    } else {
        (void)snprintf(buf, sizeof(buf), "beatbox: reg 0x%08lX = 0x%08lX (ok=%d)\r\n",
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
        log_str("beatbox: t1s not initialized\r\n");
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
                   "beatbox: PLCA status = 0x%08lX (plca_status=%d)\r\n",
                   (unsigned long)value,
                   (int)(success && ((value & (1uL << 15)) != 0u)));
    log_str(buf);
}

void T1SFollower_ReadPlca(void)
{
    if (s_tc6 == NULL) {
        log_str("beatbox: t1s not initialized\r\n");
        return;
    }
    /* Enqueue only; the main service loop completes it (see T1SFollower_ReadId). */
    (void)TC6_ReadRegister(s_tc6, T1S_PLCA_STATUS_REG, true, on_plca_read, NULL);
}

/*>>>>>>>>>>>>>>>>>>>>  TC6 driver callbacks (integrator)  >>>>>>>>>>>>>>>>>>>>*/

bool TC6_CB_OnSpiTransaction(uint8_t tc6instance, uint8_t *pTx, uint8_t *pRx,
                             uint16_t len, void *pGlobalTag)
{
    (void)pGlobalTag;
    if (s_spi_busy) {
        return false;
    }
    s_spi_busy = true;
    T1S_CS_SetLow();             /* CS low — start of transaction */
    for (uint16_t i = 0u; i < len; i++) {
        uint8_t tx = (pTx != NULL) ? pTx[i] : 0x00u;
        uint8_t rx = SPI1_ByteExchange(tx);
        if (pRx != NULL) {
            pRx[i] = rx;
        }
    }
    T1S_CS_SetHigh();            /* CS high — end of transaction */
    s_spi_busy = false;
    /* SPI1 is blocking, so the transfer is complete here. Settle the library's
     * completion bookkeeping inline. TC6_SpiBufferDone only advances the op
     * queue and flags need-service (no re-entry into serviceControl/serviceData
     * and no nested transaction — it guards with intContext), so it is safe to
     * call from within the TC6_Service pass that issued this transfer. This is
     * required: the TC6Regs_Init register sequence spins on TC6_Service
     * internally waiting for these completions, so deferring them to the main
     * loop would deadlock init. */
    TC6_SpiBufferDone(tc6instance, true);
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

    if (!success || (len < (T1S_ETH_HDR_LEN + 1u))) {
        return;
    }
    uint16_t ethertype = (uint16_t)((s_rx_buf[12] << 8) | s_rx_buf[13]);
    if (ethertype != T1S_ETHERTYPE) {
        return;
    }
    /* beatbox drives no bus actuator; count the frame and keep the first payload
     * byte for diagnostics. The lemmy/lightshow publish path lands at B4. */
    s_last_cmd = s_rx_buf[T1S_ETH_HDR_LEN];
    s_rx_count++;
}

void TC6_CB_OnError(TC6_t *pInst, TC6_Error_t err, void *pGlobalTag)
{
    (void)pGlobalTag;
    s_err_count++;
    diag_log("beatbox: t1s error: ", TC6_GetErrorStr(err));
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
    return now_ms();
}

void TC6Regs_CB_OnEvent(TC6_t *pInst, TC6Regs_Event_t event, void *pTag)
{
    (void)pTag;
    diag_log("beatbox: t1s event: ", TC6Regs_GetEventStr(event));
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
