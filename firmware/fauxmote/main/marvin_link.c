#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "marvin_link.h"
#include "mf_link.h"
#include "mf_proto.h"

static const char *TAG = "mflink";

/* UART port + pins for the marvin link. NOT UART0 (that is the USB-CDC console).
 * ESP32 Feather V2 (PICO-MINI-02): the broken-out RX/TX pins (GPIO7/GPIO8) are the
 * board's second hardware UART, independent of the USB debug UART. */
#define MF_UART_PORT     UART_NUM_1
#define MF_UART_TX_PIN   8    /* Feather "TX" pin */
#define MF_UART_RX_PIN   7    /* Feather "RX" pin */

static StaticTask_t s_task_tcb;
static StackType_t  s_task_stack[4096];

/* --- STATUS uplink: build the SOF/TYPE/LEN/payload/CRC8 frame ------------------- */

static void mf_send(uint8_t type, const uint8_t *payload, uint8_t len)
{
    uint8_t frame[3 + MF_MAX_PAYLOAD + 1];
    uint8_t hdr[2 + MF_MAX_PAYLOAD];
    frame[0] = MF_SOF;
    frame[1] = type;
    frame[2] = len;
    hdr[0] = type;
    hdr[1] = len;
    for (uint8_t i = 0; i < len; i++) {
        frame[3 + i] = payload[i];
        hdr[2 + i]   = payload[i];
    }
    frame[3 + len] = mf_crc8(hdr, (size_t)(2 + len));
    uart_write_bytes(MF_UART_PORT, frame, (size_t)(4 + len));
}

/* --- frame parser -------------------------------------------------------------- */

typedef enum { P_SOF, P_TYPE, P_LEN, P_PAYLOAD, P_CRC } parse_state_t;

typedef struct {
    parse_state_t state;
    uint8_t       type;
    uint8_t       len;
    uint8_t       idx;
    uint8_t       payload[MF_MAX_PAYLOAD];
} parser_t;

/* Feed one byte; on a CRC-valid frame hand the message to the transport-neutral
 * message layer. */
static void parse_byte(parser_t *p, uint8_t b)
{
    switch (p->state) {
    case P_SOF:
        if (b == MF_SOF) p->state = P_TYPE;
        break;
    case P_TYPE:
        if (b == 0x00) { p->state = P_SOF; break; }   /* 0x00 never valid */
        p->type  = b;
        p->state = P_LEN;
        break;
    case P_LEN:
        if (b > MF_MAX_PAYLOAD) { p->state = P_SOF; break; }  /* corrupt -> rescan */
        p->len = b;
        p->idx = 0;
        p->state = (b == 0) ? P_CRC : P_PAYLOAD;
        break;
    case P_PAYLOAD:
        p->payload[p->idx++] = b;
        if (p->idx >= p->len) p->state = P_CRC;
        break;
    case P_CRC: {
        uint8_t hdr[2 + MF_MAX_PAYLOAD];
        hdr[0] = p->type;
        hdr[1] = p->len;
        memcpy(&hdr[2], p->payload, p->len);
        p->state = P_SOF;
        if (mf_crc8(hdr, (size_t)(2 + p->len)) != b) break;   /* bad CRC -> drop */
        (void)MfLink_HandleMessage(p->type, p->payload, p->len);
        break;
    }
    }
}

/* --- task ---------------------------------------------------------------------- */

static void marvin_link_task(void *arg)
{
    (void)arg;
    parser_t parser = { .state = P_SOF };
    uint8_t  rx[64];

    for (;;) {
        int n = uart_read_bytes(MF_UART_PORT, rx, sizeof(rx), pdMS_TO_TICKS(20));
        for (int i = 0; i < n; i++) {
            parse_byte(&parser, rx[i]);
        }
        MfLink_Service((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
    }
}

void MarvinLink_Start(void)
{
    const uart_config_t cfg = {
        .baud_rate = MF_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(MF_UART_PORT, 512, 512, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(MF_UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(MF_UART_PORT, MF_UART_TX_PIN, MF_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    MfLink_Init(mf_send);
    xTaskCreateStatic(marvin_link_task, "mflink", sizeof(s_task_stack) / sizeof(StackType_t),
                      NULL, 5, s_task_stack, &s_task_tcb);
    ESP_LOGI(TAG, "marvin link up on UART%d (TX=%d RX=%d, %lu baud)",
             MF_UART_PORT, MF_UART_TX_PIN, MF_UART_RX_PIN, (unsigned long)MF_UART_BAUD);
}
