#ifndef MARVIN_LOG_H
#define MARVIN_LOG_H

#include <stdarg.h>

/* Lightweight logging shim. Goes through libc printf → xc32_monitor →
 * DBGU. Adds a runtime severity filter and FreeRTOS-aware locking.
 *
 * Pre-scheduler calls work too — the mutex is taken only when the
 * scheduler is running, so log_printf is safe to call from
 * APP_Initialize / SYS_Initialize.
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

void log_vprintf(log_level_t lvl, const char *fmt, va_list ap);
void log_printf(log_level_t lvl, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Convenience macros — match a typical embedded log API. */
#define LOG_ERROR(...) log_printf(LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_WARN(...)  log_printf(LOG_LEVEL_WARN,  __VA_ARGS__)
#define LOG_INFO(...)  log_printf(LOG_LEVEL_INFO,  __VA_ARGS__)
#define LOG_DEBUG(...) log_printf(LOG_LEVEL_DEBUG, __VA_ARGS__)

#endif
