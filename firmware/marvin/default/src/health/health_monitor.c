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

#define HM_HEARTBEAT_MS      60000u    /* one heartbeat per 60 s */
#define HM_STACKDUMP_MS      600000u   /* full stack table every 10 min */

/* Compile-time switch for the on-card log (health/heartbeat.csv + stacks.txt).
 * Off by default now the freeze investigation is done — the supervisor's RUNAWAY
 * line (logged at ERROR, always on) is the live watch, and the periodic
 * telemetry drops to DEBUG. Build with -DHM_SD_LOG_ENABLED=1 to restore the
 * on-card crash-timing record. */
#ifndef HM_SD_LOG_ENABLED
#define HM_SD_LOG_ENABLED    0
#endif

#define HM_MAX_TASKS         28u       /* snapshot buffer; sized above the task count */

/* Supervisor: runs above every app task (they top out at prio 5) so it keeps
 * running even when a lower task spins, and reports the runaway by name. */
#define HM_SUP_STACK_WORDS   512u
#define HM_SUP_PRIORITY      6u
#define HM_SUP_SAMPLE_MS     1000u     /* runaway-detection sample period */
#define HM_SUP_SUMMARY_N     10u       /* emit a CPU summary every N samples */
#define HM_SUP_IDLE_PCT      3u        /* idle below this + one hot task = runaway */
#define HM_SUP_HOT_PCT       90u

#define HM_REL_DIR   "health"
#define HM_REL_HB    "health/heartbeat.csv"
#define HM_REL_DUMP  "health/stacks.txt"

/* heap_1 never frees, so free space only shrinks: current free == min-ever. */
#define HM_HB_HEADER \
    "timestamp,uptime_s,tick,frames,free_heap,worst_task,worst_stk_free"

static StackType_t   s_task_stack[HM_TASK_STACK_WORDS];
static StaticTask_t  s_task_tcb;

static StackType_t   s_sup_stack[HM_SUP_STACK_WORDS];
static StaticTask_t  s_sup_tcb;

/* Guards s_tasks: the health task samples into it, and the console `health`
 * command samples into it from a different task. The supervisor deliberately
 * does NOT touch s_tasks / s_lock — if the health task were the one being
 * starved while holding the mutex, a supervisor that waited on it would
 * deadlock exactly when it needs to report. It owns a separate lock-free
 * buffer instead (safe: it is the highest-priority task, so no lower task can
 * preempt its snapshot). */
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_buf;
static TaskStatus_t      s_tasks[HM_MAX_TASKS];

/* HealthMonitor_Report shows a recent-window CPU%, not cumulative-since-boot
 * (which buries current load under startup): it snapshots, waits this window,
 * snapshots again, and diffs. s_rpt_* holds the first snapshot's runtime keyed
 * by handle for the diff. */
#define HM_REPORT_WINDOW_MS  500u
static TaskHandle_t      s_rpt_h[HM_MAX_TASKS];
static uint64_t          s_rpt_rt[HM_MAX_TASKS];

/* Set once the boot sequence has revealed the UI. Until then both tasks stay
 * idle — HM card I/O + task-list walks in the capture/reveal window wedge boot. */
static volatile bool     s_ready;

static TaskStatus_t      s_sup_tasks[HM_MAX_TASKS];
/* Previous per-task runtime counters, keyed by handle, for interval deltas. */
static TaskHandle_t      s_prev_handle[HM_MAX_TASKS];
static uint64_t          s_prev_rt[HM_MAX_TASKS];
static UBaseType_t       s_prev_n;
static uint64_t          s_prev_total;

/* Latest sample's non-idle share, permille. Published for the UI (see the header). */
static volatile uint32_t s_cpu_permille;

/* ---- helpers ------------------------------------------------------------ */

#if HM_SD_LOG_ENABLED
static void build_path(char *buf, size_t n, const char *rel)
{
    (void)snprintf(buf, n, "%s/%s", Storage_MountPoint(), rel);
}
#endif

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

static uint64_t rpt_prev_rt(TaskHandle_t h, UBaseType_t prev_n)
{
    for (UBaseType_t i = 0u; i < prev_n; i++)
    {
        if (s_rpt_h[i] == h) { return s_rpt_rt[i]; }
    }
    return 0u;   /* task not in the first snapshot → count all its time as new */
}

void HealthMonitor_Report(health_print_fn out, void *ctx)
{
    if (out == NULL) { return; }

    (void)xSemaphoreTake(s_lock, portMAX_DELAY);

    /* First snapshot; retain per-task runtime, then wait the window. */
    uint64_t    t0 = 0u;
    UBaseType_t prev_n = uxTaskGetSystemState(s_tasks, HM_MAX_TASKS, &t0);
    for (UBaseType_t i = 0u; i < prev_n; i++)
    {
        s_rpt_h[i]  = s_tasks[i].xHandle;
        s_rpt_rt[i] = s_tasks[i].ulRunTimeCounter;
    }

    vTaskDelay(pdMS_TO_TICKS(HM_REPORT_WINDOW_MS));

    /* Second snapshot; report each task's share of the window's runtime. */
    uint64_t    t1    = 0u;
    UBaseType_t n     = uxTaskGetSystemState(s_tasks, HM_MAX_TASKS, &t1);
    uint64_t    dtot  = (t1 > t0) ? (t1 - t0) : 0u;
    uint64_t    denom = dtot / 100u;   /* percent divisor; avoids *100 overflow */

    char line[96];
    out(ctx, "task             st pri  stkfree  cpu%");
    if (n == 0u)
    {
        out(ctx, "(task count exceeds HM_MAX_TASKS snapshot buffer)");
    }
    for (UBaseType_t i = 0u; i < n; i++)
    {
        const TaskStatus_t *t = &s_tasks[i];
        unsigned stk  = (unsigned)t->usStackHighWaterMark * (unsigned)sizeof(StackType_t);
        uint64_t prev = rpt_prev_rt(t->xHandle, prev_n);
        uint64_t d    = (t->ulRunTimeCounter >= prev) ? (t->ulRunTimeCounter - prev) : 0u;
        unsigned pct  = (denom > 0u) ? (unsigned)(d / denom) : 0u;
        (void)snprintf(line, sizeof(line), "%-16s %c %3u  %6u  %3u",
                       (t->pcTaskName != NULL) ? t->pcTaskName : "?",
                       state_char(t->eCurrentState),
                       (unsigned)t->uxCurrentPriority, stk, pct);
        out(ctx, line);
    }
    (void)snprintf(line, sizeof(line), "heap: %u free  (cpu%% over %ums)",
                   (unsigned)xPortGetFreeHeapSize(), (unsigned)HM_REPORT_WINDOW_MS);
    out(ctx, line);

    (void)xSemaphoreGive(s_lock);
}

static void log_line(void *ctx, const char *line)
{
    (void)ctx;
    LOG_DEBUG("HM| %s\r\n", line);
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

    /* Periodic telemetry — DEBUG (silent at the default INFO level). */
    LOG_DEBUG("HM: %s up=%lus frames=%lu heap=%u worststk=%s:%u\r\n",
              ts, (unsigned long)up_s, (unsigned long)vi.frame_count,
              heap, worst_name, worst);

#if HM_SD_LOG_ENABLED
    /* Only touch the card once the boot task has mounted it. The monitor never
     * mounts itself — a second mounter races the boot mount, and FatFs is built
     * non-reentrant (FF_FS_REENTRANT=0). */
    if (!Storage_IsMounted()) { return; }

    char dir[64];
    build_path(dir, sizeof(dir), HM_REL_DIR);
    (void)SYS_FS_DirectoryMake(dir);   /* harmless if it already exists */

    char path[80];
    build_path(path, sizeof(path), HM_REL_HB);

    bool need_header = true;
    SYS_FS_FSTAT st;
    memset(&st, 0, sizeof st);   /* lfname MUST be NULL — else FATFS_stat writes
                                  * through it (a garbage stack value → data abort) */
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
#endif /* HM_SD_LOG_ENABLED */
}

/* ---- stack dump --------------------------------------------------------- */

#if HM_SD_LOG_ENABLED
static void file_line(void *ctx, const char *line)
{
    SYS_FS_HANDLE h = *(const SYS_FS_HANDLE *)ctx;
    (void)SYS_FS_FileWrite(h, line, strlen(line));
    (void)SYS_FS_FileWrite(h, "\n", 1u);
}
#endif

static void stackdump_write(void)
{
#if HM_SD_LOG_ENABLED
    /* Best-effort on-card copy, once the boot task has mounted (never self-mounts). */
    if (Storage_IsMounted())
    {
        char dir[64];
        build_path(dir, sizeof(dir), HM_REL_DIR);
        (void)SYS_FS_DirectoryMake(dir);

        char path[80];
        build_path(path, sizeof(path), HM_REL_DUMP);

        SYS_FS_HANDLE h = SYS_FS_FileOpen(path, SYS_FS_FILE_OPEN_WRITE);  /* truncate/create */
        if (h != SYS_FS_HANDLE_INVALID)
        {
            char ts[24];
            iso8601_utc(ts, sizeof(ts));
            file_line(&h, ts);
            HealthMonitor_Report(file_line, &h);
            (void)SYS_FS_FileClose(h);
        }
        else
        {
            LOG_DEBUG("HM: stackdump open failed (fs err %d)\r\n", (int)SYS_FS_Error());
        }
    }
#endif
    /* The table always goes to the log (DEBUG). */
    HealthMonitor_Report(log_line, NULL);
}

/* ---- supervisor (runaway-task detector) --------------------------------- */

static uint64_t prev_rt_for(TaskHandle_t h)
{
    for (UBaseType_t i = 0u; i < s_prev_n; i++)
    {
        if (s_prev_handle[i] == h) { return s_prev_rt[i]; }
    }
    return 0u;   /* task not seen last sample → treat as no prior time */
}

/* One sample: diff each task's runtime against the previous sample and report
 * a starving CPU (one task hot while idle ~= 0). Runs at the top priority, so
 * it keeps sampling even while a lower task spins. */
static void supervisor_sample(uint32_t seq)
{
    uint64_t    total = 0u;
    UBaseType_t n     = uxTaskGetSystemState(s_sup_tasks, HM_MAX_TASKS, &total);
    if (n == 0u) { return; }   /* HM_MAX_TASKS too small — nothing usable */

    TaskHandle_t idle = xTaskGetIdleTaskHandle();

    uint64_t    total_delta = (total >= s_prev_total) ? (total - s_prev_total) : 0u;
    uint64_t    idle_delta  = 0u;
    uint64_t    hot_delta   = 0u;
    uint64_t    sum_delta   = 0u;
    const char *hot_name    = "?";

    for (UBaseType_t i = 0u; i < n; i++)
    {
        const TaskStatus_t *t = &s_sup_tasks[i];
        uint64_t prev = prev_rt_for(t->xHandle);
        uint64_t d    = (t->ulRunTimeCounter >= prev) ? (t->ulRunTimeCounter - prev) : 0u;
        sum_delta += d;
        if (t->xHandle == idle) { idle_delta = d; }
        else if (d > hot_delta)
        {
            hot_delta = d;
            hot_name  = (t->pcTaskName != NULL) ? t->pcTaskName : "?";
        }
    }

    bool have_prev = (s_prev_n > 0u);

    /* Roll the snapshot into the prev table for the next interval. */
    for (UBaseType_t i = 0u; i < n; i++)
    {
        s_prev_handle[i] = s_sup_tasks[i].xHandle;
        s_prev_rt[i]     = s_sup_tasks[i].ulRunTimeCounter;
    }
    s_prev_n     = n;
    s_prev_total = total;

    if (!have_prev || total_delta == 0u) { return; }

    uint32_t idle_pm = (uint32_t)((idle_delta * 1000u) / total_delta);
    s_cpu_permille = (idle_pm < 1000u) ? (1000u - idle_pm) : 0u;

    unsigned idle_pct = (unsigned)((idle_delta * 100u) / total_delta);
    unsigned hot_pct  = (unsigned)((hot_delta  * 100u) / total_delta);
    unsigned isr_pct  = (sum_delta < total_delta)
                        ? (unsigned)(((total_delta - sum_delta) * 100u) / total_delta) : 0u;

    /* Require two consecutive starved samples before crying wolf — a single
     * CPU-bound frame shouldn't trip it; a real spin persists. */
    static unsigned starve_streak;
    bool starved = (idle_pct <= HM_SUP_IDLE_PCT) && (hot_pct >= HM_SUP_HOT_PCT);
    starve_streak = starved ? (starve_streak + 1u) : 0u;

    if (starve_streak >= 2u)
    {
        LOG_ERROR("HM: RUNAWAY '%s' cpu=%u%% idle=%u%% isr=%u%%\r\n",
                  hot_name, hot_pct, idle_pct, isr_pct);
    }
    else if (!starved && (seq % HM_SUP_SUMMARY_N) == 0u)
    {
        LOG_DEBUG("SUP: idle=%u%% top=%s:%u%% isr=%u%%\r\n",
                  idle_pct, hot_name, hot_pct, isr_pct);
    }
}

static void supervisor_task(void *param)
{
    (void)param;

    LOG_DEBUG("HM: supervisor started (prio %u, %ums)\r\n",
              (unsigned)HM_SUP_PRIORITY, (unsigned)HM_SUP_SAMPLE_MS);

    uint32_t   seq  = 0u;
    TickType_t last = xTaskGetTickCount();
    for (;;)
    {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(HM_SUP_SAMPLE_MS));
        if (!s_ready) { continue; }   /* stay off the fragile boot window */
        supervisor_sample(seq++);
    }
}

/* ---- task --------------------------------------------------------------- */

static void health_task(void *param)
{
    (void)param;

    LOG_DEBUG("HM: health monitor started (hb=%us dump=%us)\r\n",
              (unsigned)(HM_HEARTBEAT_MS / 1000u), (unsigned)(HM_STACKDUMP_MS / 1000u));

    /* Hold off until the UI is revealed — see s_ready. */
    while (!s_ready) { vTaskDelay(pdMS_TO_TICKS(100)); }

    /* Baseline dump once armed so the file exists and the just-booted stack
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

    (void)xTaskCreateStatic(supervisor_task,
                            "Supervisor",
                            HM_SUP_STACK_WORDS,
                            NULL,
                            HM_SUP_PRIORITY,
                            s_sup_stack,
                            &s_sup_tcb);
}

void HealthMonitor_NotifyReady(void)
{
    s_ready = true;
}

uint32_t HealthMonitor_CpuPermille(void)
{
    return s_cpu_permille;
}
