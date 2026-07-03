#include "health_monitor.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "definitions.h"
#include "log.h"

#include "storage/storage.h"
#include "video/video.h"

#define HM_TASK_STACK_WORDS  1024u
#define HM_TASK_PRIORITY     1u        /* just above idle — starved first by a spin */

#define HM_HEARTBEAT_MS      60000u    /* one heartbeat line per minute */
#define HM_STACKDUMP_MS      600000u   /* full stack table every 10 min */

#define HM_MAX_TASKS         24u       /* snapshot buffer; assert-checked at runtime */

#define HM_REL_DIR   "health"
#define HM_REL_HB    "health/heartbeat.csv"
#define HM_REL_DUMP  "health/stacks.txt"

/* heap_1 never frees, so free space only shrinks: current free == min-ever. */
#define HM_HB_HEADER \
    "timestamp,uptime_s,tick,frames,free_heap,worst_task,worst_stk_free"

static StackType_t   s_task_stack[HM_TASK_STACK_WORDS];
static StaticTask_t  s_task_tcb;

/* Guards s_tasks: the health task samples into it, and the console `health`
 * command samples into it from a different task. */
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_buf;
static TaskStatus_t      s_tasks[HM_MAX_TASKS];

/* ---- helpers ------------------------------------------------------------ */

static void build_path(char *buf, size_t n, const char *rel)
{
    (void)snprintf(buf, n, "%s/%s", Storage_MountPoint(), rel);
}

static void iso8601_utc(char *buf, size_t n)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    RTC_TimeGet(&t);
    (void)snprintf(buf, n, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                   t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                   t.tm_hour, t.tm_min, t.tm_sec);
}

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

/* ---- table report (shared by console, stack-dump file, and log) --------- */

void HealthMonitor_Report(health_print_fn out, void *ctx)
{
    if (out == NULL) { return; }

    (void)xSemaphoreTake(s_lock, portMAX_DELAY);

    uint64_t    total_rt = 0u;
    UBaseType_t n        = uxTaskGetSystemState(s_tasks, HM_MAX_TASKS, &total_rt);
    uint64_t    denom    = total_rt / 100u;   /* percent divisor; avoids *100 overflow */

    char line[96];
    out(ctx, "task             st pri  stkfree  run%");
    if (n == 0u)
    {
        out(ctx, "(task count exceeds HM_MAX_TASKS snapshot buffer)");
    }
    for (UBaseType_t i = 0u; i < n; i++)
    {
        const TaskStatus_t *t = &s_tasks[i];
        unsigned stk = (unsigned)t->usStackHighWaterMark * (unsigned)sizeof(StackType_t);
        unsigned pct = (denom > 0u) ? (unsigned)(t->ulRunTimeCounter / denom) : 0u;
        (void)snprintf(line, sizeof(line), "%-16s %c %3u  %6u  %3u",
                       (t->pcTaskName != NULL) ? t->pcTaskName : "?",
                       state_char(t->eCurrentState),
                       (unsigned)t->uxCurrentPriority, stk, pct);
        out(ctx, line);
    }
    (void)snprintf(line, sizeof(line), "heap: %u free",
                   (unsigned)xPortGetFreeHeapSize());
    out(ctx, line);

    (void)xSemaphoreGive(s_lock);
}

static void log_line(void *ctx, const char *line)
{
    (void)ctx;
    LOG_INFO("HM| %s\r\n", line);
}

/* ---- heartbeat ---------------------------------------------------------- */

static void heartbeat_write(void)
{
    /* Worst (smallest) stack headroom across all tasks + its owner. */
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
    uint64_t    total_rt   = 0u;
    UBaseType_t n          = uxTaskGetSystemState(s_tasks, HM_MAX_TASKS, &total_rt);
    unsigned    worst      = 0u;
    const char *worst_name = "?";
    for (UBaseType_t i = 0u; i < n; i++)
    {
        unsigned stk = (unsigned)s_tasks[i].usStackHighWaterMark * (unsigned)sizeof(StackType_t);
        if (i == 0u || stk < worst)
        {
            worst      = stk;
            worst_name = (s_tasks[i].pcTaskName != NULL) ? s_tasks[i].pcTaskName : "?";
        }
    }
    (void)xSemaphoreGive(s_lock);

    Video_FrameInfo vi;
    Video_GetFrameInfo(&vi);

    uint32_t tick = (uint32_t)xTaskGetTickCount();
    uint32_t up_s = tick / (uint32_t)configTICK_RATE_HZ;

    char ts[24];
    iso8601_utc(ts, sizeof(ts));

    unsigned heap = (unsigned)xPortGetFreeHeapSize();

    /* Mirror to the log first — survives even if SD is the wedged path. */
    LOG_INFO("HM: %s up=%lus frames=%lu heap=%u worststk=%s:%u\r\n",
             ts, (unsigned long)up_s, (unsigned long)vi.frame_count,
             heap, worst_name, worst);

    if (!Storage_Mount()) { return; }

    char dir[64];
    build_path(dir, sizeof(dir), HM_REL_DIR);
    (void)SYS_FS_DirectoryMake(dir);   /* harmless if it already exists */

    char path[80];
    build_path(path, sizeof(path), HM_REL_HB);

    bool need_header = true;
    SYS_FS_FSTAT st;
    if (SYS_FS_FileStat(path, &st) == SYS_FS_RES_SUCCESS && st.fsize > 0u)
    {
        need_header = false;
    }

    SYS_FS_HANDLE h = SYS_FS_FileOpen(path, SYS_FS_FILE_OPEN_APPEND);
    if (h == SYS_FS_HANDLE_INVALID)
    {
        /* FF_FS_MAX_FILES=1: a collision with another writer just skips this
         * beat. Quiet at DEBUG — the log line above already recorded the beat. */
        LOG_DEBUG("HM: heartbeat open failed (fs err %d)\r\n", (int)SYS_FS_Error());
        return;
    }

    char line[160];
    int  len;
    if (need_header)
    {
        len = snprintf(line, sizeof(line), "%s\n", HM_HB_HEADER);
        if (len > 0 && (size_t)len < sizeof(line))
        {
            (void)SYS_FS_FileWrite(h, line, (size_t)len);
        }
    }
    len = snprintf(line, sizeof(line), "%s,%lu,%lu,%lu,%u,%s,%u\n",
                   ts, (unsigned long)up_s, (unsigned long)tick,
                   (unsigned long)vi.frame_count, heap,
                   worst_name, worst);
    if (len > 0 && (size_t)len < sizeof(line))
    {
        (void)SYS_FS_FileWrite(h, line, (size_t)len);
    }
    (void)SYS_FS_FileClose(h);
}

/* ---- stack dump --------------------------------------------------------- */

static void file_line(void *ctx, const char *line)
{
    SYS_FS_HANDLE h = *(const SYS_FS_HANDLE *)ctx;
    (void)SYS_FS_FileWrite(h, line, strlen(line));
    (void)SYS_FS_FileWrite(h, "\n", 1u);
}

static void stackdump_write(void)
{
    /* The table always goes to the log; the file is best-effort on top. */
    if (!Storage_Mount())
    {
        HealthMonitor_Report(log_line, NULL);
        return;
    }

    char dir[64];
    build_path(dir, sizeof(dir), HM_REL_DIR);
    (void)SYS_FS_DirectoryMake(dir);

    char path[80];
    build_path(path, sizeof(path), HM_REL_DUMP);

    SYS_FS_HANDLE h = SYS_FS_FileOpen(path, SYS_FS_FILE_OPEN_WRITE);  /* truncate/create */
    if (h == SYS_FS_HANDLE_INVALID)
    {
        LOG_DEBUG("HM: stackdump open failed (fs err %d)\r\n", (int)SYS_FS_Error());
        HealthMonitor_Report(log_line, NULL);
        return;
    }

    char ts[24];
    iso8601_utc(ts, sizeof(ts));
    file_line(&h, ts);
    HealthMonitor_Report(file_line, &h);
    (void)SYS_FS_FileClose(h);

    HealthMonitor_Report(log_line, NULL);
}

/* ---- task --------------------------------------------------------------- */

static void health_task(void *param)
{
    (void)param;

    LOG_INFO("HM: health monitor started (hb=%us dump=%us)\r\n",
             (unsigned)(HM_HEARTBEAT_MS / 1000u), (unsigned)(HM_STACKDUMP_MS / 1000u));

    /* Baseline dump at startup so the file exists and the fresh-boot stack
     * headroom is on record for comparison. */
    stackdump_write();

    uint32_t   since_dump = 0u;
    TickType_t last       = xTaskGetTickCount();
    for (;;)
    {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(HM_HEARTBEAT_MS));

        heartbeat_write();

        since_dump += HM_HEARTBEAT_MS;
        if (since_dump >= HM_STACKDUMP_MS)
        {
            since_dump = 0u;
            stackdump_write();
        }
    }
}

void HealthMonitor_Initialize(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    configASSERT(s_lock != NULL);

    (void)xTaskCreateStatic(health_task,
                            "Health",
                            HM_TASK_STACK_WORDS,
                            NULL,
                            HM_TASK_PRIORITY,
                            s_task_stack,
                            &s_task_tcb);
}
