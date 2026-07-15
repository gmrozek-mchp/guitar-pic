#ifndef GUITAR_CMD_H
#define GUITAR_CMD_H

/* Guitar controller command bitmask — the wire format marvin sends to the
 * actuator node over the fretboard link (§4.3). Shared vocabulary for every
 * producer that builds a mask (timing pipeline, manual control, game
 * controller) and everything that interprets one (the link, the UI). Mirrors
 * the receiver's layout: firmware/fretboard cmd_receive.h and the guitar
 * node's T1S command bits.
 *
 *   bit 0 = green   bit 1 = red    bit 2 = yellow
 *   bit 3 = blue    bit 4 = orange bit 5 = strum-down bit 6 = strum-up */

#define GUITAR_BTN_GREEN       (1u << 0)
#define GUITAR_BTN_RED         (1u << 1)
#define GUITAR_BTN_YELLOW      (1u << 2)
#define GUITAR_BTN_BLUE        (1u << 3)
#define GUITAR_BTN_ORANGE      (1u << 4)
#define GUITAR_BTN_STRUM_DOWN  (1u << 5)
#define GUITAR_BTN_STRUM_UP    (1u << 6)
#define GUITAR_BTN_FRET_MASK   0x1Fu   /* the five fret bits */
#define GUITAR_BTN_VALID_MASK  0x7Fu   /* all valid command bits */

#endif
