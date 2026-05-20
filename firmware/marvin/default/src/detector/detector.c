#include "detector.h"
#include "cv_marvin_v1.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "log.h"

/* Sized for ~one detector × one frame-time of slack. With cv_marvin_v1
 * publishing at 60 Hz and the stub consumer draining every tick, depth 8
 * absorbs a few ms of consumer stall without dropping records. Bump when
 * adding more producers or a slower consumer. */
#define DETECTOR_BUS_DEPTH       8u

#define DRAIN_TASK_STACK_WORDS   512u
#define DRAIN_TASK_PRIORITY      1u
#define DRAIN_LOG_INTERVAL_MS    1000u

static QueueHandle_t   s_bus_queue;
static StaticQueue_t   s_bus_queue_buf;
static uint8_t         s_bus_queue_storage[DETECTOR_BUS_DEPTH * sizeof(detector_state_t)];

static StackType_t     s_drain_stack[DRAIN_TASK_STACK_WORDS];
static StaticTask_t    s_drain_tcb;

static volatile uint32_t s_enabled_mask;
static volatile uint8_t  s_active_id = (uint8_t)DETECTOR_CV_MARVIN_V1;

static void drain_task(void *param)
{
    (void)param;

    TickType_t window_start = xTaskGetTickCount();
    uint32_t   window_count = 0;

    for (;;)
    {
        detector_state_t state;
        if (xQueueReceive(s_bus_queue, &state, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }
        window_count++;

        TickType_t now = xTaskGetTickCount();
        if ((uint32_t)(now - window_start) >= pdMS_TO_TICKS(DRAIN_LOG_INTERVAL_MS))
        {
            LOG_INFO("DETECTOR: bus drain %lu records/s (last epoch=%lu id=%u)\r\n",
                     (unsigned long)window_count,
                     (unsigned long)state.frame_epoch,
                     (unsigned)state.detector_id);
            window_start = now;
            window_count = 0;
        }
    }
}

void Detector_Initialize(void)
{
    s_bus_queue = xQueueCreateStatic(DETECTOR_BUS_DEPTH,
                                     sizeof(detector_state_t),
                                     s_bus_queue_storage,
                                     &s_bus_queue_buf);
    configASSERT(s_bus_queue != NULL);

    CvMarvinV1_Initialize();

    (void)xTaskCreateStatic(drain_task,
                            "DetectorDrain",
                            DRAIN_TASK_STACK_WORDS,
                            NULL,
                            DRAIN_TASK_PRIORITY,
                            s_drain_stack,
                            &s_drain_tcb);
}

QueueHandle_t Detector_BusQueue(void)
{
    return s_bus_queue;
}

void Detector_Enable(detector_id_t id)
{
    taskENTER_CRITICAL();
    s_enabled_mask |= (1u << (uint32_t)id);
    taskEXIT_CRITICAL();
}

void Detector_Disable(detector_id_t id)
{
    taskENTER_CRITICAL();
    s_enabled_mask &= ~(1u << (uint32_t)id);
    taskEXIT_CRITICAL();
}

bool Detector_IsEnabled(detector_id_t id)
{
    return (s_enabled_mask & (1u << (uint32_t)id)) != 0u;
}

void Detector_SetActive(detector_id_t id)
{
    s_active_id = (uint8_t)id;
}

detector_id_t Detector_GetActive(void)
{
    return (detector_id_t)s_active_id;
}
