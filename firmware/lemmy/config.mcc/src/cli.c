#include "cli.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "definitions.h"   /* SERCOM1_USART_*, SYSTICK_* */
#include "embedded_cli.h"

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

static void cmd_info(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)args;
    (void)ctx;
    cli_printf("node:  lemmy (animation)");
    cli_printf("mcu:   PIC32CM6408PL10048");
    cli_printf("role:  T1S PLCA follower id 6 (planned)");
    cli_printf("state: cli up; t1s + servos not yet wired");
}

static void cmd_reset(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)args;
    (void)ctx;
    /* Write straight to the TX ring (not embedded-cli's deferred print, which
     * only flushes on the next process pass we never reach) so the delay drains
     * it to the wire before the core resets. */
    uart_str("\r\nresetting...\r\n");
    SYSTICK_DelayMs(20u);   /* let the TX ring drain before the reset */
    NVIC_SystemReset();
}

static void register_commands(void)
{
    static const CliCommandBinding bindings[] = {
        { "info",  "Print node identity / bring-up state", false, NULL, cmd_info },
        { "reset", "Reset the MCU (system reset)",          false, NULL, cmd_reset },
    };
    for (size_t i = 0u; i < (sizeof(bindings) / sizeof(bindings[0])); i++)
    {
        (void)embeddedCliAddBinding(s_cli, bindings[i]);
    }
}

void CLI_Initialize(void)
{
    EmbeddedCliConfig *cfg = embeddedCliDefaultConfig();
    cfg->invitation         = "lemmy> ";
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

    uart_str("\r\nlemmy: boot - cli\r\n");
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
