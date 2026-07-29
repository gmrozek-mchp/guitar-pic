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
#include "servo.h"

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
    cli_printf("role:  T1S PLCA follower id %u/%u", (unsigned)T1SFollower_NodeId(),
               (unsigned)T1SFollower_NodeCount());
    cli_printf("state: t1s follower up; servos at neck=%u jaw=%u us",
               (unsigned)Servo_GetPulseUs(SERVO_NECK),
               (unsigned)Servo_GetPulseUs(SERVO_JAW));
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
    int8_t neck = 0, jaw = 0;
    T1SFollower_LastCmd(&neck, &jaw);
    cli_printf("cmd:     neck=%d jaw=%d", (int)neck, (int)jaw);
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

static const char *servo_name(servo_id_t s)
{
    return (s == SERVO_NECK) ? "neck" : "jaw";
}

static bool parse_servo(const char *a, servo_id_t *out)
{
    if ((strcmp(a, "neck") == 0) || (strcmp(a, "0") == 0)) { *out = SERVO_NECK; return true; }
    if ((strcmp(a, "jaw")  == 0) || (strcmp(a, "1") == 0)) { *out = SERVO_JAW;  return true; }
    return false;
}

static void cmd_servo(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)ctx;
    const char *a = embeddedCliGetToken(args, 1);
    if (a == NULL)
    {
        cli_printf("neck: %u us", (unsigned)Servo_GetPulseUs(SERVO_NECK));
        cli_printf("jaw:  %u us", (unsigned)Servo_GetPulseUs(SERVO_JAW));
        cli_printf("usage: servo <neck|jaw> <%u..%u us>",
                   (unsigned)SERVO_US_MIN, (unsigned)SERVO_US_MAX);
        return;
    }

    servo_id_t servo;
    if (!parse_servo(a, &servo)) { cli_printf("bad servo '%s' (neck|jaw)", a); return; }

    const char *b = embeddedCliGetToken(args, 2);
    if (b == NULL)
    {
        cli_printf("usage: servo <neck|jaw> <%u..%u us>",
                   (unsigned)SERVO_US_MIN, (unsigned)SERVO_US_MAX);
        return;
    }

    uint16_t req = (uint16_t)strtoul(b, NULL, 10);
    uint16_t got = Servo_SetPulseUs(servo, req);
    cli_printf("%s = %u us%s", servo_name(servo),
               (unsigned)got, (got != req) ? " (clamped)" : "");
}

static void cmd_pos(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)ctx;
    const char *a = embeddedCliGetToken(args, 1);
    if (a == NULL)
    {
        cli_printf("neck: %d  jaw: %d  (range %d..%d, 0 = neutral)",
                   (int)Servo_GetPosition(SERVO_NECK), (int)Servo_GetPosition(SERVO_JAW),
                   SERVO_POS_MIN, SERVO_POS_MAX);
        cli_printf("usage: pos <neck|jaw> <%d..%d>", SERVO_POS_MIN, SERVO_POS_MAX);
        return;
    }

    servo_id_t servo;
    if (!parse_servo(a, &servo)) { cli_printf("bad servo '%s' (neck|jaw)", a); return; }

    const char *b = embeddedCliGetToken(args, 2);
    if (b == NULL)
    {
        cli_printf("usage: pos <neck|jaw> <%d..%d>", SERVO_POS_MIN, SERVO_POS_MAX);
        return;
    }

    long req = strtol(b, NULL, 10);
    if (req > SERVO_POS_MAX) { req = SERVO_POS_MAX; }
    if (req < SERVO_POS_MIN) { req = SERVO_POS_MIN; }
    int8_t got = Servo_SetPosition(servo, (int8_t)req);
    cli_printf("%s pos=%d -> %u us", servo_name(servo),
               (int)got, (unsigned)Servo_GetPulseUs(servo));
}

/* Print one servo's calibration as a paste-ready C initializer for servo.c. */
static void print_cal(servo_id_t servo)
{
    servo_cal_t c = Servo_GetCal(servo);
    cli_printf("[SERVO_%s] = { .min_us = %uu, .neutral_us = %uu, .max_us = %uu, .invert = %s },",
               (servo == SERVO_NECK) ? "NECK" : "JAW",
               (unsigned)c.min_us, (unsigned)c.neutral_us, (unsigned)c.max_us,
               c.invert ? "true" : "false");
}

static void cmd_cal(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli;
    (void)ctx;
    const char *a = embeddedCliGetToken(args, 1);
    if ((a == NULL) || (strcmp(a, "show") == 0))
    {
        print_cal(SERVO_NECK);
        print_cal(SERVO_JAW);
        return;
    }

    servo_id_t servo;
    if (!parse_servo(a, &servo))
    {
        cli_printf("usage: cal [show] | cal <neck|jaw> <min|neutral|max|invert> <val>");
        return;
    }

    const char *field = embeddedCliGetToken(args, 2);
    const char *val   = embeddedCliGetToken(args, 3);
    if ((field == NULL) || (val == NULL))
    {
        print_cal(servo);
        cli_printf("usage: cal <neck|jaw> <min|neutral|max|invert> <val>");
        return;
    }

    servo_cal_t c = Servo_GetCal(servo);
    uint32_t    v = (uint32_t)strtoul(val, NULL, 10);
    if      (strcmp(field, "min")     == 0) { c.min_us     = (uint16_t)v; }
    else if (strcmp(field, "neutral") == 0) { c.neutral_us = (uint16_t)v; }
    else if (strcmp(field, "max")     == 0) { c.max_us     = (uint16_t)v; }
    else if (strcmp(field, "invert")  == 0) { c.invert     = (v != 0u); }
    else { cli_printf("bad field '%s' (min|neutral|max|invert)", field); return; }

    Servo_SetCal(servo, c);   /* re-applies current position under new cal */
    print_cal(servo);         /* echo applied values (post guard-rail clamp) */
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
        { "servo", "Raw servo pulse: servo <neck|jaw> <us>",         true,  NULL, cmd_servo },
        { "pos",   "Position via cal: pos <neck|jaw> <-127..127>",    true,  NULL, cmd_pos },
        { "cal",   "Servo cal: cal [show] | cal <s> <field> <val>",   true,  NULL, cmd_cal },
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
    cfg->invitation         = "lemmy> ";
    cfg->rxBufferSize       = 32u;
    cfg->cmdBufferSize      = 32u;
    cfg->historyBufferSize  = 64u;
    cfg->maxBindingCount    = 12u;
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
