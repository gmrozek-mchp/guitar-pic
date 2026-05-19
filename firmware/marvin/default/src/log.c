#include "log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

static volatile log_level_t s_level = LOG_LEVEL_INFO;
static SemaphoreHandle_t    s_mutex;

void log_init(log_level_t initial_level)
{
    s_level = initial_level;
    /* xSemaphoreCreateMutex is safe before vTaskStartScheduler — it
     * allocates from the FreeRTOS heap; only Take/Give require the
     * scheduler to be running. */
    s_mutex = xSemaphoreCreateMutex();
}

void log_set_level(log_level_t lvl)
{
    s_level = lvl;
}

log_level_t log_get_level(void)
{
    return s_level;
}

void log_vprintf(log_level_t lvl, const char *fmt, va_list ap)
{
    if ((int)lvl > (int)s_level) { return; }

    /* Take the mutex only when the scheduler is running and the mutex
     * was created. Pre-scheduler we're single-threaded so interleaving
     * isn't possible; xSemaphoreTake with portMAX_DELAY would hang
     * before vTaskStartScheduler. */
    bool locked = false;
    if (s_mutex != NULL
        && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
        {
            locked = true;
        }
    }

    (void)vprintf(fmt, ap);

    if (locked)
    {
        (void)xSemaphoreGive(s_mutex);
    }
}

void log_printf(log_level_t lvl, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_vprintf(lvl, fmt, ap);
    va_end(ap);
}
