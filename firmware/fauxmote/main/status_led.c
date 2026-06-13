#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "wiimote.h"
#include "bt_hid_device.h"
#include "status_led.h"

#define LED_GPIO     GPIO_NUM_13   /* onboard red user LED on the Feather V2 */
#define LED_STACK    2048

static StaticTask_t s_tcb;
static StackType_t  s_stack[LED_STACK];

static void blink(int on_ms, int off_ms)
{
    gpio_set_level(LED_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(on_ms));
    gpio_set_level(LED_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(off_ms));
}

/* Patterns (re-evaluated each cycle):
 *  - assigned   : N short flashes = player slot 1..4, then a pause, repeat
 *  - pairing/connecting : fast continuous blink
 *  - idle       : a brief blip ~1 Hz */
static void led_task(void *arg)
{
    (void)arg;
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    for (;;) {
        int slot = Wiimote_PlayerSlot();
        if (Wiimote_IsConnected() && slot > 0) {
            for (int i = 0; i < slot; i++) {
                blink(180, 180);
            }
            vTaskDelay(pdMS_TO_TICKS(800));     /* gap before repeating the count */
        } else if (Fauxmote_IsDiscoverable() || Wiimote_IsConnected()) {
            blink(100, 100);                    /* fast blink = pairing/connecting */
        } else {
            blink(30, 2970);                    /* brief blip every ~3 s = idle */
        }
    }
}

void StatusLed_Start(void)
{
    xTaskCreateStatic(led_task, "status_led", LED_STACK, NULL, 3, s_stack, &s_tcb);
}
