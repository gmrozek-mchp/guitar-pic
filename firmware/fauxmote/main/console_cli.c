#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "esp_bt_device.h"
#include "linenoise/linenoise.h"

#include "console_cli.h"
#include "bt_hid_device.h"
#include "wiimote.h"
#include "guitar.h"
#include "bt_role.h"
#ifdef CONFIG_FAUXMOTE_LINK_TRANSPORT_T1S
#include "mf_t1s.h"
#endif

static void print_bda(const char *label, const uint8_t *bda)
{
    if (bda) {
        printf("%s %02x:%02x:%02x:%02x:%02x:%02x\n", label,
               bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    } else {
        printf("%s (none)\n", label);
    }
}

static int cmd_pair(int argc, char **argv)
{
    (void)argc; (void)argv;
    Fauxmote_EnterPairing();
    printf("pairing mode ON — press the Wii's red SYNC button now\n");
    return 0;
}

static int cmd_stop(int argc, char **argv)
{
    (void)argc; (void)argv;
    Fauxmote_StopPairing();
    printf("pairing mode OFF\n");
    return 0;
}

static int cmd_reconnect(int argc, char **argv)
{
    (void)argc; (void)argv;
    Fauxmote_Reconnect();
    return 0;
}

static int cmd_disconnect(int argc, char **argv)
{
    (void)argc; (void)argv;
    Fauxmote_Disconnect();
    return 0;
}

static int cmd_btreset(int argc, char **argv)
{
    (void)argc; (void)argv;
    Fauxmote_BtReset();
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("restarting…\n");
    Fauxmote_Reboot();
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    print_bda("bd_addr   ", esp_bt_dev_get_address());
    print_bda("bonded Wii", Fauxmote_WiiAddr());
    printf("discoverable=%d connected=%d assigned=%d report_mode=0x%02x\n",
           Fauxmote_IsDiscoverable(), Wiimote_IsConnected(), Wiimote_IsAssigned(),
           Wiimote_ReportMode());
    uint32_t since_rx = Wiimote_MsSinceRx();
    if (since_rx == UINT32_MAX) {
        printf("last rx    (none)  tx_stall=%lu ms\n", (unsigned long)Wiimote_TxStallMs());
    } else {
        printf("last rx    %lu ms ago  tx_stall=%lu ms\n",
               (unsigned long)since_rx, (unsigned long)Wiimote_TxStallMs());
    }
    /* Channels the stack has opened and not yet closed — a nonzero count with no session
     * is a channel whose close never completed. (Counted here rather than read from
     * esp_bt_l2cap_get_protocol_status(), which deadlocks the caller mid-teardown.) */
    printf("channels   open=%d closes_pending=%d\n",
           Fauxmote_ChannelsOpen(), Fauxmote_ChannelsClosing());
    return 0;
}

static int cmd_role(int argc, char **argv)
{
    const uint8_t *wii = Fauxmote_WiiAddr();
    if (!wii) {
        printf("no bonded Wii (pair first)\n");
        return 1;
    }
    if (argc >= 2) {
        bool to_slave;
        if      (strcmp(argv[1], "slave")  == 0) { to_slave = true;  }
        else if (strcmp(argv[1], "master") == 0) { to_slave = false; }
        else { printf("usage: role [slave|master]\n"); return 1; }
        bool ok = BtRole_Switch(wii, to_slave);
        printf("role switch to %s %s\n", argv[1], ok ? "requested" : "REJECTED");
        return ok ? 0 : 1;
    }
    uint8_t r = BtRole_Get(wii);
    printf("ACL role = %s\n", r == 1u ? "slave" : r == 0u ? "master" : "unknown (no link)");
    return 0;
}

static int cmd_btn(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: btn <name> <0|1>\n");
        printf("  core:   a b one two plus minus home up down left right\n");
        printf("  guitar: green red yellow blue orange strumup strumdown gplus gminus pedal\n");
        return 1;
    }
    bool pressed = atoi(argv[2]) != 0;
    if (!Wiimote_SetButton(argv[1], pressed)) {
        printf("unknown button '%s'\n", argv[1]);
        return 1;
    }
    printf("%s %s\n", argv[1], pressed ? "down" : "up");
    return 0;
}

static int cmd_tap(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: tap <button>\n");
        return 1;
    }
    if (!Wiimote_TapButton(argv[1])) {
        printf("unknown button '%s'\n", argv[1]);
        return 1;
    }
    printf("tapped %s\n", argv[1]);
    return 0;
}

static int cmd_point(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "off") == 0) {
        Wiimote_ClearPointer();
        printf("pointer off\n");
        return 0;
    }
    if (argc < 3) {
        printf("usage: point <x 0..1> <y 0..1> | point off  (0,0 = top-left)\n");
        return 1;
    }
    float x = atof(argv[1]);
    float y = atof(argv[2]);
    Wiimote_SetPointer(x, y);
    printf("pointer x=%d%% y=%d%%\n", (int)(x * 100), (int)(y * 100));
    return 0;
}

static int8_t g_to_wire(float g)
{
    int v = (int)(g * 32.0f + (g >= 0 ? 0.5f : -0.5f));   /* 1/32-g units, rounded */
    if (v < -128) v = -128;
    if (v >  127) v =  127;
    return (int8_t)v;
}

static int cmd_accel(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "level") == 0) {
        Wiimote_ClearAccel();
        printf("accel level (0, 0, +1 g)\n");
        return 0;
    }
    if (argc < 4) {
        printf("usage: accel <gx> <gy> <gz> | accel level   (g, +1g on Z = level)\n");
        return 1;
    }
    int8_t x = g_to_wire(atof(argv[1]));
    int8_t y = g_to_wire(atof(argv[2]));
    int8_t z = g_to_wire(atof(argv[3]));
    Wiimote_SetAccel(x, y, z);
    printf("accel g=(%.2f,%.2f,%.2f) wire=(%d,%d,%d)\n",
           atof(argv[1]), atof(argv[2]), atof(argv[3]), x, y, z);
    return 0;
}

static int cmd_whammy(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: whammy <0..31>\n");
        return 1;
    }
    Guitar_SetWhammy((uint8_t)atoi(argv[1]));
    printf("whammy %d\n", atoi(argv[1]));
    return 0;
}

static int cmd_ext(int argc, char **argv)
{
    bool on = (argc >= 2) && strcmp(argv[1], "on") == 0;
    bool off = (argc >= 2) && strcmp(argv[1], "off") == 0;
    if (!on && !off) {
        printf("usage: ext <on|off>  (guitar extension attached/detached)\n");
        return 1;
    }
    Wiimote_SetExtension(on);
    printf("guitar extension %s\n", on ? "connected" : "disconnected");
    return 0;
}

static int cmd_unlink(int argc, char **argv)
{
    (void)argc; (void)argv;
    Fauxmote_Unlink();
    printf("bond erased (re-pair with `pair` + SYNC)\n");
    return 0;
}

#ifdef CONFIG_FAUXMOTE_LINK_TRANSPORT_T1S
static int cmd_t1s(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("t1s link=%s synced=%d node_id=%u chipRev=%u\n",
           MfT1s_IsUp() ? "up" : "down", MfT1s_IsSynced(),
           MfT1s_NodeId(), MfT1s_ChipRev());
    printf("    rx=%lu tx=%lu err=%lu hb_seq=%lu\n",
           (unsigned long)MfT1s_RxCount(), (unsigned long)MfT1s_TxCount(),
           (unsigned long)MfT1s_ErrCount(), (unsigned long)MfT1s_HbSeq());
    return 0;
}
#endif

static void register_cmd(const char *name, const char *help, esp_console_cmd_func_t fn)
{
    const esp_console_cmd_t cmd = { .command = name, .help = help, .hint = NULL, .func = fn };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

void Cli_Start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "fauxmote>";
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    /* Dumb mode: no per-line terminal-width probe (its cursor-position read-back
     * races with typed input and spuriously commits lines). Trades line history/
     * editing for a stable console over the UART. */
    linenoiseSetDumbMode(1);

    register_cmd("pair", "enter pairing/sync mode (then press the Wii SYNC button)", cmd_pair);
    register_cmd("stop", "leave pairing mode (idle)", cmd_stop);
    register_cmd("reconnect", "device-initiated reconnect to the last bonded Wii", cmd_reconnect);
    register_cmd("disconnect", "close both HID channels + drop the ACL (stays bonded)", cmd_disconnect);
    register_cmd("btreset", "close the link + deinit/re-init the L2CAP layer", cmd_btreset);
    register_cmd("reboot", "restart fauxmote (heaviest reset; bond survives)", cmd_reboot);
    register_cmd("unlink", "erase the bond (link key) from NVS", cmd_unlink);
    register_cmd("status", "show BT / connection state", cmd_status);
    register_cmd("role", "role [slave|master] — show/switch the ACL role to the Wii (real Wiimotes are slave)", cmd_role);
    register_cmd("btn", "btn <name> <0|1> — hold/release a button (core or guitar)", cmd_btn);
    register_cmd("tap", "tap <name> — brief press+release (e.g. tap strumdown)", cmd_tap);
    register_cmd("point", "point <x 0..1> <y 0..1> | point off — IR cursor", cmd_point);
    register_cmd("accel", "accel <gx> <gy> <gz> | accel level — accelerometer, in g", cmd_accel);
    register_cmd("whammy", "whammy <0..31> — guitar whammy bar", cmd_whammy);
    register_cmd("ext", "ext <on|off> — attach/detach the guitar extension", cmd_ext);
#ifdef CONFIG_FAUXMOTE_LINK_TRANSPORT_T1S
    register_cmd("t1s", "show 10BASE-T1S link + MAC-PHY diagnostics", cmd_t1s);
#endif
    esp_console_register_help_command();

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
