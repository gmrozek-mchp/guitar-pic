#include "cli.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "definitions.h"   /* SERCOM1_USART_*, SYSTICK_* */
#include "embedded_cli.h"
#include "t1s_detector.h"
#include "fret_scan.h"
#include "model_infer.h"   /* MODEL_SEL_*, name/effective/trained helpers */

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
    T1SDetector_GetState(&synced, &txc, &rxc);
    cli_printf("link:    %s", T1SDetector_IsConnected() ? "up" : "down");
    cli_printf("synced:  %s", synced ? "yes" : "no");
    cli_printf("chipRev: %u", (unsigned)T1SDetector_ChipRev());
    cli_printf("plca:    follower id=%u/%u", (unsigned)T1SDetector_NodeId(),
               (unsigned)T1SDetector_NodeCount());
    cli_printf("credits: tx=%u rx=%u", (unsigned)txc, (unsigned)rxc);
    cli_printf("data tx: %lu (-> coordinator) stream=%s", (unsigned long)T1SDetector_TxCount(),
               T1SDetector_StreamEnabled() ? "on" : "off");
    cli_printf("cmd tx:  %lu (-> guitar) last=0x%02X", (unsigned long)T1SDetector_CmdCount(),
               (unsigned)T1SDetector_LastCmd());
    uint8_t cop = 0u, carg = 0u;
    uint32_t ccnt = 0u;
    T1SDetector_LastCtrl(&cop, &carg, &ccnt);
    cli_printf("ctrl rx: %lu last op=0x%02X arg=%u", (unsigned long)ccnt,
               (unsigned)cop, (unsigned)carg);
    cli_printf("arm:     %s", T1SDetector_Armed() ? "on" : "off");
    cli_printf("errors:  %lu", (unsigned long)T1SDetector_ErrCount());
}

static void cmd_arm(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)ctx;
    const char *a = embeddedCliGetToken(args, 1);
    if (a == NULL)
    {
        cli_printf("arm: %s", T1SDetector_Armed() ? "on" : "off");
        return;
    }
    if (strcmp(a, "on") == 0)
    {
        T1SDetector_SetArmed(true);
    }
    else if (strcmp(a, "off") == 0)
    {
        T1SDetector_SetArmed(false);
    }
    else
    {
        cli_printf("usage: arm [on|off]");
        return;
    }
    cli_printf("arm: %s", T1SDetector_Armed() ? "on" : "off");
}

static void cmd_stream(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)ctx;
    const char *a = embeddedCliGetToken(args, 1);
    if (a == NULL)
    {
        cli_printf("stream: %s", T1SDetector_StreamEnabled() ? "on" : "off");
        return;
    }
    if (strcmp(a, "on") == 0)
    {
        T1SDetector_SetStream(true);
    }
    else if (strcmp(a, "off") == 0)
    {
        T1SDetector_SetStream(false);
    }
    else
    {
        cli_printf("usage: stream [on|off]");
        return;
    }
    cli_printf("stream: %s", T1SDetector_StreamEnabled() ? "on" : "off");
}

static void cmd_model(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)ctx;
    const char *a = embeddedCliGetToken(args, 1);
    if (a == NULL)
    {
        uint8_t cur = T1SDetector_ModelSel();
        cli_printf("model: %s (effective: %s)", model_infer_sel_name(cur),
                   model_infer_sel_name(model_infer_effective()));
        for (uint8_t i = 0u; i < MODEL_SEL_COUNT; i++)
        {
            const char *mark = "";
            if (i == MODEL_SEL_AUTO)              { mark = " (adaptive stub -> hard)"; }
            else if (!model_infer_sel_trained(i)) { mark = " (placeholder -> hard)"; }
            cli_printf("  %c %s%s", (i == cur) ? '*' : ' ', model_infer_sel_name(i), mark);
        }
        return;
    }
    for (uint8_t i = 0u; i < MODEL_SEL_COUNT; i++)
    {
        if (strcmp(a, model_infer_sel_name(i)) == 0)
        {
            T1SDetector_SetModelSel(i);   /* main.c applies to the engine next pass */
            cli_printf("model: %s", model_infer_sel_name(i));
            return;
        }
    }
    cli_printf("usage: model [easy|medium|hard|expert|auto]");
}

static void cmd_adc(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)args;
    (void)ctx;
    /* Latest scan from the 240 Hz TC0 ISR (lower = note present). */
    cli_printf("adc: G=%u R=%u Y=%u B=%u O=%u",
               (unsigned)fret_scan_result(FRET_GREEN),
               (unsigned)fret_scan_result(FRET_RED),
               (unsigned)fret_scan_result(FRET_YELLOW),
               (unsigned)fret_scan_result(FRET_BLUE),
               (unsigned)fret_scan_result(FRET_ORANGE));
}

static void cmd_id(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)args;
    (void)ctx;
    cli_printf("reading MAC-PHY id registers...");
    T1SDetector_ReadId();   /* results log asynchronously from the service loop */
}

static void cmd_plca(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)args;
    (void)ctx;
    cli_printf("reading PLCA status...");
    T1SDetector_ReadPlca();   /* result logs asynchronously from the service loop */
}

static void register_commands(void)
{
    static const CliCommandBinding bindings[] = {
        { "t1s",  "Print link / sync / chipRev / PLCA / counters",  false, NULL, cmd_t1s },
        { "arm",  "arm [on|off]: gate guitar actuation (no arg = show state)", true, NULL, cmd_arm },
        { "stream", "stream [on|off]: gate the data stream to marvin (no arg = show state)", true, NULL, cmd_stream },
        { "model", "model [easy|medium|hard|expert|auto]: select inference model (no arg = list)", true, NULL, cmd_model },
        { "adc",  "Print the latest 5-channel phototransistor scan", false, NULL, cmd_adc },
        { "id",   "Raw-read + log the MAC-PHY ID registers (SPI diagnostic)", false, NULL, cmd_id },
        { "plca", "Read + log the PLCA status register",            false, NULL, cmd_plca },
    };
    for (size_t i = 0u; i < (sizeof(bindings) / sizeof(bindings[0])); i++)
    {
        (void)embeddedCliAddBinding(s_cli, bindings[i]);
    }
}

void CLI_Initialize(void)
{
    EmbeddedCliConfig *cfg = embeddedCliDefaultConfig();
    cfg->invitation         = "fretboard> ";
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
