#include "nvs_flash.h"
#include "esp_log.h"

#include "bt_hid_device.h"
#include "status_led.h"
#include "console_cli.h"
#include "guitar.h"

static const char *TAG = "fauxmote";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    StatusLed_Start();
    Guitar_Init();          /* register the guitar extension with the base Wiimote */
    Fauxmote_BtStart();
    Cli_Start();
    ESP_LOGI(TAG, "ready — type `pair` then sync the Wii (`help` for commands)");
}
