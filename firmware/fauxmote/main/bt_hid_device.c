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

#include "bt_hid_device.h"
#include "wiimote_sdp.h"
#include "wiimote.h"
#include "bt_role.h"

static const char *TAG = "fauxmote.bt";

#define FAUXMOTE_BT_NAME "Nintendo RVL-CNT-01"
#define PSM_HID_CONTROL  0x0011
#define PSM_HID_INTERRUPT 0x0013

static uint8_t s_wii_bda[6];     /* last bonded Wii address (from auth) */
static bool    s_have_wii;
static bool    s_discoverable;
static bool    s_reconnecting;   /* a device-initiated (manual) reconnect is in progress */

static void log_bda(const char *what, const uint8_t *bda)
{
    ESP_LOGI(TAG, "%s %02x:%02x:%02x:%02x:%02x:%02x", what,
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

/* Advertise as a Wiimote: Class of Device 0x002504 (peripheral/joystick, with the
 * limited-discoverable service bit) + limited-discoverable scan mode. The Wii's
 * SYNC scan uses a limited inquiry (LIAC), so general discovery isn't enough. */
static void set_wiimote_discoverable(void)
{
    esp_bt_cod_t cod = {0};
    cod.minor = 0x01;
    cod.major = ESP_BT_COD_MAJOR_DEV_PERIPHERAL;
    cod.service = 0x01;
    esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_ALL);
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_LIMITED_DISCOVERABLE);
}

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_PIN_REQ_EVT: {
        /* Console-SYNC (bonding) pairing: PIN = the host's BD_ADDR, reversed. */
        esp_bt_pin_code_t pin;
        for (int i = 0; i < 6; i++) {
            pin[i] = param->pin_req.bda[5 - i];
        }
        log_bda("PIN request from", param->pin_req.bda);
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
            ESP_LOGD(TAG, "fd %d RX report 0x%02x (%d B)", l->fd,
                     n > 1 ? l->rx[1] : 0, n);
            Wiimote_HandleRx(l->fd, l->rx, n);
        } else if (n < 0) {
            break;                          /* genuine close of our fd */
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));  /* non-blocking VFS: 0 = no data yet */
        }
    }
    l->in_use = false;
    vTaskDelete(NULL);
}

static void hid_link_start(int fd, uint32_t handle)
{
    for (int i = 0; i < HID_LINK_MAX; i++) {
        if (!s_links[i].in_use) {
            s_links[i].in_use = true;
            s_links[i].stop = false;
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
    case ESP_BT_L2CAP_START_EVT:
        ESP_LOGI(TAG, "L2CAP server up status=%d handle=%u",
                 param->start.status, (unsigned)param->start.handle);
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
        if (s_reconnecting) {
            /* control channel re-opened — now open the interrupt channel. */
            s_reconnecting = false;
            esp_bt_l2cap_connect(ESP_BT_L2CAP_SEC_NONE, PSM_HID_INTERRUPT, s_wii_bda);
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

void Fauxmote_EnterPairing(void)
{
    if (s_discoverable || Wiimote_IsConnected()) {
        ESP_LOGW(TAG, "pairing: already discoverable/connected");
        return;
    }
    start_hid_servers();          /* arm the HID listeners for this sync window */
    set_wiimote_discoverable();
    s_discoverable = true;
    ESP_LOGI(TAG, "pairing mode ON — limited-discoverable as a Wiimote (sync the Wii now)");
}

void Fauxmote_StopPairing(void)
{
    esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    esp_bt_l2cap_stop_all_srv();  /* tear down any armed (unconsumed) HID listeners */
    s_discoverable = false;
    ESP_LOGI(TAG, "pairing mode OFF (idle)");
}

void Fauxmote_Reconnect(void)
{
    if (!s_have_wii) {
        ESP_LOGW(TAG, "reconnect: no bonded Wii yet — run `pair` first");
        return;
    }
    log_bda("reconnect: connecting to", s_wii_bda);
    s_reconnecting = true;
    esp_bt_l2cap_connect(ESP_BT_L2CAP_SEC_NONE, PSM_HID_CONTROL, s_wii_bda);
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

const uint8_t *Fauxmote_WiiAddr(void) { return s_have_wii ? s_wii_bda : NULL; }
