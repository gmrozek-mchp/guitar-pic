#ifndef MARVIN_LOG_H
#define MARVIN_LOG_H

#include <stdarg.h>
#include <stdint.h>

/* Lightweight logging shim. Goes through libc printf → xc32_monitor →
 * DBGU. Adds a runtime severity filter and FreeRTOS-aware locking.
 *
 * Pre-scheduler calls work too — the mutex is taken only when the
 * scheduler is running, so log_printf is safe to call from
 * APP_Initialize / SYS_Initialize.
 *
 * The lock is recursive, so a LOG_* reached from inside another one on the
 * same task interleaves a line rather than deadlocking. That nesting is a
 * defect in the caller, so it is reported to DBGU with the offending
 * address and counted (log_nested_count).
 *
 * Acquiring the lock is also bounded: no logger can be stalled indefinitely by
 * a task that wedges while holding it. A line that times out is written raw
 * (format string, no argument substitution) and counted — see
 * log_lock_timeout_count. Logging therefore cannot deadlock the system.
 *
 * Do NOT call from an ISR — use a different mechanism for ISR-side
 * diagnostics (queue to a task, or atomic counter polled by a task).
 */

typedef enum
{
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN  = 1,
    LOG_LEVEL_INFO  = 2,
    LOG_LEVEL_DEBUG = 3,
} log_level_t;

/* Set up the FreeRTOS mutex. Safe to call before vTaskStartScheduler.
 * Pass the initial filter level. */
void log_init(log_level_t initial_level);

void        log_set_level(log_level_t lvl);
log_level_t log_get_level(void);

/* Number of nested LOG_* entries seen since boot. Non-zero means some caller
 * logs from inside a log call; the DBGU marker names the address. */
uint32_t log_nested_count(void);

/* Number of lines that gave up waiting for the lock. Non-zero means a task
 * wedged while holding it; those lines went out unformatted. */
uint32_t log_lock_timeout_count(void);

/* Tap every line that passes the severity filter, in addition to writing it to DBGU.
 * Used by log_ring.c to keep the last N lines for the operator UI's activity log.
 *
 * The sink is handed the format string and its own va_list copy, so it may format the
 * line itself; the DBGU output is unaffected either way. It runs on the calling task
 * with log.c's lock held, so it must not block, must not log, and must not take a lock
 * that a logger could be holding. Lines that time out waiting for the lock go out
 * unformatted and are NOT offered to the sink — their arguments are already lost.
 *
 * Set before or after log_init; NULL disables capture. */
typedef void (*log_sink_fn)(log_level_t lvl, const char *fmt, va_list ap);

void log_set_sink(log_sink_fn fn);

void log_vprintf(log_level_t lvl, const char *fmt, va_list ap);
void log_printf(log_level_t lvl, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Convenience macros — match a typical embedded log API. */
#define LOG_ERROR(...) log_printf(LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_WARN(...)  log_printf(LOG_LEVEL_WARN,  __VA_ARGS__)
#define LOG_INFO(...)  log_printf(LOG_LEVEL_INFO,  __VA_ARGS__)
#define LOG_DEBUG(...) log_printf(LOG_LEVEL_DEBUG, __VA_ARGS__)

#endif
