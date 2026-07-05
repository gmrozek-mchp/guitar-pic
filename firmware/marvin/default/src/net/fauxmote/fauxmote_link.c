#include "fauxmote_link.h"
#include "mf_proto.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "definitions.h"
#include "log.h"

#define FX_TX_TASK_STACK_WORDS  512u
#define FX_RX_TASK_STACK_WORDS  512u
#define FX_TASK_PRIORITY        5u

/* Floor re-send of the GUITAR slice: bounds latency of a dropped frame and keeps
 * fauxmote's 200 ms link watchdog fed when gameplay is idle. Well under 200 ms. */
#define FX_REFRESH_MS           50u

#define FX_CMD_QUEUE_DEPTH      4u
#define FX_RX_WAIT_MS           100u

static bool s_ready;

/* Latched controller state (written by producers, read by the TX task). Guarded
 * by a short critical section — a few-byte copy, never a blocking call. */
static uint8_t s_g_mask;
static uint8_t s_g_whammy = MF_WHAMMY_REST;
static uint8_t s_g_aux;

static uint8_t s_nav_core;
static uint8_t s_nav_dpad;
static uint8_t s_nav_sx = MF_STICK_CENTER;
static uint8_t s_nav_sy = MF_STICK_CENTER;
static bool    s_nav_dirty;

static uint8_t s_ptr_x;
static uint8_t s_ptr_y;
static uint8_t s_ptr_flags;   /* MF_PTR_VISIBLE */
static bool    s_ptr_dirty;

/* Latest STATUS from fauxmote. */
static bool       s_status_valid;
static uint8_t    s_status[MF_LEN_STATUS];
static TickType_t s_status_tick;

static QueueHandle_t   s_cmd_queue;
static StaticQueue_t   s_cmd_queue_buf;
static uint8_t         s_cmd_queue_storage[FX_CMD_QUEUE_DEPTH];

static SemaphoreHandle_t s_tx_notify;
static StaticSemaphore_t s_tx_notify_buf;
static SemaphoreHandle_t s_rx_notify;
static StaticSemaphore_t s_rx_notify_buf;

static StackType_t   s_tx_stack[FX_TX_TASK_STACK_WORDS];
static StaticTask_t  s_tx_tcb;
static StackType_t   s_rx_stack[FX_RX_TASK_STACK_WORDS];
static StaticTask_t  s_rx_tcb;

/* ---- TX (single writer: the TX task) ------------------------------------ */

static void send_frame(uint8_t type, const uint8_t *payload, uint8_t len)
{
    uint8_t f[3u + MF_MAX_PAYLOAD + 1u];
    f[0] = MF_SOF;
    f[1] = type;
    f[2] = len;
    if (len != 0u) { memcpy(&f[3], payload, len); }
    f[3u + len] = mf_crc8(&f[1], (size_t)(2u + len));   /* CRC over TYPE,LEN,payload */
    (void)FLEXCOM1_USART_Write(f, (size_t)(4u + len));
}

static void fx_tx_task(void *param)
{
    (void)param;
    LOG_INFO("FX: fauxmote link started (FLEXCOM1, %lu baud)\r\n",
             (unsigned long)MF_UART_BAUD);

    for (;;)
    {
        /* Wake on a producer change (low latency) or the floor refresh timeout. */
        (void)xSemaphoreTake(s_tx_notify, pdMS_TO_TICKS(FX_REFRESH_MS));

        uint8_t g[MF_LEN_GUITAR];
        uint8_t nav[MF_LEN_WIIMOTE];
        uint8_t ptr[MF_LEN_POINTER];
        bool    nav_dirty;
        bool    ptr_dirty;

        taskENTER_CRITICAL();
        g[0] = s_g_mask;
        g[1] = s_g_whammy;
        g[2] = s_g_aux;
        nav[0] = s_nav_core;
        nav[1] = s_nav_dpad;
        nav[2] = s_nav_sx;
        nav[3] = s_nav_sy;
        nav_dirty = s_nav_dirty;
        s_nav_dirty = false;
        ptr[0] = s_ptr_x;
        ptr[1] = s_ptr_y;
        ptr[2] = s_ptr_flags;
        ptr_dirty = s_ptr_dirty;
        s_ptr_dirty = false;
        taskEXIT_CRITICAL();

        send_frame(MF_MSG_GUITAR, g, MF_LEN_GUITAR);   /* every wake: change + floor refresh */
        if (nav_dirty) { send_frame(MF_MSG_WIIMOTE, nav, MF_LEN_WIIMOTE); }
        if (ptr_dirty) { send_frame(MF_MSG_POINTER, ptr, MF_LEN_POINTER); }

        uint8_t op;
        while (xQueueReceive(s_cmd_queue, &op, 0) == pdTRUE)
        {
            send_frame(MF_MSG_LINK_CMD, &op, MF_LEN_LINK_CMD);
        }
    }
}

/* ---- RX (STATUS uplink parser) ------------------------------------------ */

typedef enum { P_SOF, P_TYPE, P_LEN, P_PAYLOAD, P_CRC } parse_state_t;

static void parse_byte(uint8_t b)
{
    static parse_state_t state = P_SOF;
    static uint8_t type, len, idx;
    static uint8_t payload[MF_MAX_PAYLOAD];

    switch (state)
    {
        case P_SOF:
            if (b == MF_SOF) { state = P_TYPE; }
            break;
        case P_TYPE:
            if (b == 0x00u) { state = P_SOF; break; }
            type = b;
            state = P_LEN;
            break;
        case P_LEN:
            if (b > MF_MAX_PAYLOAD) { state = P_SOF; break; }
            len = b;
            idx = 0u;
            state = (b == 0u) ? P_CRC : P_PAYLOAD;
            break;
        case P_PAYLOAD:
            payload[idx++] = b;
            if (idx >= len) { state = P_CRC; }
            break;
        case P_CRC:
        {
            uint8_t hdr[2u + MF_MAX_PAYLOAD];
            hdr[0] = type;
            hdr[1] = len;
            memcpy(&hdr[2], payload, len);
            state = P_SOF;
            if (mf_crc8(hdr, (size_t)(2u + len)) != b) { break; }   /* bad CRC */

            if (type == MF_MSG_STATUS && len == MF_LEN_STATUS)
            {
                bool changed = !s_status_valid ||
                               memcmp(s_status, payload, MF_LEN_STATUS) != 0;
                taskENTER_CRITICAL();
                memcpy(s_status, payload, MF_LEN_STATUS);
                s_status_valid = true;
                s_status_tick  = xTaskGetTickCount();
                taskEXIT_CRITICAL();
                if (changed)
                {
                    LOG_INFO("FX: status flags=0x%02x slot=%u mode=0x%02x res=%u\r\n",
                             payload[0], payload[1], payload[2], payload[3]);
                }
            }
            break;
        }
        default:
            state = P_SOF;
            break;
    }
}

static void rx_event_handler(FLEXCOM_USART_EVENT event, uintptr_t context)
{
    (void)context;
    BaseType_t hpw = pdFALSE;

    switch (event)
    {
        case FLEXCOM_USART_EVENT_READ_THRESHOLD_REACHED:
        case FLEXCOM_USART_EVENT_READ_BUFFER_FULL:
            (void)xSemaphoreGiveFromISR(s_rx_notify, &hpw);
            break;
        case FLEXCOM_USART_EVENT_READ_ERROR:
            (void)FLEXCOM1_USART_ErrorGet();
            (void)xSemaphoreGiveFromISR(s_rx_notify, &hpw);
            break;
        default:
            break;
    }

    portYIELD_FROM_ISR(hpw);
}

static void fx_rx_task(void *param)
{
    (void)param;

    for (;;)
    {
        if (FLEXCOM1_USART_ReadCountGet() == 0u)
        {
            (void)xSemaphoreTake(s_rx_notify, pdMS_TO_TICKS(FX_RX_WAIT_MS));
            continue;
        }

        uint8_t c;
        while (FLEXCOM1_USART_Read(&c, 1u) == 1u)
        {
            parse_byte(c);
        }
    }
}

/* ---- public API --------------------------------------------------------- */

void Fauxmote_SendGuitar(uint8_t mask, uint8_t whammy, uint8_t aux)
{
    if (!s_ready) { return; }
    taskENTER_CRITICAL();
    s_g_mask   = (uint8_t)(mask & 0x7Fu);
    s_g_whammy = (uint8_t)(whammy & MF_WHAMMY_MASK);
    s_g_aux    = aux;
    taskEXIT_CRITICAL();
    (void)xSemaphoreGive(s_tx_notify);
}

void Fauxmote_SendGuitarMask(uint8_t mask)
{
    Fauxmote_SendGuitar(mask, MF_WHAMMY_REST, 0u);
}

void Fauxmote_SendNav(uint8_t core, uint8_t dpad, uint8_t stick_x, uint8_t stick_y)
{
    if (!s_ready) { return; }
    taskENTER_CRITICAL();
    s_nav_core  = core;
    s_nav_dpad  = dpad;
    s_nav_sx    = (uint8_t)(stick_x & 0x3Fu);
    s_nav_sy    = (uint8_t)(stick_y & 0x3Fu);
    s_nav_dirty = true;
    taskEXIT_CRITICAL();
    (void)xSemaphoreGive(s_tx_notify);
}

void Fauxmote_SendCmd(uint8_t op)
{
    if (!s_ready) { return; }
    (void)xQueueSend(s_cmd_queue, &op, 0);
    (void)xSemaphoreGive(s_tx_notify);
}

void Fauxmote_SendPointer(uint8_t x, uint8_t y, bool visible)
{
    if (!s_ready) { return; }
    taskENTER_CRITICAL();
    s_ptr_x     = x;
    s_ptr_y     = y;
    s_ptr_flags = visible ? MF_PTR_VISIBLE : 0u;
    s_ptr_dirty = true;
    taskEXIT_CRITICAL();
    (void)xSemaphoreGive(s_tx_notify);
}

bool Fauxmote_GetStatus(uint8_t *flags, uint8_t *player_slot,
                        uint8_t *report_mode, uint8_t *last_result, uint32_t *age_ms)
{
    uint8_t    snap[MF_LEN_STATUS];
    bool       valid;
    TickType_t tick;

    taskENTER_CRITICAL();
    valid = s_status_valid;
    memcpy(snap, s_status, MF_LEN_STATUS);
    tick = s_status_tick;
    taskEXIT_CRITICAL();

    if (!valid) { return false; }

    if (flags)       { *flags       = snap[0]; }
    if (player_slot) { *player_slot = snap[1]; }
    if (report_mode) { *report_mode = snap[2]; }
    if (last_result) { *last_result = snap[3]; }
    if (age_ms)      { *age_ms = (uint32_t)(xTaskGetTickCount() - tick) * portTICK_PERIOD_MS; }
    return true;
}

void Fauxmote_Initialize(void)
{
    s_cmd_queue = xQueueCreateStatic(FX_CMD_QUEUE_DEPTH, sizeof(uint8_t),
                                     s_cmd_queue_storage, &s_cmd_queue_buf);
    configASSERT(s_cmd_queue != NULL);
    s_tx_notify = xSemaphoreCreateBinaryStatic(&s_tx_notify_buf);
    s_rx_notify = xSemaphoreCreateBinaryStatic(&s_rx_notify_buf);
    configASSERT(s_tx_notify != NULL && s_rx_notify != NULL);

    /* Reconfigure FLEXCOM1 (MCC default 500000, unused while the guitar is on T1S)
     * to the link baud, 8-N-1. */
    FLEXCOM_USART_SERIAL_SETUP setup = {
        .baudRate = MF_UART_BAUD,
        .dataWidth = FLEXCOM_USART_DATA_8_BIT,
        .parity    = FLEXCOM_USART_PARITY_NONE,
        .stopBits  = FLEXCOM_USART_STOP_1_BIT,
    };
    (void)FLEXCOM1_USART_SerialSetup(&setup, FLEXCOM1_USART_FrequencyGet());

    /* Arm continuous RX for the STATUS uplink: wake the parser on each byte. */
    FLEXCOM1_USART_ReadCallbackRegister(rx_event_handler, 0u);
    FLEXCOM1_USART_ReadThresholdSet(1u);
    (void)FLEXCOM1_USART_ReadNotificationEnable(true, true);

    (void)xTaskCreateStatic(fx_tx_task, "FxTx", FX_TX_TASK_STACK_WORDS,
                            NULL, FX_TASK_PRIORITY, s_tx_stack, &s_tx_tcb);
    (void)xTaskCreateStatic(fx_rx_task, "FxRx", FX_RX_TASK_STACK_WORDS,
                            NULL, FX_TASK_PRIORITY, s_rx_stack, &s_rx_tcb);

    s_ready = true;
}
