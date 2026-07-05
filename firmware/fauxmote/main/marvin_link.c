#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "marvin_link.h"
#include "mf_proto.h"
#include "wiimote.h"
#include "guitar.h"
#include "bt_hid_device.h"

static const char *TAG = "mflink";

/* UART port + pins for the marvin link. NOT UART0 (that is the USB-CDC console).
 * ESP32 Feather V2 (PICO-MINI-02): the broken-out RX/TX pins (GPIO7/GPIO8) are the
 * board's second hardware UART, independent of the USB debug UART. */
#define MF_UART_PORT     UART_NUM_1
#define MF_UART_TX_PIN   8    /* Feather "TX" pin */
#define MF_UART_RX_PIN   7    /* Feather "RX" pin */

#define MF_INPUT_TIMEOUT_MS  200   /* link watchdog: quiet longer than this -> safe */
#define MF_STATUS_PERIOD_MS  500   /* STATUS heartbeat */

static StaticTask_t s_task_tcb;
static StackType_t  s_task_stack[4096];

static uint8_t s_last_result;      /* result of the most recent LINK_CMD (0 = ok) */

/* --- apply a received input message by driving the existing control APIs -------- */

static void apply_guitar(uint8_t mask, uint8_t whammy, uint8_t aux)
{
    Wiimote_SetButton("green",     mask & MF_G_GREEN);
    Wiimote_SetButton("red",       mask & MF_G_RED);
    Wiimote_SetButton("yellow",    mask & MF_G_YELLOW);
    Wiimote_SetButton("blue",      mask & MF_G_BLUE);
    Wiimote_SetButton("orange",    mask & MF_G_ORANGE);
    Wiimote_SetButton("strumdown", mask & MF_G_STRUM_DOWN);
    Wiimote_SetButton("strumup",   mask & MF_G_STRUM_UP);
    Guitar_SetWhammy(whammy & MF_WHAMMY_MASK);
    Wiimote_SetButton("gplus",  aux & MF_AUX_PLUS);
    Wiimote_SetButton("gminus", aux & MF_AUX_MINUS);
    Wiimote_SetButton("pedal",  aux & MF_AUX_PEDAL);
    /* MF_AUX_STARPOWER: no-op until the ACCEL/tilt slice is live. */
}

static void apply_wiimote(uint8_t core, uint8_t dpad, uint8_t sx, uint8_t sy)
{
    Wiimote_SetButton("a",     core & MF_W_A);
    Wiimote_SetButton("b",     core & MF_W_B);
    Wiimote_SetButton("one",   core & MF_W_ONE);
    Wiimote_SetButton("two",   core & MF_W_TWO);
    Wiimote_SetButton("plus",  core & MF_W_PLUS);
    Wiimote_SetButton("minus", core & MF_W_MINUS);
    Wiimote_SetButton("home",  core & MF_W_HOME);
    Wiimote_SetButton("up",    dpad & MF_W_UP);
    Wiimote_SetButton("down",  dpad & MF_W_DOWN);
    Wiimote_SetButton("left",  dpad & MF_W_LEFT);
    Wiimote_SetButton("right", dpad & MF_W_RIGHT);
    Guitar_SetStick(sx, sy);
}

static void apply_pointer(uint8_t x, uint8_t y, uint8_t flags)
{
    if (flags & MF_PTR_VISIBLE) {
        Wiimote_SetPointer((float)x / 255.0f, (float)y / 255.0f);
    } else {
        Wiimote_ClearPointer();
    }
}

/* Release everything to safe defaults (link watchdog fired). */
static void neutralize(void)
{
    apply_guitar(0, MF_WHAMMY_REST, 0);
    apply_wiimote(0, 0, MF_STICK_CENTER, MF_STICK_CENTER);
    Wiimote_ClearPointer();
}

static void handle_link_cmd(uint8_t op)
{
    s_last_result = 0;
    switch (op) {
    case MF_CMD_PAIR:       Fauxmote_EnterPairing();     break;
    case MF_CMD_STOP:       Fauxmote_StopPairing();      break;
    case MF_CMD_RECONNECT:  Fauxmote_Reconnect();        break;
    case MF_CMD_UNLINK:     Fauxmote_Unlink();           break;
    case MF_CMD_EXT_ATTACH: Wiimote_SetExtension(true);  break;
    case MF_CMD_EXT_DETACH: Wiimote_SetExtension(false); break;
    case MF_CMD_STATUS_REQ: /* handled by the caller (forces a STATUS send) */ break;
    default:                s_last_result = 1;           break;   /* unknown opcode */
    }
}

/* --- STATUS uplink ------------------------------------------------------------- */

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

static void build_status(uint8_t out[MF_LEN_STATUS])
{
    uint8_t flags = 0;
    if (Fauxmote_IsDiscoverable()) flags |= MF_ST_DISCOVERABLE | MF_ST_PAIRING;
    if (Wiimote_IsConnected())     flags |= MF_ST_CONNECTED;
    if (Wiimote_IsAssigned())      flags |= MF_ST_ASSIGNED;
    if (Wiimote_ExtAttached())     flags |= MF_ST_EXT_ATTACHED;
    if (Fauxmote_WiiAddr())        flags |= MF_ST_BONDED;
    out[0] = flags;
    out[1] = (uint8_t)Wiimote_PlayerSlot();
    out[2] = Wiimote_ReportMode();
    out[3] = s_last_result;
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

/* Returns true if a valid input message updated the controller state (resets the
 * watchdog); STATUS_REQ sets *status_req. */
static bool parse_byte(parser_t *p, uint8_t b, bool *status_req)
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

        switch (p->type) {
        case MF_MSG_GUITAR:
            if (p->len != MF_LEN_GUITAR) break;
            apply_guitar(p->payload[0], p->payload[1], p->payload[2]);
            return true;
        case MF_MSG_WIIMOTE:
            if (p->len != MF_LEN_WIIMOTE) break;
            apply_wiimote(p->payload[0], p->payload[1], p->payload[2], p->payload[3]);
            return true;
        case MF_MSG_POINTER:
            if (p->len != MF_LEN_POINTER) break;
            apply_pointer(p->payload[0], p->payload[1], p->payload[2]);
            return true;
        case MF_MSG_ACCEL:
            if (p->len != MF_LEN_ACCEL) break;
            /* planned: drive the Wiimote accel field; no-op for now */
            return true;
        case MF_MSG_LINK_CMD:
            if (p->len != MF_LEN_LINK_CMD) break;
            if (p->payload[0] == MF_CMD_STATUS_REQ) *status_req = true;
            else handle_link_cmd(p->payload[0]);
            break;
        default:
            break;   /* unknown type: ignore */
        }
        break;
    }
    }
    return false;
}

/* --- task ---------------------------------------------------------------------- */

static void marvin_link_task(void *arg)
{
    (void)arg;
    parser_t parser = { .state = P_SOF };
    uint8_t  rx[64];

    TickType_t last_input  = xTaskGetTickCount();
    TickType_t last_status = 0;
    bool       safed       = true;   /* start neutralized until marvin talks */
    uint8_t    prev_status[MF_LEN_STATUS] = { 0xFF, 0xFF, 0xFF, 0xFF };

    for (;;) {
        int n = uart_read_bytes(MF_UART_PORT, rx, sizeof(rx), pdMS_TO_TICKS(20));
        bool status_req = false;
        for (int i = 0; i < n; i++) {
            if (parse_byte(&parser, rx[i], &status_req)) {
                last_input = xTaskGetTickCount();
                safed = false;
            }
        }

        TickType_t now = xTaskGetTickCount();

        /* Watchdog: if no input slice arrived within the timeout, release all. */
        if (!safed && (now - last_input) > pdMS_TO_TICKS(MF_INPUT_TIMEOUT_MS)) {
            neutralize();
            safed = true;
        }

        /* STATUS: on change, on request, or as a heartbeat. */
        uint8_t status[MF_LEN_STATUS];
        build_status(status);
        bool changed = memcmp(status, prev_status, MF_LEN_STATUS) != 0;
        if (status_req || changed ||
            (now - last_status) > pdMS_TO_TICKS(MF_STATUS_PERIOD_MS)) {
            mf_send(MF_MSG_STATUS, status, MF_LEN_STATUS);
            memcpy(prev_status, status, MF_LEN_STATUS);
            last_status = now;
        }
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

    xTaskCreateStatic(marvin_link_task, "mflink", sizeof(s_task_stack) / sizeof(StackType_t),
                      NULL, 5, s_task_stack, &s_task_tcb);
    ESP_LOGI(TAG, "marvin link up on UART%d (TX=%d RX=%d, %lu baud)",
             MF_UART_PORT, MF_UART_TX_PIN, MF_UART_RX_PIN, (unsigned long)MF_UART_BAUD);
}
