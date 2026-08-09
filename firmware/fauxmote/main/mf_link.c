#include <string.h>

#include "mf_link.h"
#include "mf_proto.h"
#include "wiimote.h"
#include "guitar.h"
#include "bt_hid_device.h"

#define MF_INPUT_TIMEOUT_MS  200u   /* link watchdog: quiet longer than this -> safe */
#define MF_STATUS_PERIOD_MS  500u   /* STATUS heartbeat */

static mf_send_fn s_send;
static uint8_t    s_last_result;    /* result of the most recent LINK_CMD (0 = ok) */

/* Watchdog / STATUS cadence state (owned by MfLink_Service). */
static uint32_t s_last_input;
static uint32_t s_last_status;
static bool     s_safed = true;     /* start neutralized until marvin talks */
static bool     s_got_input;        /* an input slice arrived since last Service */
static bool     s_force_status;     /* a STATUS_REQ is pending */
static uint8_t  s_prev_status[MF_LEN_STATUS] = { 0xFF, 0xFF, 0xFF, 0xFF };

/* --- apply a received input message by driving the existing control APIs -------- */

static void apply_guitar(uint8_t mask, uint8_t whammy, uint8_t aux)
{
    Wiimote_SetButton("green",     mask & MF_G_GREEN);
    Wiimote_SetButton("red",       mask & MF_G_RED);
    Wiimote_SetButton("yellow",    mask & MF_G_YELLOW);
    Wiimote_SetButton("blue",      mask & MF_G_BLUE);
    Wiimote_SetButton("orange",    mask & MF_G_ORANGE);
    Wiimote_SetButton("strumdown", mask & MF_G_STRUM_DOWN);
    Wiimote_SetButton("strumup",   mask & MF_G_STRUM_UP);
    Guitar_SetWhammy(whammy & MF_WHAMMY_MASK);
    Wiimote_SetButton("gplus",  aux & MF_AUX_PLUS);
    Wiimote_SetButton("gminus", aux & MF_AUX_MINUS);
    Wiimote_SetButton("pedal",  aux & MF_AUX_PEDAL);
    /* MF_AUX_STARPOWER is ignored: star power is a tilt, not a device button. marvin
     * expresses it through the ACCEL slice (an accelerometer, which is what we emulate). */
}

static void apply_wiimote(uint8_t core, uint8_t dpad, uint8_t sx, uint8_t sy)
{
    Wiimote_SetButton("a",     core & MF_W_A);
    Wiimote_SetButton("b",     core & MF_W_B);
    Wiimote_SetButton("one",   core & MF_W_ONE);
    Wiimote_SetButton("two",   core & MF_W_TWO);
    Wiimote_SetButton("plus",  core & MF_W_PLUS);
    Wiimote_SetButton("minus", core & MF_W_MINUS);
    Wiimote_SetButton("home",  core & MF_W_HOME);
    Wiimote_SetButton("up",    dpad & MF_W_UP);
    Wiimote_SetButton("down",  dpad & MF_W_DOWN);
    Wiimote_SetButton("left",  dpad & MF_W_LEFT);
    Wiimote_SetButton("right", dpad & MF_W_RIGHT);
    Guitar_SetStick(sx, sy);
}

static void apply_pointer(uint8_t x, uint8_t y, uint8_t flags)
{
    if (flags & MF_PTR_VISIBLE) {
        Wiimote_SetPointer((float)x / 255.0f, (float)y / 255.0f);
    } else {
        Wiimote_ClearPointer();
    }
}

/* Release everything to safe defaults (link watchdog fired). */
static void neutralize(void)
{
    apply_guitar(0, MF_WHAMMY_REST, 0);
    apply_wiimote(0, 0, MF_STICK_CENTER, MF_STICK_CENTER);
    Wiimote_ClearPointer();
    Wiimote_ClearAccel();
}

static void handle_link_cmd(uint8_t op)
{
    s_last_result = 0;
    switch (op) {
    case MF_CMD_PAIR:       Fauxmote_EnterPairing();     break;
    case MF_CMD_STOP:       Fauxmote_StopPairing();      break;
    case MF_CMD_RECONNECT:  Fauxmote_Reconnect();        break;
    case MF_CMD_DISCONNECT: Fauxmote_Disconnect();       break;
    case MF_CMD_BT_RESET:   Fauxmote_BtReset();          break;
    case MF_CMD_REBOOT:     Fauxmote_Reboot();           break;
    case MF_CMD_UNLINK:     Fauxmote_Unlink();           break;
    case MF_CMD_EXT_ATTACH: Wiimote_SetExtension(true);  break;
    case MF_CMD_EXT_DETACH: Wiimote_SetExtension(false); break;
    case MF_CMD_STATUS_REQ: s_force_status = true;       break;
    default:                s_last_result = 1;           break;   /* unknown opcode */
    }
}

static void build_status(uint8_t out[MF_LEN_STATUS])
{
    uint8_t flags = 0;
    if (Fauxmote_IsDiscoverable()) flags |= MF_ST_DISCOVERABLE | MF_ST_PAIRING;
    if (Wiimote_IsConnected())
    {
        flags |= MF_ST_CONNECTED;
        if (Wiimote_MsSinceRx() > MF_HOST_SILENT_MS) { flags |= MF_ST_HOST_SILENT; }
    }
    if (Wiimote_IsAssigned())      flags |= MF_ST_ASSIGNED;
    if (Wiimote_ExtAttached())     flags |= MF_ST_EXT_ATTACHED;
    if (Fauxmote_WiiAddr())        flags |= MF_ST_BONDED;
    out[0] = flags;
    out[1] = (uint8_t)Wiimote_PlayerSlot();
    out[2] = Wiimote_ReportMode();
    out[3] = s_last_result;
}

/* --- public API ---------------------------------------------------------------- */

void MfLink_Init(mf_send_fn send)
{
    s_send = send;
}

bool MfLink_HandleMessage(uint8_t type, const uint8_t *payload, uint8_t len)
{
    /* Accept len >= the per-type size: the T1S MAC-PHY pads short frames to the
     * 60-byte Ethernet minimum, so a received payload carries trailing pad. */
    switch (type) {
    case MF_MSG_GUITAR:
        if (len < MF_LEN_GUITAR) break;
        apply_guitar(payload[0], payload[1], payload[2]);
        s_got_input = true;
        return true;
    case MF_MSG_WIIMOTE:
        if (len < MF_LEN_WIIMOTE) break;
        apply_wiimote(payload[0], payload[1], payload[2], payload[3]);
        s_got_input = true;
        return true;
    case MF_MSG_POINTER:
        if (len < MF_LEN_POINTER) break;
        apply_pointer(payload[0], payload[1], payload[2]);
        s_got_input = true;
        return true;
    case MF_MSG_ACCEL:
        if (len < MF_LEN_ACCEL) break;
        Wiimote_SetAccel((int8_t)payload[0], (int8_t)payload[1], (int8_t)payload[2]);
        s_got_input = true;
        return true;
    case MF_MSG_LINK_CMD:
        if (len < MF_LEN_LINK_CMD) break;
        handle_link_cmd(payload[0]);
        break;
    default:
        break;   /* unknown type: ignore */
    }
    return false;
}

void MfLink_Service(uint32_t now_ms)
{
    if (s_got_input) {
        s_got_input  = false;
        s_last_input = now_ms;
        s_safed      = false;
    }

    /* Watchdog: if no input slice arrived within the timeout, release all. */
    if (!s_safed && (now_ms - s_last_input) > MF_INPUT_TIMEOUT_MS) {
        neutralize();
        s_safed = true;
    }

    /* STATUS: on change, on request, or as a heartbeat. */
    uint8_t status[MF_LEN_STATUS];
    build_status(status);
    bool changed = memcmp(status, s_prev_status, MF_LEN_STATUS) != 0;
    if (s_force_status || changed ||
        (now_ms - s_last_status) > MF_STATUS_PERIOD_MS) {
        if (s_send != NULL) {
            s_send(MF_MSG_STATUS, status, MF_LEN_STATUS);
        }
        memcpy(s_prev_status, status, MF_LEN_STATUS);
        s_last_status  = now_ms;
        s_force_status = false;
    }
}
