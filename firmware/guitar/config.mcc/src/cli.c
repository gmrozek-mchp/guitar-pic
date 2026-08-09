#include "cli.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "definitions.h"   /* SERCOM1_USART_*, SYSTICK_* */
#include "embedded_cli.h"
#include "t1s_follower.h"

/* embedded-cli working buffer (static-allocation mode → no malloc). Sized for
 * the small config below; the requirement is checked at init. */
static CLI_UINT     s_cli_buf[BYTES_TO_CLI_UINTS(1024)];
static EmbeddedCli *s_cli;

static void uart_str(const char *s)
{
    (void)SERCOM1_USART_Write((uint8_t *)s, strlen(s));
}

/* Char sink for embedded-cli. The SERCOM1 TX ring drains at the wire rate via
 * its ISR regardless of any listener, so a bounded retry only ever waits for
 * ring space. */
static void cli_write_char(EmbeddedCli *cli, char c)
{
    (void)cli;
    for (uint32_t tries = 0u; tries < 1000u; tries++)
    {
        if (SERCOM1_USART_Write((uint8_t *)&c, 1u) == 1u) { return; }
        SYSTICK_DelayMs(1u);
    }
}

static void cli_printf(const char *fmt, ...)
{
    static char buf[128];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    embeddedCliPrint(s_cli, buf);   /* appends newline */
}

/* ---- commands ----------------------------------------------------------- */

static void cmd_t1s(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)args;
    (void)ctx;
    bool synced = false;
    uint8_t txc = 0u, rxc = 0u;
    T1SFollower_GetState(&synced, &txc, &rxc);
    cli_printf("link:    %s", T1SFollower_IsConnected() ? "up" : "down");
    cli_printf("synced:  %s", synced ? "yes" : "no");
    cli_printf("chipRev: %u", (unsigned)T1SFollower_ChipRev());
    cli_printf("plca:    follower id=%u/%u", (unsigned)T1SFollower_NodeId(),
               (unsigned)T1SFollower_NodeCount());
    cli_printf("credits: tx=%u rx=%u", (unsigned)txc, (unsigned)rxc);
    cli_printf("rx cmds: %lu", (unsigned long)T1SFollower_RxCount());
    cli_printf("last:    0x%02X", (unsigned)T1SFollower_LastCmd());
    cli_printf("output:  %s", T1SFollower_IsOutputEnabled() ? "on" : "off");

    uint8_t  cop = 0u, carg = 0u;
    uint32_t ccount = 0u;
    T1SFollower_LastCtrl(&cop, &carg, &ccount);
    cli_printf("ctrl:    op=%u arg=%u count=%lu", (unsigned)cop, (unsigned)carg,
               (unsigned long)ccount);
    cli_printf("errors:  %lu", (unsigned long)T1SFollower_ErrCount());
}

static void cmd_output(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)ctx;
    const char *a = embeddedCliGetToken(args, 1);
    if (a == NULL || (strcmp(a, "on") != 0 && strcmp(a, "off") != 0))
    {
        cli_printf("usage: output <on|off>  (currently %s)",
                   T1SFollower_IsOutputEnabled() ? "on" : "off");
        return;
    }
    bool on = (strcmp(a, "on") == 0);
    T1SFollower_SetOutputEnabled(on);
    cli_printf("output %s", on ? "on" : "off");
    /* The coordinator reconciles its own wanted state against the heartbeat, so a
     * local change here is transient while marvin is on the bus. */
    cli_printf("(marvin overrides this within ~1s if it disagrees)");
}

static void cmd_btn(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)ctx;
    const char *a = embeddedCliGetToken(args, 1);
    if (a == NULL)
    {
        cli_printf("usage: btn <mask hex>  (bit0 G,1 R,2 Y,3 B,4 O,5 Sdn,6 Sup; 0=release)");
        return;
    }
    uint8_t m = (uint8_t)(strtoul(a, NULL, 16) & 0x7Fu);
    T1SFollower_ApplyButtons(m);
    cli_printf("buttons = 0x%02X", (unsigned)m);
}

static void cmd_tap(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)ctx;
    const char *a = embeddedCliGetToken(args, 1);
    if (a == NULL)
    {
        cli_printf("usage: tap <mask hex> [ms]");
        return;
    }
    uint8_t  m  = (uint8_t)(strtoul(a, NULL, 16) & 0x7Fu);
    const char *b = embeddedCliGetToken(args, 2);
    uint32_t ms = (b != NULL) ? (uint32_t)strtoul(b, NULL, 10) : 60u;

    T1SFollower_ApplyButtons(m);
    SYSTICK_DelayMs(ms);
    T1SFollower_ReleaseButtons();
    cli_printf("tap 0x%02X %lums", (unsigned)m, (unsigned long)ms);
}

static void cmd_id(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)args;
    (void)ctx;
    cli_printf("reading MAC-PHY id registers...");
    T1SFollower_ReadId();   /* results log asynchronously from the service loop */
}

static void cmd_plca(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)args;
    (void)ctx;
    cli_printf("reading PLCA status...");
    T1SFollower_ReadPlca();   /* result logs asynchronously from the service loop */
}

static void register_commands(void)
{
    static const CliCommandBinding bindings[] = {
        { "t1s",    "Print link / sync / chipRev / PLCA / counters", false, NULL, cmd_t1s },
        { "btn",    "btn <mask hex>: drive the 7 button GPIOs (0 = release all)", true, NULL, cmd_btn },
        { "tap",    "tap <mask hex> [ms]: assert then release (default 60 ms)",   true, NULL, cmd_tap },
        { "output", "output <on|off>: gate the button GPIOs (all sources)",       true, NULL, cmd_output },
        { "id",     "Raw-read + log the MAC-PHY ID registers (SPI diagnostic)",   false, NULL, cmd_id },
        { "plca",   "Read + log the PLCA status register",                        false, NULL, cmd_plca },
    };
    for (size_t i = 0u; i < (sizeof(bindings) / sizeof(bindings[0])); i++)
    {
        (void)embeddedCliAddBinding(s_cli, bindings[i]);
    }
}

void CLI_Initialize(void)
{
    EmbeddedCliConfig *cfg = embeddedCliDefaultConfig();
    cfg->invitation         = "guitar> ";
    cfg->rxBufferSize       = 32u;
    cfg->cmdBufferSize      = 32u;
    cfg->historyBufferSize  = 64u;
    cfg->maxBindingCount    = 8u;
    cfg->enableAutoComplete = true;
    cfg->cliBuffer          = s_cli_buf;
    cfg->cliBufferSize      = sizeof(s_cli_buf);

    if (embeddedCliRequiredSize(cfg) > sizeof(s_cli_buf))
    {
        uart_str("cli: buffer too small\r\n");
        return;
    }

    s_cli = embeddedCliNew(cfg);
    if (s_cli == NULL)
    {
        uart_str("cli: init failed\r\n");
        return;
    }
    s_cli->writeChar = cli_write_char;

    register_commands();
    embeddedCliProcess(s_cli);   /* emit the first prompt */
}

void CLI_Tasks(void)
{
    if (s_cli == NULL)
    {
        return;
    }
    uint8_t c;
    while (SERCOM1_USART_Read(&c, 1u) == 1u)
    {
        embeddedCliReceiveChar(s_cli, (char)c);
    }
    embeddedCliProcess(s_cli);
}
