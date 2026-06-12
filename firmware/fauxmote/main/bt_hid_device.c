#include <errno.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_l2cap_bt_api.h"

#include "bt_hid_device.h"
#include "wiimote_sdp.h"
#include "wiimote.h"

static const char *TAG = "fauxmote.bt";

#define FAUXMOTE_BT_NAME "Nintendo RVL-CNT-01"
#define PSM_HID_CONTROL  0x0011
#define PSM_HID_INTERRUPT 0x0013

static void log_bda(const char *what, const uint8_t *bda)
{
    ESP_LOGI(TAG, "%s %02x:%02x:%02x:%02x:%02x:%02x", what,
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

/* Advertise as a Wiimote: Class of Device 0x002504 (peripheral/joystick, with the
 * limited-discoverable service bit) + limited-discoverable scan mode. The Wii's
 * SYNC scan uses a limited inquiry (LIAC), so general discovery isn't enough. */
static void set_wiimote_discoverable(void)
{
    esp_bt_cod_t cod = {0};
    cod.minor = 0x01;
    cod.major = ESP_BT_COD_MAJOR_DEV_PERIPHERAL;
    cod.service = 0x01;
    esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_ALL);
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_LIMITED_DISCOVERABLE);
}

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_PIN_REQ_EVT: {
        /* Console-SYNC (bonding) pairing: PIN = the host's BD_ADDR, reversed. */
        esp_bt_pin_code_t pin;
        for (int i = 0; i < 6; i++) {
            pin[i] = param->pin_req.bda[5 - i];
        }
        log_bda("PIN request from", param->pin_req.bda);
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 6, pin);
        break;
    }
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            log_bda("auth OK with", param->auth_cmpl.bda);
        } else {
            ESP_LOGW(TAG, "auth FAILED, status=%d", param->auth_cmpl.stat);
        }
        break;
    default:
        ESP_LOGD(TAG, "gap event %d", event);
        break;
    }
}

#define HID_LINK_MAX  2
#define HID_RX_LEN    64
#define HID_RX_STACK  3072

typedef struct {
    bool         in_use;
    int          fd;
    StaticTask_t tcb;
    StackType_t  stack[HID_RX_STACK];
    uint8_t      rx[HID_RX_LEN];
} hid_link_t;

static hid_link_t s_links[HID_LINK_MAX];

/* Read + log every HIDP frame the Wii sends on an open HID channel, so we can see
 * its handshake (output reports 0x11/0x12/0x15, …) before crafting responses. */
static void hid_reader_task(void *arg)
{
    hid_link_t *l = (hid_link_t *)arg;
    for (;;) {
        int n = read(l->fd, l->rx, sizeof(l->rx));
        if (n > 0) {
            ESP_LOGI(TAG, "fd %d RX report 0x%02x (%d B)", l->fd,
                     n > 1 ? l->rx[1] : 0, n);
            Wiimote_HandleRx(l->fd, l->rx, n);
        } else if (n == 0) {
            /* esp_bt_l2cap read is non-blocking: 0 = no data yet, so poll. */
            vTaskDelay(pdMS_TO_TICKS(10));
        } else {
            ESP_LOGW(TAG, "fd %d closed (errno %d); reader exit", l->fd, errno);
            break;
        }
    }
    Wiimote_NotifyFdClosed(l->fd);
    l->in_use = false;
    vTaskDelete(NULL);
}

static void hid_link_start(int fd)
{
    for (int i = 0; i < HID_LINK_MAX; i++) {
        if (!s_links[i].in_use) {
            s_links[i].in_use = true;
            s_links[i].fd = fd;
            xTaskCreateStatic(hid_reader_task, "hid_rx", HID_RX_STACK,
                              &s_links[i], 5, s_links[i].stack, &s_links[i].tcb);
            return;
        }
    }
    ESP_LOGE(TAG, "no free HID link slot for fd %d", fd);
}

static void l2cap_cb(esp_bt_l2cap_cb_event_t event, esp_bt_l2cap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_L2CAP_INIT_EVT:
        /* The L2CAP data path is fd-based; the VFS must be registered before a
         * server can allocate a slot/fd. */
        ESP_LOGI(TAG, "L2CAP init status=%d; registering VFS", param->init.status);
        esp_bt_l2cap_vfs_register();
        break;
    case ESP_BT_L2CAP_VFS_REGISTER_EVT:
        ESP_LOGI(TAG, "L2CAP VFS registered status=%d; starting HID servers",
                 param->vfs_register.status);
        esp_bt_l2cap_start_srv(ESP_BT_L2CAP_SEC_NONE, PSM_HID_CONTROL);
        esp_bt_l2cap_start_srv(ESP_BT_L2CAP_SEC_NONE, PSM_HID_INTERRUPT);
        break;
    case ESP_BT_L2CAP_START_EVT:
        ESP_LOGI(TAG, "L2CAP server up status=%d handle=%u",
                 param->start.status, (unsigned)param->start.handle);
        break;
    case ESP_BT_L2CAP_OPEN_EVT:
        log_bda("L2CAP OPEN from", param->open.rem_bda);
        ESP_LOGI(TAG, "  status=%d handle=%u fd=%d mtu=%d",
                 param->open.status, (unsigned)param->open.handle,
                 param->open.fd, (int)param->open.tx_mtu);
        hid_link_start(param->open.fd);
        break;
    case ESP_BT_L2CAP_CLOSE_EVT:
        ESP_LOGW(TAG, "L2CAP CLOSE handle=%u async=%d",
                 (unsigned)param->close.handle, param->close.async);
        break;
    default:
        ESP_LOGD(TAG, "l2cap event %d", event);
        break;
    }
}

void Fauxmote_BtStart(void)
{
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    const uint8_t *bda = esp_bt_dev_get_address();
    if (bda) {
        /* Our BD_ADDR — also the 1+2 temporary-pairing PIN (this, reversed). */
        log_bda("BD_ADDR", bda);
    }

    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_cb));
    ESP_ERROR_CHECK(esp_bt_gap_set_device_name(FAUXMOTE_BT_NAME));

    /* Legacy PIN pairing (SSP disabled in sdkconfig): we answer each PIN request. */
    esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 0, NULL);

    /* Serve the exact Wiimote SDP record, then listen for the Wii's HID L2CAP
     * connections on PSM 0x11 (control) / 0x13 (interrupt). */
    if (!WiimoteSdp_Register()) {
        ESP_LOGE(TAG, "Wiimote SDP registration failed");
    }
    Wiimote_Start();

    ESP_ERROR_CHECK(esp_bt_l2cap_register_callback(l2cap_cb));
    ESP_ERROR_CHECK(esp_bt_l2cap_init());

    set_wiimote_discoverable();
    ESP_LOGI(TAG, "fauxmote up: custom Wiimote SDP + L2CAP 0x11/0x13, limited-discoverable");
}
