#include "detector.h"
#include "cv_marvin_v1.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Sized for ~one detector × one frame-time of slack at 60 Hz; absorbs
 * a few ms of consumer stall without dropping records. Bump when adding
 * more producers or a slower consumer. */
#define DETECTOR_BUS_DEPTH       8u

static QueueHandle_t   s_bus_queue;
static StaticQueue_t   s_bus_queue_buf;
static uint8_t         s_bus_queue_storage[DETECTOR_BUS_DEPTH * sizeof(detector_state_t)];

static volatile uint32_t s_enabled_mask;
static volatile uint8_t  s_active_id = (uint8_t)DETECTOR_CV_MARVIN_V1;

void Detector_Initialize(void)
{
    s_bus_queue = xQueueCreateStatic(DETECTOR_BUS_DEPTH,
                                     sizeof(detector_state_t),
                                     s_bus_queue_storage,
                                     &s_bus_queue_buf);
    configASSERT(s_bus_queue != NULL);

    CvMarvinV1_Initialize();
}

QueueHandle_t Detector_BusQueue(void)
{
    return s_bus_queue;
}

void Detector_Publish(const detector_state_t *state)
{
    if (state->detector_id != s_active_id)
    {
        return;
    }
    (void)xQueueSend(s_bus_queue, state, 0);
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
    /* Drop any records the previous source left queued so the consumer
     * doesn't act on a frame or two of stale detector state after a switch. */
    if (s_bus_queue != NULL)
    {
        (void)xQueueReset(s_bus_queue);
    }
}

detector_id_t Detector_GetActive(void)
{
    return (detector_id_t)s_active_id;
}
