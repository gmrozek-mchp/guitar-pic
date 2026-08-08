#include "log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "definitions.h"   /* DBGU_WriteByte (polled) */

/* Ceiling on how long a caller waits for the lock. Bounded so a holder that
 * wedges cannot stall every other task that logs — an unbounded wait turns one
 * stuck task into a dead system, because every logger queues behind it. */
#define LOG_LOCK_TIMEOUT_MS   250u

static volatile log_level_t s_level = LOG_LEVEL_INFO;
static SemaphoreHandle_t    s_mutex;
static StaticSemaphore_t    s_mutex_buf;
static log_sink_fn volatile s_sink;

/* Which task holds the lock and how deep it is in. Tracked here rather than read
 * back from the mutex so it works pre-scheduler and needs no
 * INCLUDE_xSemaphoreGetMutexHolder. Only ever written under the lock. */
static TaskHandle_t volatile s_holder;
static volatile uint32_t     s_depth;
static volatile uint32_t     s_nested;
static volatile uint32_t     s_lock_timeouts;

void log_init(log_level_t initial_level)
{
    s_level         = initial_level;
    s_holder        = NULL;
    s_depth         = 0u;
    s_nested        = 0u;
    s_lock_timeouts = 0u;

    /* Recursive: a LOG_* reached from inside a log call — via a helper evaluated
     * for a log argument, or anything on the printf write path — would otherwise
     * block forever on a mutex this task already holds, and every other logger
     * would pile up behind it. Recursion garbles one line instead.
     * Safe before vTaskStartScheduler; only Take/Give require the scheduler. */
    s_mutex = xSemaphoreCreateRecursiveMutexStatic(&s_mutex_buf);
}

void log_set_level(log_level_t lvl)
{
    s_level = lvl;
}

log_level_t log_get_level(void)
{
    return s_level;
}

void log_set_sink(log_sink_fn fn)
{
    s_sink = fn;
}

uint32_t log_nested_count(void)
{
    return s_nested;
}

uint32_t log_lock_timeout_count(void)
{
    return s_lock_timeouts;
}

/* Polled DBGU, so reporting a nested call cannot itself go through printf and
 * nest again. Same approach as fault/fault_report.c. */
static void dbgu_str(const char *s)
{
    while (*s != '\0') { DBGU_WriteByte((uint8_t)*s++); }
}

static void dbgu_hex32(uint32_t v)
{
    dbgu_str("0x");
    for (int i = 28; i >= 0; i -= 4)
    {
        uint32_t nib = (v >> (unsigned)i) & 0xFu;
        DBGU_WriteByte((uint8_t)(nib < 10u ? ('0' + nib) : ('a' + nib - 10u)));
    }
}

/* Breakpoint target for locating a nested LOG_* call: `break log_nested_detected`
 * in gdb yields a backtrace naming the inner call site directly. `site` is the
 * return address of the LOG_* that nested — resolve it with
 * `xc32-addr2line -e out/marvin/default.elf <site>`. */
static void __attribute__((noinline)) log_nested_detected(const void *site)
{
    const char *name = (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
                     ? pcTaskGetName(NULL) : NULL;

    dbgu_str("\r\n*** LOG NESTED task=");
    dbgu_str((name != NULL) ? name : "?");
    dbgu_str(" site=");
    dbgu_hex32((uint32_t)(uintptr_t)site);
    dbgu_str(" ***\r\n");
}

/* Breakpoint target for the first lock timeout: whoever holds the lock is wedged,
 * and `info threads` / the mutex's xMutexHolder names it. `site` resolves with
 * `xc32-addr2line -e <matching elf> <site>`. */
static void __attribute__((noinline)) log_lock_timeout_detected(const void *site)
{
    dbgu_str("\r\n*** LOG LOCK TIMEOUT site=");
    dbgu_hex32((uint32_t)(uintptr_t)site);
    dbgu_str(" - lines are unformatted until the holder releases ***\r\n");
}

static void log_emit(log_level_t lvl, const char *fmt, va_list ap, const void *site)
{
    if ((int)lvl > (int)s_level) { return; }

    /* Take the mutex only when the scheduler is running and the mutex was
     * created. Pre-scheduler we're single-threaded so interleaving isn't
     * possible; a Take with portMAX_DELAY would hang before
     * vTaskStartScheduler. */
    bool locked = false;
    if (s_mutex != NULL
        && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        /* Safe to read unlocked: only this task can have set s_holder to itself. */
        TaskHandle_t self   = xTaskGetCurrentTaskHandle();
        bool         nested = (s_holder == self) && (s_depth > 0u);

        if (xSemaphoreTakeRecursive(s_mutex, pdMS_TO_TICKS(LOG_LOCK_TIMEOUT_MS))
            == pdTRUE)
        {
            locked   = true;
            s_holder = self;
            s_depth++;

            if (nested)
            {
                s_nested++;
                log_nested_detected(site);
            }
        }
        else
        {
            /* Holder is wedged. vprintf is off limits — it is not reentrant with
             * configUSE_NEWLIB_REENTRANT 0, and the holder may be parked inside
             * it — so the format string goes out raw and the arguments are lost.
             * Keeps the message and its ordering rather than the whole system. */
            if (s_lock_timeouts++ == 0u) { log_lock_timeout_detected(site); }
            dbgu_str(fmt);
            return;
        }
    }

    /* Its own copy, taken before vprintf consumes ap: capturing must not change what
     * goes out over serial, including for a line the sink would have to truncate. */
    log_sink_fn sink = s_sink;
    if (sink != NULL)
    {
        va_list ap2;
        va_copy(ap2, ap);
        sink(lvl, fmt, ap2);
        va_end(ap2);
    }

    (void)vprintf(fmt, ap);

    if (locked)
    {
        s_depth--;
        if (s_depth == 0u) { s_holder = NULL; }
        (void)xSemaphoreGiveRecursive(s_mutex);
    }
}

void log_vprintf(log_level_t lvl, const char *fmt, va_list ap)
{
    log_emit(lvl, fmt, ap, __builtin_return_address(0));
}

void log_printf(log_level_t lvl, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_emit(lvl, fmt, ap, __builtin_return_address(0));
    va_end(ap);
}
