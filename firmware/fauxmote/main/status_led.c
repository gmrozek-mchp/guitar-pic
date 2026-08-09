#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wiimote.h"
#include "bt_hid_device.h"
#include "neopixel.h"
#include "status_led.h"
#ifdef CONFIG_FAUXMOTE_LINK_TRANSPORT_T1S
#include "mf_t1s.h"
#endif

/* One NeoPixel shows two things by taking turns within a 2 s cycle: a red
 * heartbeat first, then a blue report on the Wii link. Only one colour is ever
 * lit, so the two can't blend into an ambiguous purple.
 *
 *   0        240          350                            2000 ms
 *   |__red___|            |____________blue____________|
 *
 * Red is lemmy's heartbeat (firmware/lemmy status_led.c) — 70 ms pulses, a
 * "lub-dub" pair when this node is on the T1S bus and a single blip when it
 * isn't. Blue is the Wii link:
 *   - N blinks   = connected as player N (1..4), and nothing else,
 *   - fast flash = pairing, reconnecting, or connected but not yet assigned,
 *   - one blip   = idle, no Wii link.
 * Something always moves, so a dark pixel means the firmware is stuck. */

#define CYCLE_MS         2000u

#define PULSE_MS           70u   /* red pulse width (lemmy) */
#define DUB_START_MS      170u   /* 2nd red pulse, bus-up only (lemmy) */
#define RED_END_MS        240u   /* red never lights past here */

#define BLUE_START_MS     350u
#define COUNT_ON_MS       120u   /* player-count blink */
#define COUNT_PERIOD_MS   280u
#define FLASH_ON_MS       100u   /* fast flash = pairing / connecting */
#define FLASH_PERIOD_MS   200u
#define BLIP_MS            40u   /* short blip = idle */

#define LED_LEVEL          24u   /* the pixel is bright; this is plenty indoors */

#define LED_TICK_MS        10u
#define LED_STACK        3072

static StaticTask_t s_tcb;
static StackType_t  s_stack[LED_STACK];

/* On the T1S bus, in the same sense as lemmy's T1SFollower_IsConnected(): PLCA
 * synced to the coordinator's beacon, without which we have no transmit slot.
 * The UART transport carries no equivalent state, so those builds just blip. */
static bool bus_up(void)
{
#ifdef CONFIG_FAUXMOTE_LINK_TRANSPORT_T1S
    return MfT1s_IsSynced();
#else
    return false;
#endif
}

static bool red_lit(uint32_t phase)
{
    if (phase < PULSE_MS) {
        return true;
    }
    return bus_up() && phase >= DUB_START_MS && phase < (DUB_START_MS + PULSE_MS);
}

static bool blue_lit(uint32_t phase)
{
    if (phase < BLUE_START_MS) {
        return false;
    }
    uint32_t t = phase - BLUE_START_MS;

    int slot = Wiimote_PlayerSlot();
    if (Wiimote_IsConnected() && slot > 0) {
        uint32_t count_ms = (uint32_t)slot * COUNT_PERIOD_MS;
        return (t < count_ms) && ((t % COUNT_PERIOD_MS) < COUNT_ON_MS);
    }

    if (Fauxmote_IsDiscoverable() || Fauxmote_IsConnecting() || Wiimote_IsConnected()) {
        return (t % FLASH_PERIOD_MS) < FLASH_ON_MS;
    }
    return t < BLIP_MS;
}

static void led_task(void *arg)
{
    (void)arg;
    Neopixel_Init();

    TickType_t next = xTaskGetTickCount();
    uint8_t last_r = 0xFF, last_b = 0xFF;
    for (;;) {
        vTaskDelayUntil(&next, pdMS_TO_TICKS(LED_TICK_MS));

        uint32_t phase = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()) % CYCLE_MS;
        uint8_t r = 0, b = 0;
        if (phase < RED_END_MS) {
            r = red_lit(phase) ? LED_LEVEL : 0;
        } else if (blue_lit(phase)) {
            b = LED_LEVEL;
        }

        if (r != last_r || b != last_b) {   /* only drive the wire on a change */
            Neopixel_Set(r, 0, b);
            last_r = r;
            last_b = b;
        }
    }
}

void StatusLed_Start(void)
{
    xTaskCreateStatic(led_task, "status_led", LED_STACK, NULL, 3, s_stack, &s_tcb);
}
