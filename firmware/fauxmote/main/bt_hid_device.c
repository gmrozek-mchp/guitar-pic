#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_l2cap_bt_api.h"
#include "esp_system.h"

#include "bt_hid_device.h"
#include "wiimote_sdp.h"
#include "wiimote.h"
#include "bt_role.h"

static const char *TAG = "fauxmote.bt";

#define FAUXMOTE_BT_NAME "Nintendo RVL-CNT-01"
#define PSM_HID_CONTROL  0x0011
#define PSM_HID_INTERRUPT 0x0013

/* A device-initiated reconnect opens the control channel, then the interrupt channel from
 * that channel's OPEN event, then adopts the interrupt fd as the data channel. */
typedef enum {
    RC_IDLE = 0,
    RC_WANT_CONTROL,     /* control connect issued */
    RC_WANT_INTERRUPT,   /* control open; interrupt connect issued */
} reconnect_stage_t;

/* An attempt that never completes must not block later ones forever. */
#define RECONNECT_TIMEOUT_MS  8000u

/* How long a blocked write() means the link is dead rather than merely slow. The Wii's
 * supervision timeout is ~1 s and sniff at intv(400 800) adds up to ~500 ms, so anything
 * past this is a zombie channel — and the L2CAP VFS would otherwise block for 40 s. */
#define TX_STALL_MS  2000u

/* Which pairing handshake the armed window expects. The only difference is the PIN:
 * bonding answers with the host's address reversed, temporary with our own. Bonding
 * (red SYNC) registers us persistently; temporary (the Wii's one-time sync screen,
 * 1+2 on a real remote) gives us a player slot without bonding. */
typedef enum {
    PAIR_BOND = 0,
    PAIR_TEMP,
} pair_mode_t;

static uint8_t s_wii_bda[6];     /* last bonded Wii address (from auth) */
static bool    s_have_wii;
static bool    s_discoverable;
static pair_mode_t s_pair_mode;  /* describes the armed window; reset when it ends */
static reconnect_stage_t s_reconnect;
static TickType_t s_reconnect_at;
static volatile bool s_disconnecting;   /* Fauxmote_Disconnect in progress (re-entry guard) */
static bool    s_l2cap_restart;   /* re-init L2CAP when the pending deinit completes */

static void log_bda(const char *what, const uint8_t *bda)
{
    ESP_LOGI(TAG, "%s %02x:%02x:%02x:%02x:%02x:%02x", what,
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

/* Deinit + re-init the L2CAP layer. A torn-down session leaves state behind that a later
 * connect inherits: on hardware the channels reopen and look healthy while the Wii sees
 * nothing on them and never answers. Re-initing clears the slot pool and the next
 * reconnect works — so every link teardown ends with this (see docs/journal.md). */
static void l2cap_layer_restart(void)
{
    s_l2cap_restart = true;          /* UNINIT_EVT re-inits, which re-registers the VFS */
    esp_err_t err = esp_bt_l2cap_deinit();
    if (err != ESP_OK) {
        s_l2cap_restart = false;
        ESP_LOGE(TAG, "l2cap deinit failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "l2cap layer restarting after teardown");
    }
}

/* A stage left hanging (the peer never opened the channel) expires so `reconnect` works
 * again without a restart. */
static reconnect_stage_t reconnect_stage(void)
{
    if (s_reconnect != RC_IDLE &&
        (TickType_t)(xTaskGetTickCount() - s_reconnect_at) > pdMS_TO_TICKS(RECONNECT_TIMEOUT_MS))
    {
        ESP_LOGW(TAG, "reconnect: attempt timed out at stage %d", (int)s_reconnect);
        s_reconnect = RC_IDLE;
    }
    return s_reconnect;
}

/* Advertise as a Wiimote: Class of Device 0x002504 (peripheral/joystick, with the
 * limited-discoverable service bit) + limited-discoverable scan mode. The Wii's
 * SYNC scan uses a limited inquiry (LIAC), so general discovery isn't enough.
 * Whether the one-time sync screen scans the same way is unverified, so `general`
 * exists to try the other mode without a rebuild. */
static void set_wiimote_discoverable(bool general)
{
    esp_bt_cod_t cod = {0};
    cod.minor = 0x01;
    cod.major = ESP_BT_COD_MAJOR_DEV_PERIPHERAL;
    cod.service = 0x01;
    esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_ALL);
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
                             general ? ESP_BT_GENERAL_DISCOVERABLE
                                     : ESP_BT_LIMITED_DISCOVERABLE);
}

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_PIN_REQ_EVT: {
        /* Bonding: PIN = the host's BD_ADDR reversed. Temporary: our own, reversed. */
        const uint8_t *pin_src = param->pin_req.bda;
        if (s_pair_mode == PAIR_TEMP) {
            const uint8_t *own = esp_bt_dev_get_address();
            if (own == NULL) {
                ESP_LOGE(TAG, "PIN request: own BD_ADDR unavailable, rejecting");
                esp_bt_gap_pin_reply(param->pin_req.bda, false, 0, NULL);
                break;
            }
            pin_src = own;
        }
        esp_bt_pin_code_t pin;
        for (int i = 0; i < 6; i++) {
            pin[i] = pin_src[5 - i];
        }
        log_bda("PIN request from", param->pin_req.bda);
        log_bda(s_pair_mode == PAIR_TEMP ? "  temporary pairing, PIN from (reversed)"
                                         : "  bonding, PIN from (reversed)", pin_src);
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 6, pin);
        break;
    }
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            memcpy(s_wii_bda, param->auth_cmpl.bda, sizeof(s_wii_bda));
            s_have_wii = true;
            log_bda("auth OK with", param->auth_cmpl.bda);
        } else {
            ESP_LOGW(TAG, "auth FAILED, status=%d", param->auth_cmpl.stat);
        }
        /* The window is over either way, so a later PIN request (a red-SYNC bonding
         * attempt) must not inherit this one's mode. */
        s_pair_mode = PAIR_BOND;
        break;
    default:
        ESP_LOGD(TAG, "gap event %d", event);
        break;
    }
}

#define HID_LINK_MAX  4
#define HID_RX_LEN    64
#define HID_RX_STACK  3072

typedef struct {
    bool          in_use;
    volatile bool stop;     /* set by the CLOSE handler to make this reader exit */
    volatile bool closing;  /* closed locally: keep draining rx, but stop dispatching it */
    int           fd;
    uint32_t      handle;   /* L2CAP connection handle, matched against CLOSE events */
    StaticTask_t  tcb;
    StackType_t   stack[HID_RX_STACK];
    uint8_t       rx[HID_RX_LEN];
} hid_link_t;

static hid_link_t s_links[HID_LINK_MAX];

/* Read HIDP frames from one HID channel and dispatch them to the Wiimote module.
 * Exits when its CLOSE handler sets `stop` — not by watching read() return codes,
 * since re-armed servers can recycle this fd number out from under us. */
static void hid_reader_task(void *arg)
{
    hid_link_t *l = (hid_link_t *)arg;
    while (!l->stop) {
        int n = read(l->fd, l->rx, sizeof(l->rx));
        if (l->stop) break;
        if (n > 0) {
            if (l->closing) {
                continue;               /* draining a closed channel: discard, don't act */
            }
            ESP_LOGD(TAG, "fd %d RX report 0x%02x (%d B)", l->fd,
                     n > 1 ? l->rx[1] : 0, n);
            Wiimote_HandleRx(l->fd, l->rx, n);
        } else if (n < 0) {
            break;                          /* genuine close of our fd */
        } else {
            /* The sender can be parked inside write() for 40 s when the Wii stops
             * draining the channel (the VFS write timeout), so the stall is detected
             * from here — this task is never the blocked one — and the link is dropped
             * cleanly instead of going quiet for the whole timeout. */
            if (l->fd == Wiimote_DataFd() && Wiimote_TxStallMs() > TX_STALL_MS) {
                ESP_LOGW(TAG, "tx stalled %u ms on fd %d — dropping the link",
                         (unsigned)Wiimote_TxStallMs(), l->fd);
                Fauxmote_Disconnect();   /* keep looping: this task drains its own queue */
            }
            vTaskDelay(pdMS_TO_TICKS(10));  /* non-blocking VFS: 0 = no data yet */
        }
    }
    l->in_use = false;
    /* Last channel of a session gone — restart the layer now, while nothing is using it,
     * so whoever reconnects next (marvin or the CLI) needs no extra step. Skipped during a
     * pairing window, where a deinit would drop the armed listeners. */
    if (!s_discoverable && !s_l2cap_restart && Fauxmote_ChannelsOpen() == 0) {
        l2cap_layer_restart();
    }
    vTaskDelete(NULL);
}

static void hid_link_start(int fd, uint32_t handle)
{
    for (int i = 0; i < HID_LINK_MAX; i++) {
        if (!s_links[i].in_use) {
            s_links[i].in_use = true;
            s_links[i].stop = false;
            s_links[i].closing = false;
            s_links[i].fd = fd;
            s_links[i].handle = handle;
            xTaskCreateStatic(hid_reader_task, "hid_rx", HID_RX_STACK,
                              &s_links[i], 5, s_links[i].stack, &s_links[i].tcb);
            return;
        }
    }
    ESP_LOGE(TAG, "no free HID link slot for fd %d", fd);
}

static void hid_link_stop(uint32_t handle)
{
    for (int i = 0; i < HID_LINK_MAX; i++) {
        if (s_links[i].in_use && s_links[i].handle == handle) {
            s_links[i].stop = true;
            return;
        }
    }
}

/* Bluedroid consumes the listening server slot when a connection opens, so the
 * HID PSMs are armed per pairing window (EnterPairing) and torn down in StopPairing
 * — re-arming on every disconnect leaks slots when reconnect uses client connects. */
static void start_hid_servers(void)
{
    esp_bt_l2cap_start_srv(ESP_BT_L2CAP_SEC_NONE, PSM_HID_CONTROL);
    esp_bt_l2cap_start_srv(ESP_BT_L2CAP_SEC_NONE, PSM_HID_INTERRUPT);
}

static void l2cap_cb(esp_bt_l2cap_cb_event_t event, esp_bt_l2cap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_L2CAP_INIT_EVT:
        /* The L2CAP data path is fd-based; the VFS must be registered before a
         * server can allocate a slot/fd. */
        ESP_LOGI(TAG, "L2CAP init status=%d; registering VFS", param->init.status);
        esp_bt_l2cap_vfs_register();
        break;
    case ESP_BT_L2CAP_VFS_REGISTER_EVT:
        ESP_LOGI(TAG, "L2CAP VFS registered status=%d", param->vfs_register.status);
        break;
    case ESP_BT_L2CAP_UNINIT_EVT:
        ESP_LOGI(TAG, "L2CAP deinit status=%d", param->uninit.status);
        if (s_l2cap_restart) {
            s_l2cap_restart = false;
            esp_err_t err = esp_bt_l2cap_init();   /* INIT_EVT re-registers the VFS */
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "l2cap re-init failed: %s", esp_err_to_name(err));
            }
        }
        break;
    case ESP_BT_L2CAP_START_EVT:
        ESP_LOGI(TAG, "L2CAP server up status=%d handle=%u",
                 param->start.status, (unsigned)param->start.handle);
        break;
    case ESP_BT_L2CAP_CL_INIT_EVT:
        /* A client connect that never reaches the air fails here — this status is the
         * difference between "the stack refused us" and "the Wii didn't answer". */
        ESP_LOGI(TAG, "L2CAP client init status=%d handle=%u",
                 param->cl_init.status, (unsigned)param->cl_init.handle);
        break;
    case ESP_BT_L2CAP_SRV_STOP_EVT:
        ESP_LOGI(TAG, "L2CAP server stopped status=%d psm=0x%02x",
                 param->srv_stop.status, param->srv_stop.psm);
        break;
    case ESP_BT_L2CAP_OPEN_EVT:
        log_bda("L2CAP OPEN from", param->open.rem_bda);
        ESP_LOGI(TAG, "  status=%d handle=%u fd=%d mtu=%d",
                 param->open.status, (unsigned)param->open.handle,
                 param->open.fd, (int)param->open.tx_mtu);
        hid_link_start(param->open.fd, param->open.handle);
        /* Diagnostic: a real Wiimote is always the BT slave. If we're master, a
         * second real Wiimote makes the Wii a scatternet node and our link lags
         * (see docs/journal.md). Use the `role slave` console command to correct. */
        {
            uint8_t acl_role = BtRole_Get(param->open.rem_bda);
            ESP_LOGW(TAG, "ACL role to Wii = %s",
                     acl_role == 1u ? "SLAVE" : acl_role == 0u ? "MASTER" : "UNKNOWN");
        }
        /* Connected — stop being discoverable (stay connectable for reconnects),
         * matching a real Wiimote which doesn't advertise once connected. */
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
        s_discoverable = false;
        if (reconnect_stage() == RC_WANT_CONTROL) {
            /* control channel re-opened — now open the interrupt channel. */
            s_reconnect = RC_WANT_INTERRUPT;
            s_reconnect_at = xTaskGetTickCount();
            esp_err_t err = esp_bt_l2cap_connect(ESP_BT_L2CAP_SEC_NONE, PSM_HID_INTERRUPT,
                                                 s_wii_bda);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "reconnect: interrupt connect rejected: %s", esp_err_to_name(err));
                s_reconnect = RC_IDLE;
            }
        } else if (reconnect_stage() == RC_WANT_INTERRUPT) {
            /* Both channels are up. On a reconnect the Wii may never re-run its init
             * sequence (it does at the menu, not once a game owns the slot), so adopt
             * this channel and start reporting rather than waiting to be spoken to. */
            s_reconnect = RC_IDLE;
            Wiimote_NotifyConnected(param->open.fd);
        }
        break;
    case ESP_BT_L2CAP_CLOSE_EVT:
        ESP_LOGW(TAG, "L2CAP CLOSE handle=%u async=%d",
                 (unsigned)param->close.handle, param->close.async);
        /* Signal this channel's reader to exit (by handle, robust to fd reuse). */
        hid_link_stop(param->close.handle);
        /* Stop streaming + reset controller state immediately so the sender
         * doesn't write into the dying channel and the LED returns to idle.
         * Recovery is via the `reconnect` command — no automatic reconnect. */
        Wiimote_NotifyDisconnected();
        break;
    default:
        ESP_LOGD(TAG, "l2cap event %d", event);
        break;
    }
}

void Fauxmote_BtStart(void)
{
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    const uint8_t *bda = esp_bt_dev_get_address();
    if (bda) {
        /* Our BD_ADDR — also the 1+2 temporary-pairing PIN (this, reversed). */
        log_bda("BD_ADDR", bda);
    }

    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_cb));
    ESP_ERROR_CHECK(esp_bt_gap_set_device_name(FAUXMOTE_BT_NAME));

    /* Legacy PIN pairing (SSP disabled in sdkconfig): we answer each PIN request. */
    esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 0, NULL);

    /* Serve the exact Wiimote SDP record, then listen for the Wii's HID L2CAP
     * connections on PSM 0x11 (control) / 0x13 (interrupt). */
    if (!WiimoteSdp_Register()) {
        ESP_LOGE(TAG, "Wiimote SDP registration failed");
    }
    Wiimote_Start();

    ESP_ERROR_CHECK(esp_bt_l2cap_register_callback(l2cap_cb));
    ESP_ERROR_CHECK(esp_bt_l2cap_init());

    /* The stack persists the bond (link key) in NVS and reloads it at boot — recall
     * the bonded Wii's address so `reconnect` works after a power cycle. */
    int bonded = esp_bt_gap_get_bond_device_num();
    if (bonded > 0) {
        esp_bd_addr_t devs[8];
        int n = bonded > 8 ? 8 : bonded;
        if (esp_bt_gap_get_bond_device_list(&n, devs) == ESP_OK && n > 0) {
            memcpy(s_wii_bda, devs[0], sizeof(s_wii_bda));
            s_have_wii = true;
            log_bda("recalled bonded Wii", s_wii_bda);
        }
    }

    /* Stay idle (not discoverable/connectable) until the operator runs `pair`. */
    esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    ESP_LOGI(TAG, "fauxmote up (idle). Use the `pair` console command to sync with a Wii.");
}

/* Open a pairing window in the given mode. Both entry points share this so the two
 * windows can't drift apart in anything but the PIN and the scan mode. */
static void enter_pairing(pair_mode_t mode, bool scan_general)
{
    if (s_discoverable || Wiimote_IsConnected()) {
        ESP_LOGW(TAG, "pairing: already discoverable/connected");
        return;
    }
    s_pair_mode = mode;
    start_hid_servers();          /* arm the HID listeners for this sync window */
    set_wiimote_discoverable(scan_general);
    s_discoverable = true;
    ESP_LOGI(TAG, "pairing mode ON (%s, %s-discoverable) as a Wiimote — sync the Wii now",
             mode == PAIR_TEMP ? "temporary/guest" : "bonding",
             scan_general ? "general" : "limited");
}

void Fauxmote_EnterPairing(void)
{
    enter_pairing(PAIR_BOND, false);
}

/* The Wii's one-time sync screen (1+2 on a real remote). Gives us a player slot without
 * bonding, so the Wii won't remember us — use it to re-slot a controller, not to
 * register one. Refuses while discoverable or connected, so a swap of a live link is
 * Fauxmote_Disconnect() first. */
void Fauxmote_EnterPairingTemp(bool scan_general)
{
    enter_pairing(PAIR_TEMP, scan_general);
}

void Fauxmote_StopPairing(void)
{
    esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    esp_bt_l2cap_stop_all_srv();  /* tear down any armed (unconsumed) HID listeners */
    s_discoverable = false;
    s_pair_mode = PAIR_BOND;
    ESP_LOGI(TAG, "pairing mode OFF (idle)");
}

void Fauxmote_Reconnect(void)
{
    if (!s_have_wii) {
        ESP_LOGW(TAG, "reconnect: no bonded Wii yet — run `pair` first");
        return;
    }
    if (Wiimote_IsConnected()) {
        ESP_LOGW(TAG, "reconnect: already connected");
        return;
    }
    if (reconnect_stage() != RC_IDLE) {
        ESP_LOGW(TAG, "reconnect: already in progress");
        return;
    }
    log_bda("reconnect: connecting to", s_wii_bda);
    s_reconnect = RC_WANT_CONTROL;
    s_reconnect_at = xTaskGetTickCount();
    esp_err_t err = esp_bt_l2cap_connect(ESP_BT_L2CAP_SEC_NONE, PSM_HID_CONTROL, s_wii_bda);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "reconnect: control connect rejected: %s", esp_err_to_name(err));
        s_reconnect = RC_IDLE;
    }
}

void Fauxmote_Disconnect(void)
{
    if (s_disconnecting) {
        return;                     /* both readers can spot the same stall */
    }
    s_disconnecting = true;

    Wiimote_NotifyDisconnected();   /* stop the sender writing into a dying channel */

    /* Close, but deliberately leave the readers running: a slot whose rx queue still
     * holds data doesn't get freed on close, it arms a 20 s timer instead — and until it
     * is freed the channel's CLOSE never arrives and a blocked write() never returns.
     * Draining is what releases it, so the readers keep reading until read() fails. */
    int closed = 0;
    for (int i = 0; i < HID_LINK_MAX; i++) {
        if (s_links[i].in_use) {
            s_links[i].closing = true;
            close(s_links[i].fd);    /* the VFS close is what disconnects the channel */
            closed++;
        }
    }
    if (s_discoverable) {
        esp_bt_l2cap_stop_all_srv();   /* armed-but-unconsumed HID listeners */
    }
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    s_discoverable = false;
    s_reconnect = RC_IDLE;
    s_disconnecting = false;
    ESP_LOGI(TAG, "disconnect: closed %d HID channel(s); idle, bonded, reconnectable", closed);
}

void Fauxmote_BtReset(void)
{
    Fauxmote_Disconnect();
    /* With channels still closing, the last reader's exit path does the restart. */
    if (!s_l2cap_restart && Fauxmote_ChannelsOpen() == 0) {
        l2cap_layer_restart();
    }
}

void Fauxmote_Reboot(void)
{
    ESP_LOGW(TAG, "reboot: restarting (the bond survives in NVS)");
    vTaskDelay(pdMS_TO_TICKS(200));   /* let the console and a queued STATUS drain */
    esp_restart();
}

void Fauxmote_Unlink(void)
{
    if (!s_have_wii) {
        ESP_LOGW(TAG, "unlink: no bonded Wii");
        return;
    }
    log_bda("unlink: removing bond", s_wii_bda);
    esp_bt_gap_remove_bond_device(s_wii_bda);   /* erases the link key from NVS */
    s_have_wii = false;
    memset(s_wii_bda, 0, sizeof(s_wii_bda));
}

bool Fauxmote_IsDiscoverable(void) { return s_discoverable; }
bool Fauxmote_IsConnecting(void)   { return s_reconnect != RC_IDLE; }
bool Fauxmote_IsPairingTemp(void)  { return s_pair_mode == PAIR_TEMP; }

int Fauxmote_ChannelsOpen(void)
{
    int n = 0;
    for (int i = 0; i < HID_LINK_MAX; i++) {
        if (s_links[i].in_use) { n++; }
    }
    return n;
}

int Fauxmote_ChannelsClosing(void)
{
    int n = 0;
    for (int i = 0; i < HID_LINK_MAX; i++) {
        if (s_links[i].in_use && s_links[i].closing) { n++; }
    }
    return n;
}

const uint8_t *Fauxmote_WiiAddr(void) { return s_have_wii ? s_wii_bda : NULL; }
