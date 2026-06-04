#ifndef CMD_RECEIVE_H
#define CMD_RECEIVE_H

#include <stdint.h>

/*
 * Remote-control command receiver.
 *
 * Reads single-byte bitmask commands from SERCOM1 UART RX.
 * Each bit directly controls one output:
 *
 *   Bit 0 = Green fret
 *   Bit 1 = Red fret
 *   Bit 2 = Yellow fret
 *   Bit 3 = Blue fret
 *   Bit 4 = Orange fret
 *   Bit 5 = Strum down
 *   Bit 6 = Strum up
 *
 * Bit = 1 → assert (drive low), Bit = 0 → release (tri-state).
 */

#define CMD_BIT_GREEN       (1U << 0)
#define CMD_BIT_RED         (1U << 1)
#define CMD_BIT_YELLOW      (1U << 2)
#define CMD_BIT_BLUE        (1U << 3)
#define CMD_BIT_ORANGE      (1U << 4)
#define CMD_BIT_STRUM_DOWN  (1U << 5)
#define CMD_BIT_STRUM_UP    (1U << 6)

void cmd_receive_init(void);

/* Call each tick.  Drains RX buffer and applies the latest bitmask. */
void cmd_receive_update(void);

/* The bitmask currently driven on the outputs (most recent applied command).
 * Bit layout matches CMD_BIT_*. */
uint8_t cmd_receive_current_mask(void);

#endif
