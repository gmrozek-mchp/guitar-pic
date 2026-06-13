#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "esp_bt_device.h"

#include "console_cli.h"
#include "bt_hid_device.h"
#include "wiimote.h"

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

static int cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    print_bda("bd_addr   ", esp_bt_dev_get_address());
    print_bda("bonded Wii", Fauxmote_WiiAddr());
    printf("discoverable=%d connected=%d assigned=%d report_mode=0x%02x\n",
           Fauxmote_IsDiscoverable(), Wiimote_IsConnected(), Wiimote_IsAssigned(),
           Wiimote_ReportMode());
    return 0;
}

static int cmd_btn(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: btn <a|b|one|two|plus|minus|home|up|down|left|right> <0|1>\n");
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

static int cmd_unlink(int argc, char **argv)
{
    (void)argc; (void)argv;
    Fauxmote_Unlink();
    printf("bond erased (re-pair with `pair` + SYNC)\n");
    return 0;
}

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

    register_cmd("pair", "enter pairing/sync mode (then press the Wii SYNC button)", cmd_pair);
    register_cmd("stop", "leave pairing mode (idle)", cmd_stop);
    register_cmd("reconnect", "device-initiated reconnect to the last bonded Wii", cmd_reconnect);
    register_cmd("unlink", "erase the bond (link key) from NVS", cmd_unlink);
    register_cmd("status", "show BT / connection state", cmd_status);
    register_cmd("btn", "btn <name> <0|1> — hold/release a button", cmd_btn);
    register_cmd("tap", "tap <name> — brief press+release", cmd_tap);
    esp_console_register_help_command();

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
