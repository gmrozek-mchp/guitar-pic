#include <inttypes.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_hidd.h"

#include "bt_hid_device.h"

static const char *TAG = "fauxmote.bt";

#define FAUXMOTE_BT_NAME "Nintendo RVL-CNT-01"

static esp_hidd_dev_t *s_hid_dev = NULL;

/* Minimal vendor-defined report map (report IDs 0x30 in / 0x12 out). esp_hidd
 * cannot host the real 217-byte Wiimote descriptor — Bluedroid's per-record SDP
 * pad is hardcoded at 300 bytes (SDP_MAX_PAD_LEN). The real descriptor + full
 * Wiimote SDP record live in docs/wiimote-sdp.md for the custom-SDP path. */
static const uint8_t s_wiimote_report_map[] = {
    0x06, 0x00, 0xFF,        /* Usage Page (Vendor-Defined 0xFF00)        */
    0x09, 0x01,              /* Usage (0x01)                              */
    0xA1, 0x01,              /* Collection (Application)                  */
    0x85, 0x30,              /*   Report ID (0x30)                        */
    0x15, 0x00,              /*   Logical Minimum (0)                     */
    0x26, 0xFF, 0x00,        /*   Logical Maximum (255)                   */
    0x75, 0x08,              /*   Report Size (8)                         */
    0x95, 0x02,              /*   Report Count (2)                        */
    0x09, 0x01,              /*   Usage (0x01)                            */
    0x81, 0x02,              /*   Input (Data,Var,Abs)                    */
    0x85, 0x12,              /*   Report ID (0x12)                        */
    0x95, 0x02,              /*   Report Count (2)                        */
    0x09, 0x01,              /*   Usage (0x01)                            */
    0x91, 0x02,              /*   Output (Data,Var,Abs)                   */
    0xC0,                    /* End Collection                            */
};

static esp_hid_raw_report_map_t s_report_maps[] = {
    { .data = s_wiimote_report_map, .len = sizeof(s_wiimote_report_map) },
};

static esp_hid_device_config_t s_hid_config = {
    .vendor_id = 0x057E,
    .product_id = 0x0306,
    .version = 0x0100,
    .device_name = FAUXMOTE_BT_NAME,
    .manufacturer_name = "Nintendo",
    .serial_number = "",
    .report_maps = s_report_maps,
    .report_maps_len = 1,
};

static void log_bda(const char *what, const uint8_t *bda)
{
    ESP_LOGI(TAG, "%s %02x:%02x:%02x:%02x:%02x:%02x", what,
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
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
    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(TAG, "mode change %d", param->mode_chg.mode);
        break;
    default:
        ESP_LOGD(TAG, "gap event %d", event);
        break;
    }
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

static void hidd_cb(void *args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *p = (esp_hidd_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDD_START_EVENT:
        /* Set COD + discoverability after esp_hidd init so its own Class of
         * Device doesn't override the Wiimote value. */
        set_wiimote_discoverable();
        ESP_LOGI(TAG, "HIDD started; limited-discoverable as a Wiimote");
        break;
    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(TAG, "HIDD connected");
        break;
    case ESP_HIDD_OUTPUT_EVENT:
        ESP_LOGI(TAG, "OUTPUT report id=0x%02x len=%d",
                 p->output.report_id, p->output.length);
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        ESP_LOGW(TAG, "HIDD disconnected; re-advertising");
        set_wiimote_discoverable();
        break;
    default:
        ESP_LOGD(TAG, "hidd event %" PRId32, id);
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

    /* COD + discoverability are set in the HIDD START handler (after esp_hidd
     * init, which otherwise overrides the Class of Device). */

    /* Legacy PIN pairing (SSP disabled in sdkconfig): we answer each PIN request. */
    esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 0, NULL);

    ESP_ERROR_CHECK(esp_hidd_dev_init(&s_hid_config, ESP_HID_TRANSPORT_BT, hidd_cb, &s_hid_dev));
}
