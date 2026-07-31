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
#include "neopixel.h"
#include "beat_show.h"

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
    cli_printf("node:  lightshow (LED lighting)");
    cli_printf("mcu:   PIC32CM6408PL10048");
    cli_printf("role:  T1S PLCA follower id %u/%u", (unsigned)T1SFollower_NodeId(),
               (unsigned)T1SFollower_NodeCount());
    cli_printf("state: t1s follower up; WS2812 driver ready (see 'led')");
}

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
    cli_printf("rx:      %lu frames", (unsigned long)T1SFollower_RxCount());
    cli_printf("last:    0x%02X", (unsigned)T1SFollower_LastByte());
    cli_printf("errors:  %lu", (unsigned long)T1SFollower_ErrCount());
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

static void cmd_led(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)ctx;

    uint16_t n = embeddedCliGetTokenCount(args);
    if (n == 0u)
    {
        cli_printf("usage:");
        cli_printf("  led off");
        cli_printf("  led fill <r> <g> <b>");
        cli_printf("  led set <strand> <idx> <r> <g> <b>");
        cli_printf("  led test");
        return;
    }

    const char *sub = embeddedCliGetToken(args, 1u);

    if (strcmp(sub, "off") == 0)
    {
        NeoPixel_Clear();
    }
    else if (strcmp(sub, "fill") == 0)
    {
        uint8_t r, g, b;
        if ((n != 4u) || !parse_u8(embeddedCliGetToken(args, 2u), &r)
                      || !parse_u8(embeddedCliGetToken(args, 3u), &g)
                      || !parse_u8(embeddedCliGetToken(args, 4u), &b))
        {
            cli_printf("usage: led fill <r> <g> <b>");
            return;
        }
        for (uint8_t s = 0u; s < NEOPIXEL_STRANDS; s++)
        {
            for (uint16_t i = 0u; i < NEOPIXEL_COUNT; i++)
            {
                NeoPixel_SetPixel(s, i, r, g, b);
            }
        }
    }
    else if (strcmp(sub, "set") == 0)
    {
        uint8_t st, idx, r, g, b;
        if ((n != 6u) || !parse_u8(embeddedCliGetToken(args, 2u), &st)
                      || !parse_u8(embeddedCliGetToken(args, 3u), &idx)
                      || !parse_u8(embeddedCliGetToken(args, 4u), &r)
                      || !parse_u8(embeddedCliGetToken(args, 5u), &g)
                      || !parse_u8(embeddedCliGetToken(args, 6u), &b))
        {
            cli_printf("usage: led set <strand> <idx> <r> <g> <b>");
            return;
        }
        if ((st >= NEOPIXEL_STRANDS) || (idx >= NEOPIXEL_COUNT))
        {
            cli_printf("led: strand 0-%u, idx 0-%u", (unsigned)(NEOPIXEL_STRANDS - 1u),
                       (unsigned)(NEOPIXEL_COUNT - 1u));
            return;
        }
        NeoPixel_SetPixel(st, idx, r, g, b);
    }
    else if (strcmp(sub, "test") == 0)
    {
        /* R/G/B march on strand 0, a dim white every 4th pixel on strand 1.
         * Low levels keep the bring-up current modest. */
        NeoPixel_Clear();
        for (uint16_t i = 0u; i < NEOPIXEL_COUNT; i++)
        {
            uint8_t phase = (uint8_t)(i % 3u);
            NeoPixel_SetPixel(0u, i, (phase == 0u) ? 32u : 0u,
                                     (phase == 1u) ? 32u : 0u,
                                     (phase == 2u) ? 32u : 0u);
            uint8_t w = ((i % 4u) == 0u) ? 16u : 0u;
            NeoPixel_SetPixel(1u, i, w, w, w);
        }
    }
    else
    {
        cli_printf("led: unknown '%s'", sub);
        return;
    }

    if (NeoPixel_Show())
    {
        cli_printf("led: %u px/strand shown", (unsigned)NEOPIXEL_COUNT);
    }
    else
    {
        cli_printf("led: busy, try again");
    }
}

static const char *effect_name(uint8_t idx)
{
    switch (idx) {
        case 0u:  return "beat flash";
        case 1u:  return "dual comet";
        default:  return "split energy";
    }
}

static void cmd_show(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)ctx;

    const char *sub = embeddedCliGetToken(args, 1u);
    if (sub != NULL)
    {
        if (strcmp(sub, "auto") == 0)
        {
            BeatShow_SetAuto();
        }
        else
        {
            uint8_t idx;
            if (!parse_u8(sub, &idx) || (idx >= BEAT_SHOW_EFFECTS))
            {
                cli_printf("usage: show [0..%u | auto]", (unsigned)(BEAT_SHOW_EFFECTS - 1u));
                return;
            }
            BeatShow_SetEffect(idx);
        }
    }

    uint8_t seq, energy, bass, treble, kick, flags;
    BeatShow_GetLast(&seq, &energy, &bass, &treble, &kick, &flags);
    uint8_t cop, carg;
    uint32_t ccount;
    T1SFollower_LastCtrl(&cop, &carg, &ccount);
    cli_printf("output:  %s", BeatShow_IsEnabled() ? "on" : "off");
    cli_printf("effect:  %u (%s)%s", (unsigned)BeatShow_Effect(),
               effect_name(BeatShow_Effect()), BeatShow_IsAuto() ? " auto" : " locked");
    cli_printf("ctrl:    op=0x%02X arg=%u (%lu rx)", (unsigned)cop, (unsigned)carg,
               (unsigned long)ccount);
    cli_printf("frames:  %lu", (unsigned long)BeatShow_FrameCount());
    cli_printf("last:    seq=%u energy=%u bass=%u treble=%u kick=%u",
               (unsigned)seq, (unsigned)energy, (unsigned)bass,
               (unsigned)treble, (unsigned)kick);
    cli_printf("flags:   %s%s%s%s%s(0x%02X)",
               (flags & BEAT_FLAG_BASS)     ? "bass " : "",
               (flags & BEAT_FLAG_MID)      ? "mid "  : "",
               (flags & BEAT_FLAG_KICK)     ? "kick " : "",
               (flags & BEAT_FLAG_BIG)      ? "BIG "  : "",
               (flags & BEAT_FLAG_BASS_DOM) ? "dom "  : "",
               (unsigned)flags);
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
        { "info",  "Print node identity / bring-up state",           false, NULL, cmd_info },
        { "t1s",   "Print link / sync / chipRev / PLCA / counters",  false, NULL, cmd_t1s },
        { "id",    "Raw-read + log the MAC-PHY ID registers",        false, NULL, cmd_id },
        { "plca",  "Read + log the PLCA status register",            false, NULL, cmd_plca },
        { "led",   "Drive the WS2812 strands (off/fill/set/test)",   true,  NULL, cmd_led },
        { "show",  "Beat show: status; 'show <0-2>'/'show auto'",    true,  NULL, cmd_show },
        { "reset", "Reset the MCU (system reset)",                   false, NULL, cmd_reset },
    };
    for (size_t i = 0u; i < (sizeof(bindings) / sizeof(bindings[0])); i++)
    {
        (void)embeddedCliAddBinding(s_cli, bindings[i]);
    }
}

void CLI_Initialize(void)
{
    EmbeddedCliConfig *cfg = embeddedCliDefaultConfig();
    cfg->invitation         = "lightshow> ";
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
