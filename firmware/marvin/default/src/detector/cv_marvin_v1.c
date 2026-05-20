#include "cv_marvin_v1.h"
#include "detector.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "log.h"
#include "video/video.h"

#define CV_TASK_STACK_WORDS    1024u
#define CV_TASK_PRIORITY       2u
#define CV_FRAME_QUEUE_DEPTH   1u

/* Capture frames are RGB888 packed, 3 bytes per pixel. Confirmed at runtime
 * via Video_FrameInfo.bytes_per_pixel; this constant is the value the
 * detector is built against. */
#define CV_BYTES_PER_PIXEL     3u

/* Microseconds per FreeRTOS tick — pre-evaluates to a constant at config
 * tick rate 1000. Used to project xTaskGetTickCount() into timestamp_us. */
#define CV_US_PER_TICK         (1000000u / configTICK_RATE_HZ)

static void cv_marvin_v1_task(void *param)
{
    (void)param;

    QueueHandle_t frames = xQueueCreate(CV_FRAME_QUEUE_DEPTH,
                                        sizeof(Video_FrameInfo));
    configASSERT(frames != NULL);
    Video_SubscribeFrames(frames);

    QueueHandle_t bus = Detector_BusQueue();
    configASSERT(bus != NULL);

    LOG_INFO("CV: cv_marvin_v1 started (stub publisher)\r\n");

    for (;;)
    {
        Video_FrameInfo frame;
        if (xQueueReceive(frames, &frame, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        /* Always drain the frame queue, even when disabled, so frames don't
         * back up in the video module. Skip publishing when disabled. */
        if (!Detector_IsEnabled(DETECTOR_CV_MARVIN_V1))
        {
            continue;
        }

        detector_state_t state;
        memset(&state, 0, sizeof(state));

        /* frame_count is monotonic since the most-recent ISC_Capture_Start —
         * it resets when the bridge re-arms. A persistent frame_epoch that
         * survives ISC restart is needed for recording (§4.6.4) and lands
         * with M5; until then this is a near-good-enough stand-in. */
        state.frame_epoch  = frame.frame_count;
        state.timestamp_us = (uint64_t)xTaskGetTickCount() * CV_US_PER_TICK;
        state.detector_id  = (uint8_t)DETECTOR_CV_MARVIN_V1;

        /* Real detection lands here. M1 publishes all-zero fret state to
         * validate task scheduling, queue depth, and frame-epoch flow. */
        (void)xQueueSend(bus, &state, 0);
    }
}

void CvMarvinV1_Initialize(void)
{
    (void)xTaskCreate(cv_marvin_v1_task,
                      "CvMarvinV1",
                      CV_TASK_STACK_WORDS,
                      NULL,
                      CV_TASK_PRIORITY,
                      NULL);
}
