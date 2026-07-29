#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "marvin_link.h"
#include "mf_t1s.h"
#include "mf_link.h"
#include "mf_proto.h"

#include "tc6.h"
#include "tc6-regs.h"

static const char *TAG = "mft1s";

/* PLCA follower identity (docs/t1s-podl-link.md §7.1). The coordinator (marvin)
 * is node id 0; controllers (fauxmotes) take ids 1-2, one PLCA node each (at
 * most two). */
#ifndef CONFIG_FAUXMOTE_T1S_NODE_ID
#define CONFIG_FAUXMOTE_T1S_NODE_ID  1
#endif
#define T1S_NODE_ID         ((uint8_t)CONFIG_FAUXMOTE_T1S_NODE_ID)
#define T1S_NODE_COUNT      (8u)     /* PLCA cycle length (must match the coordinator) */
#define T1S_INSTANCE        (0u)

#define T1S_ETHERTYPE_MF    (0x88B7u)  /* fauxmote command / STATUS channel */
#define T1S_ETHERTYPE_HB    (0x88B6u)  /* heartbeat / presence frames */
#define T1S_ETH_HDR_LEN     (14u)

/* Heartbeat (docs/t1s-podl-link.md §7.2): ver, node_type, node_id, flags, seq_u32. */
#define T1S_HB_INTERVAL_MS      (500u)
#define T1S_HB_VERSION          (1u)
#define T1S_HB_TYPE_CONTROLLER  (3u)   /* 1 = detector, 2 = guitar, 3 = controller */
#define T1S_HB_LEN              (8u)

/* LAN8651 wiring — Adafruit ESP32 Feather V2 defaults (overridable via Kconfig). */
#ifndef CONFIG_FAUXMOTE_T1S_PIN_SCLK
#define CONFIG_FAUXMOTE_T1S_PIN_SCLK  5
#endif
#ifndef CONFIG_FAUXMOTE_T1S_PIN_MOSI
#define CONFIG_FAUXMOTE_T1S_PIN_MOSI  19
#endif
#ifndef CONFIG_FAUXMOTE_T1S_PIN_MISO
#define CONFIG_FAUXMOTE_T1S_PIN_MISO  21
#endif
#ifndef CONFIG_FAUXMOTE_T1S_PIN_CS
#define CONFIG_FAUXMOTE_T1S_PIN_CS    33
#endif
#ifndef CONFIG_FAUXMOTE_T1S_PIN_RST
#define CONFIG_FAUXMOTE_T1S_PIN_RST   27
#endif
#ifndef CONFIG_FAUXMOTE_T1S_PIN_IRQ
#define CONFIG_FAUXMOTE_T1S_PIN_IRQ   32
#endif

#define PIN_SCLK  ((gpio_num_t)CONFIG_FAUXMOTE_T1S_PIN_SCLK)
#define PIN_MOSI  ((gpio_num_t)CONFIG_FAUXMOTE_T1S_PIN_MOSI)
#define PIN_MISO  ((gpio_num_t)CONFIG_FAUXMOTE_T1S_PIN_MISO)
#define PIN_CS    ((gpio_num_t)CONFIG_FAUXMOTE_T1S_PIN_CS)
#define PIN_RST   ((gpio_num_t)CONFIG_FAUXMOTE_T1S_PIN_RST)
#define PIN_IRQ   ((gpio_num_t)CONFIG_FAUXMOTE_T1S_PIN_IRQ)

#define T1S_SPI_HOST   SPI2_HOST
#define T1S_SPI_HZ     (15 * 1000 * 1000)
#define T1S_SPI_MAX_XFER  (4096)     /* multi-chunk transactions concatenate chunks */

#define T1S_POLL_MS  (2u)  /* re-service cadence; also catches level-latched IRQ_N */

/* This node's MAC 02:00:00:00:00:<id>; coordinator MAC 02:00:00:00:00:00. */
static uint8_t       s_mac[6]       = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, T1S_NODE_ID };
static const uint8_t s_coord_mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u };

static TC6_t              *s_tc6;
static spi_device_handle_t s_spi;
static volatile bool       s_need_service;
static volatile bool       s_link_up;

/* Diagnostics. */
static volatile uint32_t s_rx_count;
static volatile uint32_t s_tx_count;
static volatile uint32_t s_err_count;
static uint32_t          s_last_diag_ms;

static uint8_t s_rx_buf[64];

/* TX staging (each buffer must stay valid until its TX callback fires). */
static uint8_t       s_hb_frame[T1S_ETH_HDR_LEN + T1S_HB_LEN];
static volatile bool s_hb_busy;
static uint32_t      s_hb_seq;
static uint32_t      s_hb_last_ms;

static uint8_t       s_tx_frame[T1S_ETH_HDR_LEN + MF_MAX_PAYLOAD + 1];
static volatile bool s_tx_busy;

static StaticTask_t  s_task_tcb;
static StackType_t   s_task_stack[4096];
static TaskHandle_t  s_task_handle;

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void diag_log(const char *msg)
{
    uint32_t now = now_ms();
    if ((now - s_last_diag_ms) < 1000u) { return; }
    s_last_diag_ms = now;
    ESP_LOGW(TAG, "%s", msg);
}

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>  SPI + IRQ  >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

/* Deferred-interrupt handling: mask IRQ_N here and hand off to the task, which
 * re-enables it after servicing. Without the mask a level-latched or chattering
 * IRQ_N re-notifies faster than the task can consume it, so the task never
 * blocks and starves the idle task (task watchdog). Not IRAM_ATTR: it calls
 * gpio_intr_disable, and there is no flash-operation concurrency to guard. */
static void irq_isr(void *arg)
{
    (void)arg;
    gpio_intr_disable(PIN_IRQ);
    s_need_service = true;
    if (s_task_handle != NULL) {
        BaseType_t hpw = pdFALSE;
        vTaskNotifyGiveFromISR(s_task_handle, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
}

/* One service pass, then check the library's timers. Never loop here on
 * s_need_service: with a MAC-PHY that never syncs, the library re-requests
 * service every pass (and the synchronous SPI completion re-arms it), so a loop
 * would spin forever. The task paces repeated passes. IRQ_N is active-low;
 * TC6_Service treats a false interruptLevel as "interrupt active". */
static void service_pump(void)
{
    s_need_service = false;
    bool no_int = (gpio_get_level(PIN_IRQ) != 0);
    (void)TC6_Service(s_tc6, no_int);
    TC6Regs_CheckTimers();
}

static void hb_tx_done(TC6_t *pInst, const uint8_t *pTx, uint16_t len,
                       void *pTag, void *pGlobalTag)
{
    (void)pInst; (void)pTx; (void)len; (void)pTag; (void)pGlobalTag;
    s_hb_busy = false;
}

static void send_heartbeat(void)
{
    if (s_hb_busy || !s_link_up) {
        return;
    }
    bool synced = false;
    TC6_GetState(s_tc6, NULL, NULL, &synced);

    memcpy(&s_hb_frame[0], s_coord_mac, 6u);   /* dst = coordinator */
    memcpy(&s_hb_frame[6], s_mac, 6u);         /* src = this node   */
    s_hb_frame[12] = (uint8_t)(T1S_ETHERTYPE_HB >> 8);
    s_hb_frame[13] = (uint8_t)(T1S_ETHERTYPE_HB & 0xFFu);
    s_hb_frame[14] = T1S_HB_VERSION;
    s_hb_frame[15] = T1S_HB_TYPE_CONTROLLER;
    s_hb_frame[16] = T1S_NODE_ID;
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
    } else {
        s_tx_count++;
    }
}

static void tx_done(TC6_t *pInst, const uint8_t *pTx, uint16_t len,
                    void *pTag, void *pGlobalTag)
{
    (void)pInst; (void)pTx; (void)len; (void)pTag; (void)pGlobalTag;
    s_tx_busy = false;
}

/* mf_send_fn: carry one f->m message (STATUS) as a frame to the coordinator on
 * the mf ethertype. Payload is [TYPE][payload...]; the MAC-PHY pads short frames
 * to the 46-byte minimum and appends the FCS. */
static void mf_t1s_send(uint8_t type, const uint8_t *payload, uint8_t len)
{
    if (s_tx_busy || !s_link_up || len > MF_MAX_PAYLOAD) {
        return;
    }
    memcpy(&s_tx_frame[0], s_coord_mac, 6u);
    memcpy(&s_tx_frame[6], s_mac, 6u);
    s_tx_frame[12] = (uint8_t)(T1S_ETHERTYPE_MF >> 8);
    s_tx_frame[13] = (uint8_t)(T1S_ETHERTYPE_MF & 0xFFu);
    s_tx_frame[14] = type;
    memcpy(&s_tx_frame[15], payload, len);

    s_tx_busy = true;
    if (!TC6_SendRawEthernetPacket(s_tc6, s_tx_frame,
                                   (uint16_t)(T1S_ETH_HDR_LEN + 1u + len),
                                   0u, tx_done, NULL)) {
        s_tx_busy = false;
    } else {
        s_tx_count++;
    }
}

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>  Task + init  >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

static void mf_t1s_task(void *arg)
{
    (void)arg;
    /* At least one tick: pdMS_TO_TICKS truncates to 0 below one tick period
     * (e.g. 2 ms at the default 100 Hz tick), which turns the wait into a
     * non-blocking poll — the task would then never yield and starve the idle
     * task. Real RX still wakes the task immediately via the IRQ notification. */
    TickType_t poll_ticks = pdMS_TO_TICKS(T1S_POLL_MS);
    if (poll_ticks == 0u) {
        poll_ticks = 1u;
    }
    for (;;) {
        /* Block until IRQ_N fires (masked in the ISR) or the poll tick elapses.
         * Blocking here is what yields the CPU; the poll tick also re-services a
         * level-latched IRQ_N that produced no fresh edge. */
        (void)ulTaskNotifyTake(pdTRUE, poll_ticks);

        service_pump();
        gpio_intr_enable(PIN_IRQ);   /* re-arm after the deferred service pass */

        if (!s_link_up && TC6Regs_GetInitDone(s_tc6)) {
            s_link_up = true;
            TC6_EnableData(s_tc6, true);
            ESP_LOGI(TAG, "LAN8651 up - chipRev=%u, MAC=02:00:00:00:00:%02X, "
                          "PLCA follower id=%u/%u",
                     (unsigned)TC6Regs_GetChipRevision(s_tc6),
                     (unsigned)T1S_NODE_ID, (unsigned)T1S_NODE_ID,
                     (unsigned)T1S_NODE_COUNT);
        }

        if (s_link_up) {
            uint32_t now = now_ms();
            if ((now - s_hb_last_ms) >= T1S_HB_INTERVAL_MS) {
                s_hb_last_ms = now;
                send_heartbeat();
            }
        }

        MfLink_Service(now_ms());
    }
}

static void spi_gpio_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_CS) | (1ULL << PIN_RST),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&out);
    gpio_set_level(PIN_CS, 1);    /* CS idle high */

    /* Hardware reset pulse (RST active-low, idle high). */
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_IRQ),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&in);

    const spi_bus_config_t bus = {
        .mosi_io_num     = PIN_MOSI,
        .miso_io_num     = PIN_MISO,
        .sclk_io_num     = PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = T1S_SPI_MAX_XFER,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(T1S_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    const spi_device_interface_config_t dev = {
        .clock_speed_hz = T1S_SPI_HZ,
        .mode           = 0,             /* CPOL=0, CPHA=0 */
        .spics_io_num   = -1,            /* CS driven manually in OnSpiTransaction */
        .queue_size     = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(T1S_SPI_HOST, &dev, &s_spi));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_IRQ, irq_isr, NULL));
}

void MarvinLink_Start(void)
{
    spi_gpio_init();

    s_tc6 = TC6_Init(NULL);
    if (s_tc6 == NULL) {
        ESP_LOGE(TAG, "TC6_Init failed");
        return;
    }

    /* Configure the LAN8651 + PLCA as this follower id. Not promiscuous — the
     * MAC-PHY filters to this node's MAC + broadcast. This runs the register
     * sequence synchronously (each SPI transaction completes in-line in
     * OnSpiTransaction); with no MAC-PHY responding it falls through cleanly
     * rather than spinning, and GetInitDone stays false. */
    if (!TC6Regs_Init(s_tc6, NULL, s_mac, true, T1S_NODE_ID, T1S_NODE_COUNT,
                      0u, 0u, false, false, false)) {
        ESP_LOGE(TAG, "TC6Regs_Init rejected");
        return;
    }

    MfLink_Init(mf_t1s_send);
    s_task_handle = xTaskCreateStatic(mf_t1s_task, "mft1s",
                                      sizeof(s_task_stack) / sizeof(StackType_t),
                                      NULL, 5, s_task_stack, &s_task_tcb);
    ESP_LOGI(TAG, "marvin link up on T1S (PLCA follower id=%u, ethertype=0x%04X)",
             (unsigned)T1S_NODE_ID, (unsigned)T1S_ETHERTYPE_MF);
}

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>  Diagnostics  >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

bool     MfT1s_IsUp(void)      { return s_link_up; }
uint8_t  MfT1s_NodeId(void)    { return T1S_NODE_ID; }
uint8_t  MfT1s_ChipRev(void)   { return (s_tc6 != NULL) ? TC6Regs_GetChipRevision(s_tc6) : 0u; }
uint32_t MfT1s_RxCount(void)   { return s_rx_count; }
uint32_t MfT1s_TxCount(void)   { return s_tx_count; }
uint32_t MfT1s_ErrCount(void)  { return s_err_count; }
uint32_t MfT1s_HbSeq(void)     { return s_hb_seq; }

bool MfT1s_IsSynced(void)
{
    bool synced = false;
    if (s_tc6 != NULL) {
        TC6_GetState(s_tc6, NULL, NULL, &synced);
    }
    return synced;
}

/*>>>>>>>>>>>>>>>>>>>>  TC6 driver callbacks (integrator)  >>>>>>>>>>>>>>>>>>>>*/

bool TC6_CB_OnSpiTransaction(uint8_t tc6instance, uint8_t *pTx, uint8_t *pRx,
                             uint16_t len, void *pGlobalTag)
{
    (void)tc6instance;
    (void)pGlobalTag;

    spi_transaction_t t = {
        .length    = (size_t)len * 8u,   /* bits */
        .tx_buffer = pTx,
        .rx_buffer = pRx,
    };
    gpio_set_level(PIN_CS, 0);           /* CS low — start of transaction */
    esp_err_t err = spi_device_polling_transmit(s_spi, &t);
    gpio_set_level(PIN_CS, 1);           /* CS high — end of transaction */

    /* The transfer is blocking, so rx data is valid on return — report completion
     * synchronously. tc6.h permits TC6_SpiBufferDone directly from this callback,
     * and it is required here: TC6Regs_Init busy-loops on TC6_Service waiting for
     * these completions before the service task exists, so a deferred model would
     * never make progress during init. */
    TC6_SpiBufferDone(tc6instance, err == ESP_OK);
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
    if (ethertype != T1S_ETHERTYPE_MF) {
        return;
    }
    /* Payload is [TYPE][payload...]; trailing min-frame padding is ignored by
     * the per-type length check in the message layer. */
    uint8_t  type    = s_rx_buf[T1S_ETH_HDR_LEN];
    uint16_t paylen  = (uint16_t)(len - (T1S_ETH_HDR_LEN + 1u));
    if (paylen > MF_MAX_PAYLOAD) {
        paylen = MF_MAX_PAYLOAD;
    }
    s_rx_count++;
    (void)MfLink_HandleMessage(type, &s_rx_buf[T1S_ETH_HDR_LEN + 1u], (uint8_t)paylen);
}

void TC6_CB_OnError(TC6_t *pInst, TC6_Error_t err, void *pGlobalTag)
{
    (void)pGlobalTag;
    s_err_count++;
    diag_log(TC6_GetErrorStr(err));
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
    diag_log(TC6Regs_GetEventStr(event));
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
