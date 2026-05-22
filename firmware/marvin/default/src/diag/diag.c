#include "diag.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "definitions.h"
#include "log.h"

#define DIAG_TASK_STACK_WORDS    1024u
#define DIAG_TASK_PRIORITY       1u

#define DIAG_PERIOD_MS           10000u
#define DIAG_FIRST_DELAY_MS      2000u

/* Sized above the current 16-task footprint (9 MCC + 7 marvin) with
 * headroom for future tasks. uxTaskGetSystemState writes only as many
 * entries as it has tasks to fill. */
#define DIAG_MAX_TASKS           24u

static StackType_t  s_task_stack[DIAG_TASK_STACK_WORDS];
static StaticTask_t s_task_tcb;
static TaskStatus_t s_status[DIAG_MAX_TASKS];

static char state_char(eTaskState s)
{
    switch (s)
    {
        case eRunning:   return 'X';
        case eReady:     return 'R';
        case eBlocked:   return 'B';
        case eSuspended: return 'S';
        case eDeleted:   return 'D';
        default:         return '?';
    }
}

static void emit_static_header(void)
{
    LOG_INFO("\r\n=== diag: build ===\r\n"
             "  CPU=%u Hz  tick=%u Hz  max_pri=%u\r\n"
             "  rt_counter=SYS_TIME (%u Hz)  total_heap=%u  free_now=%u\r\n",
             (unsigned)configCPU_CLOCK_HZ,
             (unsigned)configTICK_RATE_HZ,
             (unsigned)configMAX_PRIORITIES,
             (unsigned)SYS_TIME_FrequencyGet(),
             (unsigned)configTOTAL_HEAP_SIZE,
             (unsigned)xPortGetFreeHeapSize());
}

static void emit_task_dump(void)
{
    configRUN_TIME_COUNTER_TYPE total = 0;
    UBaseType_t n = uxTaskGetSystemState(s_status, DIAG_MAX_TASKS, &total);

    /* SYS_TIME drives the run-time counter at full chip-timer rate; show
     * accumulated CPU time in ms for readability. ticks_per_ms is computed
     * once at startup, but freq is constant so a local recompute is fine. */
    uint64_t ticks_per_ms = (uint64_t)SYS_TIME_FrequencyGet() / 1000u;
    if (ticks_per_ms == 0u) { ticks_per_ms = 1u; }

    LOG_INFO("\r\n=== diag: tasks (n=%u total_cpu_ms=%lu) ===\r\n"
             "  %-16s St Pri  HWM   CPU(ms)  Pct\r\n",
             (unsigned)n,
             (unsigned long)(total / ticks_per_ms),
             "Name");

    configRUN_TIME_COUNTER_TYPE slice = total / 1000u;
    if (slice == 0u) { slice = 1u; }

    for (UBaseType_t i = 0u; i < n; i++)
    {
        unsigned pct10 = (unsigned)(s_status[i].ulRunTimeCounter / slice);
        unsigned long cpu_ms =
            (unsigned long)(s_status[i].ulRunTimeCounter / ticks_per_ms);
        LOG_INFO("  %-16s %c  %2u %5u %9lu  %3u.%01u%%\r\n",
                 s_status[i].pcTaskName,
                 state_char(s_status[i].eCurrentState),
                 (unsigned)s_status[i].uxCurrentPriority,
                 (unsigned)s_status[i].usStackHighWaterMark,
                 cpu_ms,
                 pct10 / 10u, pct10 % 10u);
    }
}

static void diag_task(void *param)
{
    (void)param;

    emit_static_header();

    vTaskDelay(pdMS_TO_TICKS(DIAG_FIRST_DELAY_MS));

    for (;;)
    {
        emit_task_dump();
        vTaskDelay(pdMS_TO_TICKS(DIAG_PERIOD_MS));
    }
}

void Diag_Initialize(void)
{
    (void)xTaskCreateStatic(diag_task,
                            "Diag",
                            DIAG_TASK_STACK_WORDS,
                            NULL,
                            DIAG_TASK_PRIORITY,
                            s_task_stack,
                            &s_task_tcb);
}
