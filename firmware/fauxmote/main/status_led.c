#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "wiimote.h"
#include "status_led.h"

#define LED_GPIO     GPIO_NUM_13   /* onboard red user LED on the Feather V2 */
#define LED_STACK    2048

static StaticTask_t s_tcb;
static StackType_t  s_stack[LED_STACK];

static void led_task(void *arg)
{
    (void)arg;
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    bool on = false;

    for (;;) {
        if (Wiimote_IsAssigned()) {
            gpio_set_level(LED_GPIO, 1);                 /* solid = assigned + live */
            vTaskDelay(pdMS_TO_TICKS(200));
        } else if (Wiimote_IsConnected()) {
            on = !on;
            gpio_set_level(LED_GPIO, on);                /* fast blink = connected */
            vTaskDelay(pdMS_TO_TICKS(120));
        } else {
            gpio_set_level(LED_GPIO, 1);                 /* slow heartbeat = waiting */
            vTaskDelay(pdMS_TO_TICKS(80));
            gpio_set_level(LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(920));
        }
    }
}

void StatusLed_Start(void)
{
    xTaskCreateStatic(led_task, "status_led", LED_STACK, NULL, 3, s_stack, &s_tcb);
}
