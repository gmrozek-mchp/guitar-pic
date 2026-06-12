#include "nvs_flash.h"
#include "esp_log.h"

#include "bt_hid_device.h"

static const char *TAG = "fauxmote";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    Fauxmote_BtStart();
    ESP_LOGI(TAG, "HID device started, identity = Nintendo RVL-CNT-01");
}
