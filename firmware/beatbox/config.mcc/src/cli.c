#include "cli.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <xc.h>

#include "embedded_cli.h"
#include "t1s_follower.h"
#include "rgb_led.h"
#include "audio.h"
#include "../mcc_generated_files/uart/uart2.h"

/* embedded-cli working buffer (static-allocation mode → no malloc). Sized for
 * the small config below; the requirement is checked at init. */
static CLI_UINT     s_cli_buf[BYTES_TO_CLI_UINTS(1024)];
static EmbeddedCli *s_cli;

static void uart_str(const char *s)
{
    for (const char *p = s; *p != '\0'; p++)
    {
        UART2_Write((uint8_t)*p);
    }
}

/* Char sink for embedded-cli. UART2_Write queues to the interrupt-driven TX
 * ring and busy-waits only if the ring is full, so it blocks at most for wire
 * rate — no listener needed. */
static void cli_write_char(EmbeddedCli *cli, char c)
{
    (void)cli;
    UART2_Write((uint8_t)c);
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

static bool parse_u8(const char *s, uint8_t *out)
{
    if (s == NULL)
    {
        return false;
    }
    char *end;
    unsigned long v = strtoul(s, &end, 0);
    if ((*end != '\0') || (v > 255ul))
    {
        return false;
    }
    *out = (uint8_t)v;
    return true;
}

/* ---- commands ----------------------------------------------------------- */

static void cmd_info(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)args;
    (void)ctx;
    cli_printf("node:  beatbox (beat source)");
    cli_printf("mcu:   dsPIC33AK512MPS512");
    cli_printf("role:  T1S PLCA follower id 5");
    cli_printf("state: bring-up; UART2 CLI + T1S follower up");
}

static void cmd_t1s(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)ctx;
    const char *sub = embeddedCliGetToken(args, 1);
    if (sub != NULL)
    {
        if (strcmp(sub, "id") == 0)
        {
            cli_printf("reading MAC-PHY id registers...");
            T1SFollower_ReadId();     /* results log asynchronously from the service loop */
            return;
        }
        if (strcmp(sub, "plca") == 0)
        {
            cli_printf("reading PLCA status...");
            T1SFollower_ReadPlca();   /* result logs asynchronously from the service loop */
            return;
        }
        cli_printf("usage: t1s [id|plca]");
        return;
    }

    bool cfgsync = false;
    uint8_t txc = 0u, rxc = 0u;
    T1SFollower_GetState(&cfgsync, &txc, &rxc);

    unsigned rev = (unsigned)T1SFollower_ChipRev();
    if ((rev == 0u) || (rev == 0xFFu))
    {
        cli_printf("chip:    absent (rev 0x%02X)", rev);
    }
    else
    {
        cli_printf("chip:    LAN8651 rev %u", rev);
    }
    cli_printf("init:    %s", T1SFollower_IsInitialized() ? "done" : "pending");
    cli_printf("plca:    %s (follower %u/%u)",
               T1SFollower_IsConnected() ? "operating" : "idle - no coordinator",
               (unsigned)T1SFollower_NodeId(), (unsigned)T1SFollower_NodeCount());
    cli_printf("cfgsync: %s", cfgsync ? "yes" : "no");
    cli_printf("credits: tx=%u rx=%u", (unsigned)txc, (unsigned)rxc);
    cli_printf("rx:      %lu frames", (unsigned long)T1SFollower_RxCount());
    cli_printf("last:    0x%02X", (unsigned)T1SFollower_LastCmd());
    cli_printf("errors:  %lu", (unsigned long)T1SFollower_ErrCount());
}

static void cmd_rgb(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)ctx;
    const char *sub = embeddedCliGetToken(args, 1u);
    if ((sub != NULL) && (strcmp(sub, "off") == 0))
    {
        RGB_LED_Off();
        cli_printf("rgb: off");
        return;
    }
    uint8_t r = 0u, g = 0u, b = 0u;
    if ((embeddedCliGetTokenCount(args) != 3u)
        || !parse_u8(embeddedCliGetToken(args, 1u), &r)
        || !parse_u8(embeddedCliGetToken(args, 2u), &g)
        || !parse_u8(embeddedCliGetToken(args, 3u), &b))
    {
        cli_printf("usage: rgb <r> <g> <b>  (0-255) | rgb off");
        return;
    }
    RGB_LED_Set(r, g, b);
    cli_printf("rgb: r=%u g=%u b=%u", (unsigned)r, (unsigned)g, (unsigned)b);
}

static void cmd_audio(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)args;
    (void)ctx;
    uint16_t l = 0u, r = 0u;
    uint16_t lmin = 0u, lmax = 0u, rmin = 0u, rmax = 0u;
    Audio_GetLevels(&l, &r);
    Audio_GetPeaks(&lmin, &lmax, &rmin, &rmax);
    cli_printf("audio now:  L=%u R=%u (raw 0-65535, mid 32768)", (unsigned)l, (unsigned)r);
    cli_printf("audio peak: L=%u..%u pp=%u  R=%u..%u pp=%u",
               (unsigned)lmin, (unsigned)lmax, (unsigned)(lmax - lmin),
               (unsigned)rmin, (unsigned)rmax, (unsigned)(rmax - rmin));
}

static void cmd_reset(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)args;
    (void)ctx;
    /* Write straight to the TX ring (not embedded-cli's deferred print, which
     * only flushes on the next process pass we never reach) and drain it to the
     * wire before the core resets. */
    uart_str("\r\nresetting...\r\n");
    while (!UART2_IsTxDone())
    {
    }
    __asm__ volatile ("reset");
}

static void register_commands(void)
{
    static const CliCommandBinding bindings[] = {
        { "info",  "Print node identity / bring-up state",              false, NULL, cmd_info },
        { "t1s",   "T1S link status; 't1s id'/'t1s plca' run diagnostics", true,  NULL, cmd_t1s },
        { "rgb",   "Set RGB LED: 'rgb <r> <g> <b>' (0-255) or 'rgb off'",   true,  NULL, cmd_rgb },
        { "audio", "Print latest L/R sample + peak envelope since last call", false, NULL, cmd_audio },
        { "reset", "Reset the MCU (software reset)",                     false, NULL, cmd_reset },
    };
    for (size_t i = 0u; i < (sizeof(bindings) / sizeof(bindings[0])); i++)
    {
        (void)embeddedCliAddBinding(s_cli, bindings[i]);
    }
}

void CLI_Initialize(void)
{
    EmbeddedCliConfig *cfg = embeddedCliDefaultConfig();
    cfg->invitation         = "beatbox> ";
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
    while (UART2_IsRxReady())
    {
        embeddedCliReceiveChar(s_cli, (char)UART2_Read());
    }
    embeddedCliProcess(s_cli);
}
