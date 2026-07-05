#ifndef FAUXMOTE_LINK_H
#define FAUXMOTE_LINK_H

#include <stdint.h>
#include <stdbool.h>

/* marvin -> fauxmote command link: transmits controller input over the FLEXCOM1
 * USART (PA28/PA29, the fretboard UART-transport pins, free while the guitar rides
 * T1S) to the ESP32 Wiimote emulator, and receives its STATUS uplink. Wire protocol
 * in net/fauxmote/mf_proto.h / docs/marvin-fauxmote-link.md.
 *
 * Owns FLEXCOM1 exclusively, so it may only be built when the guitar actuator uses
 * the T1S transport (a UART-transport build drives the guitar node on FLEXCOM1). A
 * TX task sends the latched GUITAR state on change and at a floor rate (self-heals
 * dropped frames + keeps fauxmote's link watchdog fed); WIIMOTE nav and LINK_CMD go
 * out on demand. Send calls are non-blocking and safe from any producer context.
 * Call Fauxmote_Initialize once after SYS_Initialize (FLEXCOM1 up) and before the
 * mask producers start. */

void Fauxmote_Initialize(void);

/* GUITAR slice. SendGuitarMask is the gameplay mirror hook (whammy=rest, aux=0);
 * SendGuitar exposes the full slice for future whammy/star-power producers. */
void Fauxmote_SendGuitarMask(uint8_t mask);
void Fauxmote_SendGuitar(uint8_t mask, uint8_t whammy, uint8_t aux);

/* WIIMOTE nav slice (core buttons, d-pad, 6-bit stick) and LINK_CMD opcode. */
void Fauxmote_SendNav(uint8_t core, uint8_t dpad, uint8_t stick_x, uint8_t stick_y);
void Fauxmote_SendCmd(uint8_t op);

/* POINTER slice: IR pointer at (x,y), each 0..255 mapping to 0..1 of the screen
 * (0,0 = top-left). visible=false hides the pointer (off-screen). */
void Fauxmote_SendPointer(uint8_t x, uint8_t y, bool visible);

/* Latest STATUS from fauxmote. Returns false if none has ever been received.
 * Any out pointer may be NULL. age_ms = ms since the last STATUS arrived. */
bool Fauxmote_GetStatus(uint8_t *flags, uint8_t *player_slot,
                        uint8_t *report_mode, uint8_t *last_result, uint32_t *age_ms);

#endif
