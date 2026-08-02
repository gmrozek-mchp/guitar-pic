#ifndef STATUS_LED_H
#define STATUS_LED_H

/* Liveness indicator on LED0 (PB02): a non-blocking heartbeat driven from the
 * main loop off the SysTick millisecond clock. The pattern also encodes T1S
 * link state at a glance:
 *   - on the bus  -> a "lub-dub" double pulse each second,
 *   - bus down    -> a single short blip each second.
 * Either way the LED is always doing something, so a dark/steady LED means the
 * firmware is stuck.
 *
 * Call StatusLed_Initialize() once after SYS_Initialize, then StatusLed_Tasks()
 * repeatedly from the main loop. */

void StatusLed_Initialize(void);
void StatusLed_Tasks(void);

#endif /* STATUS_LED_H */
