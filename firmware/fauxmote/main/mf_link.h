#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Transport-agnostic marvin <-> fauxmote message layer. A transport backend
 * (UART or T1S) receives whole mf_proto messages and feeds them in through
 * MfLink_HandleMessage, registers a STATUS uplink sender with MfLink_Init, and
 * drives the input watchdog + STATUS cadence by calling MfLink_Service
 * periodically from its task loop. Wire protocol: docs/marvin-fauxmote-link.md. */

/* Sends one f->m message on the transport (STATUS only, in practice). */
typedef void (*mf_send_fn)(uint8_t type, const uint8_t *payload, uint8_t len);

/* Register the transport's uplink sender. Call before the first MfLink_Service. */
void MfLink_Init(mf_send_fn send);

/* Dispatch one received message. Returns true if it was an input slice (guitar,
 * wiimote, pointer, accel), which resets the link watchdog. A STATUS_REQ opcode
 * forces the next MfLink_Service to emit a STATUS. Unknown/malformed messages
 * are ignored. */
bool MfLink_HandleMessage(uint8_t type, const uint8_t *payload, uint8_t len);

/* Run the input watchdog (neutralize after MF_INPUT_TIMEOUT_MS of quiet) and the
 * STATUS cadence (on change, on request, or as a heartbeat). now_ms is a free-
 * running millisecond counter supplied by the transport. */
void MfLink_Service(uint32_t now_ms);
