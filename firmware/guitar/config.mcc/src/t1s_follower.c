#include "t1s_follower.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "definitions.h"   /* SERCOM0_SPI_*, SERCOM1_USART_*, EIC_*, PORT macros, CMSIS */

#include "tc6.h"
#include "tc6-regs.h"

/* PLCA follower identity (docs/t1s-podl-link.md §7.1). */
#define T1S_NODE_ID         (2u)
#define T1S_NODE_COUNT      (8u)     /* PLCA cycle length (must match the coordinator) */
#define T1S_INSTANCE        (0u)

#define T1S_ETHERTYPE       (0x88B5u)
#define T1S_ETH_HDR_LEN     (14u)
#define T1S_CMD_BIT_MASK    (0x7Fu)  /* 5 frets + 2 strum */

/* Coordinator-assigned MAC for this node: 02:00:00:00:00:02. */
static uint8_t s_mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, (uint8_t)T1S_NODE_ID };

static TC6_t            *s_tc6;
static volatile bool     s_need_service;
static volatile bool     s_link_up;
static volatile bool     s_spi_busy;

/* Diagnostics (read by the CLI). */
static volatile uint8_t  s_last_cmd;
static volatile uint32_t s_rx_count;
static volatile uint32_t s_err_count;   /* total TC6 errors since boot */
static uint32_t          s_last_diag_ms; /* rate-limit window for diag logs */

/* Command frames are ~60 B after min-frame padding; this only needs the header
 * plus the first payload byte, but size for a padded frame. */
static uint8_t           s_rx_buf[64];

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

/*>>>>>>>>>>>>>>>>>>>>>>>>>>  Wii-guitar actuation  >>>>>>>>>>>>>>>>>>>>>>>>>>>*/

/* Software open-drain: assert = drive low (Clear + OutputEnable), release =
 * tri-state (InputEnable, controller pull-up restores idle). */
#define BTN_APPLY(mask, bit, NAME)                          \
    do {                                                    \
        if ((mask) & (1u << (bit))) {                       \
            NAME##_Clear();                                 \
            NAME##_OutputEnable();                          \
        } else {                                            \
            NAME##_InputEnable();                           \
        }                                                   \
    } while (0)

static void buttons_release_all(void)
{
    FRET_GREEN_InputEnable();
    FRET_RED_InputEnable();
    FRET_YELLOW_InputEnable();
    FRET_BLUE_InputEnable();
    FRET_ORANGE_InputEnable();
    STRUM_DOWN_InputEnable();
    STRUM_UP_InputEnable();
}

static void buttons_apply_mask(uint8_t mask)
{
    BTN_APPLY(mask, 0u, FRET_GREEN);
    BTN_APPLY(mask, 1u, FRET_RED);
    BTN_APPLY(mask, 2u, FRET_YELLOW);
    BTN_APPLY(mask, 3u, FRET_BLUE);
    BTN_APPLY(mask, 4u, FRET_ORANGE);
    BTN_APPLY(mask, 5u, STRUM_DOWN);
    BTN_APPLY(mask, 6u, STRUM_UP);
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

/* T1S_IRQ_N falling-edge (EIC EXTINT13): the MAC-PHY needs servicing. */
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

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>  Public API  >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

void T1SFollower_Initialize(void)
{
    SYSTICK_TimerStart();    /* MCC inits the timer; the app enables it */

    log_str("guitar: boot - t1s follower + cli\r\n");  /* one-time banner */

    buttons_release_all();   /* pins boot Out/Low (asserted) — release first */

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

    EIC_CallbackRegister(EIC_PIN_13, irq_cb, 0u);   /* EXTINT13 enabled in EIC_Initialize */

    /* Configure the LAN8651 + PLCA as follower id 2. Not promiscuous — the
     * MAC-PHY filters to this node's MAC + broadcast. Non-blocking: the
     * register sequence finishes in the background via T1SFollower_Tasks, so
     * the CLI is never gated behind the link coming up. */
    if (!TC6Regs_Init(s_tc6, NULL, s_mac, true, T1S_NODE_ID, T1S_NODE_COUNT,
                      0u, 0u, false, false, false)) {
        log_str("guitar: TC6Regs_Init rejected\r\n");
    }
}

void T1SFollower_Tasks(void)
{
    if (s_tc6 == NULL) {
        return;
    }
    service_pump();
    if (!s_link_up && TC6Regs_GetInitDone(s_tc6)) {
        s_link_up = true;
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
}

bool T1SFollower_IsConnected(void)
{
    return s_link_up;
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

/* Diagnostic: log the raw value of a control register (async — the result
 * prints from the service loop a moment later). */
static void on_id_read(TC6_t *pInst, bool success, uint32_t addr, uint32_t value,
                       void *pTag, void *pGlobalTag)
{
    (void)pInst;
    (void)pTag;
    (void)pGlobalTag;
    char buf[88];
    (void)snprintf(buf, sizeof(buf),
                   "guitar: reg 0x%08lX = 0x%08lX (ok=%d, oui=0x%03lX model=0x%02lX)\r\n",
                   (unsigned long)addr, (unsigned long)value, (int)success,
                   (unsigned long)(value >> 10), (unsigned long)((value >> 4) & 0x3FFu));
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
    for (uint8_t i = 0u; i < 3u; i++) {
        uint32_t tries = 0u;
        /* Enqueue the read, servicing to drain a full control queue. */
        while (!TC6_ReadRegister(s_tc6, addrs[i], false, on_id_read, NULL) &&
               (++tries < 2000u)) {
            service_pump();
        }
        /* Service until the result returns and on_id_read logs it. */
        for (uint32_t t = 0u; t < 5000u; t++) {
            service_pump();
        }
    }
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

    if (!success || (len < (T1S_ETH_HDR_LEN + 1u))) {
        return;
    }
    uint16_t ethertype = (uint16_t)((s_rx_buf[12] << 8) | s_rx_buf[13]);
    if (ethertype != T1S_ETHERTYPE) {
        return;
    }
    /* Command byte is the first payload octet; trailing min-frame padding is
     * ignored. Apply directly (latest-wins). */
    uint8_t mask = (uint8_t)(s_rx_buf[T1S_ETH_HDR_LEN] & T1S_CMD_BIT_MASK);
    s_last_cmd = mask;
    s_rx_count++;
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
        case TC6Regs_Event_Loss_of_Framing_Error:
        case TC6Regs_Event_RX_Non_Recoverable_Error:
        case TC6Regs_Event_TX_Non_Recoverable_Error:
            TC6Regs_Reinit(pInst);
            break;
        default:
            break;
    }
}
