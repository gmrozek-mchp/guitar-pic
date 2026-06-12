#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"

static const char *TAG = "fauxmote";

/* The Wii matches a Wiimote on this SDP device name. Phase 0 only advertises the
 * name and makes the radio discoverable; the HID device + the rest of the SDP
 * record (VID/PID/descriptor/Class-of-Device) and the PIN-request handling land
 * in Phase 1. See firmware/fauxmote/docs/journal.md. */
#define FAUXMOTE_BT_NAME "Nintendo RVL-CNT-01"

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "auth complete, status=%d", param->auth_cmpl.stat);
        break;
    case ESP_BT_GAP_PIN_REQ_EVT:
        ESP_LOGI(TAG, "PIN requested (min_16_digit=%d)", param->pin_req.min_16_digit);
        break;
    default:
        ESP_LOGD(TAG, "gap event %d", event);
        break;
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_cb));
    ESP_ERROR_CHECK(esp_bt_gap_set_device_name(FAUXMOTE_BT_NAME));
    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE));

    /* Our BD_ADDR is also the basis for the Wiimote pairing PIN (this address, reversed). */
    const uint8_t *bda = esp_bt_dev_get_address();
    if (bda) {
        ESP_LOGI(TAG, "BD_ADDR %02x:%02x:%02x:%02x:%02x:%02x",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    }

    ESP_LOGI(TAG, "Phase 0: BR/EDR radio up, discoverable as \"%s\"", FAUXMOTE_BT_NAME);
}
