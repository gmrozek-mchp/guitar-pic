#include "console.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "definitions.h"
#include "log.h"
#include "embedded_cli.h"

#include "actuator/timing_pipeline.h"
#include "actuator/manual_control.h"
#include "actuator/fretboard_link.h"
#include "detector/detector.h"
#include "video/video.h"
#include "game/fret.h"
#include "net/t1s/t1s_link.h"

#define CON_TASK_STACK_WORDS  1024u
#define CON_TASK_PRIORITY     2u      /* low / UI band — human-interactive */

#define CON_RX_THRESHOLD      1u      /* wake on any inbound byte */
#define CON_RX_WAIT_MS        100u    /* bounded so a missed notify can't wedge */

/* embedded-cli internal buffer (static-allocation mode → no malloc). Sized
 * generously for the config below; the actual requirement is asserted at init,
 * so bump this if the assert ever fires after a config change. */
static CLI_UINT s_cli_buf[BYTES_TO_CLI_UINTS(1024)];

static EmbeddedCli *s_cli;

static StackType_t   s_task_stack[CON_TASK_STACK_WORDS];
static StaticTask_t  s_task_tcb;

/* Given from the FLEXCOM2 read callback (ISR) when the RX ring has a byte;
 * woken task drains and feeds the line editor. */
static SemaphoreHandle_t s_rx_notify;
static StaticSemaphore_t s_rx_notify_buf;

/* ---- output ------------------------------------------------------------- */

static void console_write_char(EmbeddedCli *cli, char c)
{
    (void)cli;
    /* TX ring drains at line rate regardless of any listener (the UART clocks
     * bytes onto the wire), so a bounded retry only ever waits for ring space,
     * never for a reader. Cap it so a pathological full-ring can't spin. */
    for (uint32_t tries = 0u; tries < 1000u; tries++)
    {
        if (FLEXCOM2_USART_Write((uint8_t *)&c, 1u) == 1u) { return; }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void console_printf(const char *fmt, ...)
{
    static char buf[160];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    embeddedCliPrint(s_cli, buf);   /* appends newline */
}

/* ---- arg parsing helpers ------------------------------------------------ */

static int parse_onoff(const char *s)
{
    if (s == NULL) { return -1; }
    if (strcmp(s, "on") == 0 || strcmp(s, "1") == 0)  { return 1; }
    if (strcmp(s, "off") == 0 || strcmp(s, "0") == 0) { return 0; }
    return -1;
}

static int parse_detector(const char *s)
{
    if (s == NULL) { return -1; }
    if (strcmp(s, "cv") == 0)  { return DETECTOR_CV_MARVIN_V1; }
    if (strcmp(s, "adc") == 0) { return DETECTOR_ADC_FRETBOARD; }
    return -1;
}

static int parse_fret(const char *s)
{
    if (s == NULL || s[0] == '\0' || s[1] != '\0') { return -1; }
    switch (s[0])
    {
        case 'g': case 'G': return FRET_GREEN;
        case 'r': case 'R': return FRET_RED;
        case 'y': case 'Y': return FRET_YELLOW;
        case 'b': case 'B': return FRET_BLUE;
        case 'o': case 'O': return FRET_ORANGE;
        default:            return -1;
    }
}

/* ---- command handlers --------------------------------------------------- */

static void cmd_status(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)args; (void)ctx;

    detector_id_t act = Detector_GetActive();
    Video_FrameInfo vi;
    Video_GetFrameInfo(&vi);

    console_printf("link:       %s", FretboardLink_IsConnected() ? "up" : "down");
    console_printf("active:     %s", (act == DETECTOR_CV_MARVIN_V1) ? "cv" : "adc");
    console_printf("detect cv:  %s", Detector_IsEnabled(DETECTOR_CV_MARVIN_V1)  ? "on" : "off");
    console_printf("detect adc: %s", Detector_IsEnabled(DETECTOR_ADC_FRETBOARD) ? "on" : "off");
    console_printf("manual:     %s", ManualControl_IsEnabled() ? "on" : "off");
    console_printf("video:      %ux%u frame=%lu",
                   (unsigned)vi.width, (unsigned)vi.height,
                   (unsigned long)vi.frame_count);
}

static void cmd_t1s(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)args; (void)ctx;

    bool synced = false;
    uint8_t txc = 0u, rxc = 0u;
    T1SLink_GetState(&synced, &txc, &rxc);

    console_printf("link:    %s", T1SLink_IsConnected() ? "up" : "down");
    console_printf("synced:  %s", synced ? "yes" : "no");
    console_printf("chipRev: %u", (unsigned)T1SLink_ChipRev());
    console_printf("plca:    coordinator id=%u/%u",
                   (unsigned)T1SLink_NodeId(), (unsigned)T1SLink_NodeCount());
    console_printf("credits: tx=%u rx=%u", (unsigned)txc, (unsigned)rxc);
    console_printf("tx cmds: %lu", (unsigned long)T1SLink_TxCount());
    console_printf("rx frms: %lu", (unsigned long)T1SLink_RxCount());
}

static void cmd_nodes(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)args; (void)ctx;

    uint8_t n = T1SLink_NodeTableCount();
    console_printf("id  type      present  last-hb");
    for (uint8_t i = 0u; i < n; i++)
    {
        T1SLink_NodeInfo ni;
        if (T1SLink_GetNodeInfo(i, &ni))
        {
            console_printf("%-3u %-9s %-7s  %lums",
                           (unsigned)ni.node_id, ni.type,
                           ni.present ? "yes" : "no",
                           (unsigned long)ni.age_ms);
        }
    }
}

static void cmd_detect(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    int id  = parse_detector(embeddedCliGetToken(args, 1));
    int val = parse_onoff(embeddedCliGetToken(args, 2));
    if (id < 0 || val < 0)
    {
        console_printf("usage: detect <cv|adc> <on|off>");
        return;
    }
    if (val) { Detector_Enable((detector_id_t)id); }
    else     { Detector_Disable((detector_id_t)id); }
    console_printf("detect %s = %s", (id == DETECTOR_CV_MARVIN_V1) ? "cv" : "adc",
                   val ? "on" : "off");
}

static void cmd_active(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    int id = parse_detector(embeddedCliGetToken(args, 1));
    if (id < 0)
    {
        console_printf("usage: active <cv|adc>");
        return;
    }
    Detector_SetActive((detector_id_t)id);
    console_printf("active = %s", (id == DETECTOR_CV_MARVIN_V1) ? "cv" : "adc");
}

static void cmd_timing(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    int val = parse_onoff(embeddedCliGetToken(args, 1));
    if (val < 0)
    {
        console_printf("usage: timing <on|off>");
        return;
    }
    TimingPipeline_SetEnabled(val != 0);
    console_printf("timing = %s", val ? "on" : "off");
}

static void cmd_manual(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    int val = parse_onoff(embeddedCliGetToken(args, 1));
    if (val < 0)
    {
        console_printf("usage: manual <on|off>");
        return;
    }
    ManualControl_SetEnabled(val != 0);
    console_printf("manual = %s", val ? "on" : "off");
}

static void cmd_fret(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    int fret = parse_fret(embeddedCliGetToken(args, 1));
    int val  = parse_onoff(embeddedCliGetToken(args, 2));
    if (fret < 0 || val < 0)
    {
        console_printf("usage: fret <g|r|y|b|o> <0|1>");
        return;
    }
    if (!ManualControl_IsEnabled())
    {
        console_printf("note: manual mode off — run 'manual on' first");
    }
    ManualControl_SetFret((fret_t)fret, val != 0);
    console_printf("fret %s = %d", embeddedCliGetToken(args, 1), val);
}

static void cmd_strum(EmbeddedCli *cli, char *args, void *ctx)
{
    (void)cli; (void)ctx;
    const char *dir = embeddedCliGetToken(args, 1);
    bool down;
    if      (dir != NULL && strcmp(dir, "down") == 0) { down = true; }
    else if (dir != NULL && strcmp(dir, "up") == 0)   { down = false; }
    else
    {
        console_printf("usage: strum <down|up>");
        return;
    }
    if (!ManualControl_IsEnabled())
    {
        console_printf("note: manual mode off — run 'manual on' first");
    }
    /* One-shot pulse: assert, brief hold, release. */
    ManualControl_SetStrum(down, true);
    vTaskDelay(pdMS_TO_TICKS(40));
    ManualControl_SetStrum(down, false);
    console_printf("strum %s", down ? "down" : "up");
}

static void register_commands(void)
{
    static const CliCommandBinding bindings[] = {
        { "status", "Print link / detector / mode / video state", false, NULL, cmd_status },
        { "t1s",    "Print T1S link / sync / PLCA / traffic counters",  false, NULL, cmd_t1s },
        { "nodes",  "List T1S nodes + heartbeat presence / last-seen",  false, NULL, cmd_nodes },
        { "detect", "detect <cv|adc> <on|off>: enable/disable a detector", true, NULL, cmd_detect },
        { "active", "active <cv|adc>: select the actuated detector",       true, NULL, cmd_active },
        { "timing", "timing <on|off>: marvin chord/strum scheduler",       true, NULL, cmd_timing },
        { "manual", "manual <on|off>: manual-control actuation mode",      true, NULL, cmd_manual },
        { "fret",   "fret <g|r|y|b|o> <0|1>: press/release a fret",        true, NULL, cmd_fret },
        { "strum",  "strum <down|up>: one strum pulse",                    true, NULL, cmd_strum },
    };
    for (size_t i = 0u; i < (sizeof(bindings) / sizeof(bindings[0])); i++)
    {
        (void)embeddedCliAddBinding(s_cli, bindings[i]);
    }
}

/* ---- transport ---------------------------------------------------------- */

static void rx_event_handler(FLEXCOM_USART_EVENT event, uintptr_t context)
{
    (void)context;
    BaseType_t hpw = pdFALSE;

    switch (event)
    {
        case FLEXCOM_USART_EVENT_READ_THRESHOLD_REACHED:
        case FLEXCOM_USART_EVENT_READ_BUFFER_FULL:
            (void)xSemaphoreGiveFromISR(s_rx_notify, &hpw);
            break;
        case FLEXCOM_USART_EVENT_READ_ERROR:
            (void)FLEXCOM2_USART_ErrorGet();
            (void)xSemaphoreGiveFromISR(s_rx_notify, &hpw);
            break;
        default:
            break;
    }

    portYIELD_FROM_ISR(hpw);
}

static void console_task(void *param)
{
    (void)param;

    LOG_INFO("CON: console started (FLEXCOM2)\r\n");

    for (;;)
    {
        if (FLEXCOM2_USART_ReadCountGet() == 0u)
        {
            (void)xSemaphoreTake(s_rx_notify, pdMS_TO_TICKS(CON_RX_WAIT_MS));
        }

        uint8_t c;
        while (FLEXCOM2_USART_Read(&c, 1u) == 1u)
        {
            embeddedCliReceiveChar(s_cli, (char)c);
        }

        embeddedCliProcess(s_cli);
    }
}

void Console_Initialize(void)
{
    EmbeddedCliConfig *cfg = embeddedCliDefaultConfig();
    cfg->invitation        = "marvin> ";
    cfg->rxBufferSize      = 64u;
    cfg->cmdBufferSize     = 64u;
    cfg->historyBufferSize = 128u;
    cfg->maxBindingCount   = 16u;
    cfg->enableAutoComplete = true;
    cfg->cliBuffer         = s_cli_buf;
    cfg->cliBufferSize     = sizeof(s_cli_buf);

    configASSERT(embeddedCliRequiredSize(cfg) <= sizeof(s_cli_buf));

    s_cli = embeddedCliNew(cfg);
    configASSERT(s_cli != NULL);
    s_cli->writeChar = console_write_char;

    register_commands();

    s_rx_notify = xSemaphoreCreateBinaryStatic(&s_rx_notify_buf);
    configASSERT(s_rx_notify != NULL);

    /* Arm continuous RX: the ring fills from the FLEXCOM2 ISR; a persistent
     * 1-byte threshold notification wakes console_task on each inbound byte. */
    FLEXCOM2_USART_ReadCallbackRegister(rx_event_handler, 0u);
    FLEXCOM2_USART_ReadThresholdSet(CON_RX_THRESHOLD);
    (void)FLEXCOM2_USART_ReadNotificationEnable(true, true);

    (void)xTaskCreateStatic(console_task,
                            "Console",
                            CON_TASK_STACK_WORDS,
                            NULL,
                            CON_TASK_PRIORITY,
                            s_task_stack,
                            &s_task_tcb);
}
