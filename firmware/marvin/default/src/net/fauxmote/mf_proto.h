#pragma once

#include <stdint.h>
#include <stddef.h>

/* marvin <-> fauxmote command-link wire protocol (message + UART framing layers).
 * Canonical spec: docs/marvin-fauxmote-link.md. This header is transport-neutral
 * (pure C, no ESP/Harmony deps) and shared verbatim between the fauxmote firmware
 * (firmware/fauxmote/main/mf_proto.h) and the marvin firmware
 * (firmware/marvin/default/src/net/fauxmote/mf_proto.h) — keep the two byte-for-byte
 * in sync. */

#define MF_PROTO_VERSION   1u
#define MF_UART_BAUD       1000000u   /* 1 Mbaud, 8-N-1 */

/* --- UART framing: SOF | TYPE | LEN | payload[LEN] | CRC8 (over TYPE,LEN,payload) */
#define MF_SOF             0x7Eu

/* --- Message types (LEN is fixed per type in v1). 0x00 is never valid. */
enum {
    MF_MSG_GUITAR   = 0x01,   /* m->f, 3B: hot gameplay input */
    MF_MSG_WIIMOTE  = 0x02,   /* m->f, 4B: menu-nav input */
    MF_MSG_LINK_CMD = 0x03,   /* m->f, 1B: bluetooth link management */
    MF_MSG_ACCEL    = 0x04,   /* m->f, 3B: accelerometer (planned) */
    MF_MSG_POINTER  = 0x05,   /* m->f, 3B: IR pointer (planned) */
    MF_MSG_STATUS   = 0x81,   /* f->m, 4B: link/connection state */
};

#define MF_LEN_GUITAR    3u
#define MF_LEN_WIIMOTE   4u
#define MF_LEN_LINK_CMD  1u
#define MF_LEN_ACCEL     3u
#define MF_LEN_POINTER   3u
#define MF_LEN_STATUS    4u
#define MF_MAX_PAYLOAD   8u   /* largest LEN across all types */

/* --- GUITAR (3B): [0] fret/strum mask, [1] whammy, [2] aux */
#define MF_G_GREEN       (1u << 0)
#define MF_G_RED         (1u << 1)
#define MF_G_YELLOW      (1u << 2)
#define MF_G_BLUE        (1u << 3)
#define MF_G_ORANGE      (1u << 4)
#define MF_G_STRUM_DOWN  (1u << 5)
#define MF_G_STRUM_UP    (1u << 6)

#define MF_WHAMMY_MASK   0x1Fu
#define MF_WHAMMY_REST   0x10u   /* released/idle whammy value (safe default) */

#define MF_AUX_PLUS       (1u << 0)   /* Start / pause */
#define MF_AUX_MINUS      (1u << 1)   /* Select */
#define MF_AUX_PEDAL      (1u << 2)
#define MF_AUX_STARPOWER  (1u << 3)   /* planned: maps to a Wiimote tilt (no-op for now) */

/* --- WIIMOTE (4B): [0] core buttons, [1] d-pad, [2] stick X, [3] stick Y */
#define MF_W_A           (1u << 0)
#define MF_W_B           (1u << 1)
#define MF_W_ONE         (1u << 2)
#define MF_W_TWO         (1u << 3)
#define MF_W_PLUS        (1u << 4)
#define MF_W_MINUS       (1u << 5)
#define MF_W_HOME        (1u << 6)

#define MF_W_UP          (1u << 0)
#define MF_W_DOWN        (1u << 1)
#define MF_W_LEFT        (1u << 2)
#define MF_W_RIGHT       (1u << 3)

#define MF_STICK_CENTER  0x20u   /* 6-bit stick 0..63, center (safe default) */

/* --- LINK_CMD (1B): [0] opcode */
enum {
    MF_CMD_PAIR       = 0x01,
    MF_CMD_STOP       = 0x02,
    MF_CMD_RECONNECT  = 0x03,
    MF_CMD_UNLINK     = 0x04,
    MF_CMD_EXT_ATTACH = 0x05,
    MF_CMD_EXT_DETACH = 0x06,
    MF_CMD_STATUS_REQ = 0x07,
};

/* --- STATUS (4B): [0] flags, [1] player_slot, [2] report_mode, [3] last_result */
#define MF_ST_DISCOVERABLE (1u << 0)
#define MF_ST_CONNECTED    (1u << 1)
#define MF_ST_ASSIGNED     (1u << 2)
#define MF_ST_EXT_ATTACHED (1u << 3)
#define MF_ST_PAIRING      (1u << 4)
#define MF_ST_BONDED       (1u << 5)

/* CRC-8/CCITT, poly 0x07, init 0x00, no reflection. Over TYPE, LEN, payload. */
static inline uint8_t mf_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}
