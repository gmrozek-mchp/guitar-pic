#include "status_led.h"

#include <stdint.h>
#include <stdbool.h>

#include "definitions.h"     /* LED0_*, SYSTICK_* */
#include "t1s_detector.h"    /* T1SDetector_IsConnected */

/* LED0 (PB02) is wired active-low on this board: driving the pin low lights it,
 * high turns it off (MCC parks it high at boot). Flip if a rework inverts it. */
#define LED_ON()   LED0_Clear()
#define LED_OFF()  LED0_Set()

#define BEAT_PERIOD_MS  (1000u)   /* one heartbeat per second */
#define PULSE_MS        (70u)     /* width of each pulse */
#define DUB_START_MS    (170u)    /* start of the 2nd pulse (link-up "dub") */

void StatusLed_Initialize(void)
{
    LED0_OutputEnable();
    LED_OFF();
}

void StatusLed_Tasks(void)
{
    uint32_t phase = SYSTICK_GetTickCounter() % BEAT_PERIOD_MS;

    bool on;
    if (T1SDetector_IsConnected())
    {
        /* lub-dub: two short pulses at the top of each second. */
        on = (phase < PULSE_MS)
          || ((phase >= DUB_START_MS) && (phase < (DUB_START_MS + PULSE_MS)));
    }
    else
    {
        /* single short blip: alive, but not on the bus. */
        on = (phase < PULSE_MS);
    }

    if (on) { LED_ON(); } else { LED_OFF(); }
}
