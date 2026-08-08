#include "ui/ui_anim.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "ui/ui_manager.h"   /* UiManager_RenderLock/Unlock */

/* See ui_anim.h for why this exists rather than Legato's update() hook. */

/* Concurrent animations. Two is the realistic ceiling (one list flinging while a drawer
 * slides); four leaves room without making the scan worth optimising. */
#define UIANIM_SLOTS   4u

static struct { uianim_step_fn fn; void *ctx; } s_slot[UIANIM_SLOTS];
static volatile uint32_t s_active;

static SemaphoreHandle_t s_wake;
static StaticSemaphore_t s_wake_buf;

static StackType_t  s_stack[512];
static StaticTask_t s_tcb;

bool UiAnim_Active(void)
{
    return s_active > 0u;
}

void UiAnim_Start(uianim_step_fn fn, void *ctx)
{
    if (fn == NULL) { return; }

    bool added = false;

    /* Called from the Legato input context (a touch-up handler) while the ticker task may
     * be scanning the same table. The critical section is a few compares long. */
    taskENTER_CRITICAL();
    {
        int free_slot = -1;
        bool present  = false;

        for (uint32_t i = 0u; i < UIANIM_SLOTS; i++)
        {
            if (s_slot[i].fn == fn && s_slot[i].ctx == ctx) { present = true; break; }
            if (s_slot[i].fn == NULL && free_slot < 0)      { free_slot = (int)i; }
        }

        if (!present && free_slot >= 0)
        {
            s_slot[free_slot].fn  = fn;
            s_slot[free_slot].ctx = ctx;
            s_active++;
            added = true;
        }
    }
    taskEXIT_CRITICAL();

    /* Outside the critical section: the give may unblock the ticker immediately. */
    if (added && s_wake != NULL) { (void)xSemaphoreGive(s_wake); }
}

void UiAnim_Stop(uianim_step_fn fn, void *ctx)
{
    taskENTER_CRITICAL();
    for (uint32_t i = 0u; i < UIANIM_SLOTS; i++)
    {
        if (s_slot[i].fn == fn && s_slot[i].ctx == ctx)
        {
            s_slot[i].fn  = NULL;
            s_slot[i].ctx = NULL;
            if (s_active > 0u) { s_active--; }
            break;
        }
    }
    taskEXIT_CRITICAL();
}

/* One pass over the registered steps, under the render lock. Steps that report finished are
 * cleared. The lock is taken once for the whole pass rather than per step, so two
 * simultaneous animations cost one suspend/resume of the render task instead of two. */
static void step_all(void)
{
    uianim_step_fn fn[UIANIM_SLOTS];
    void          *cx[UIANIM_SLOTS];
    uint32_t       n = 0u;

    /* Snapshot, so a step that registers or deregisters something cannot invalidate the
     * table under the loop. */
    taskENTER_CRITICAL();
    for (uint32_t i = 0u; i < UIANIM_SLOTS; i++)
    {
        if (s_slot[i].fn != NULL) { fn[n] = s_slot[i].fn; cx[n] = s_slot[i].ctx; n++; }
    }
    taskEXIT_CRITICAL();

    if (n == 0u) { return; }

    UiManager_RenderLock();
    for (uint32_t i = 0u; i < n; i++)
    {
        if (!fn[i](cx[i])) { UiAnim_Stop(fn[i], cx[i]); }
    }
    UiManager_RenderUnlock();
}

static void anim_task(void *param)
{
    (void)param;

    for (;;)
    {
        /* Nothing to do: block indefinitely. This is the whole point — an idle marvin runs
         * no animation code at all, which is what re-apply patch #15 bought and what a
         * polling ticker would have given back. */
        if (s_active == 0u)
        {
            (void)xSemaphoreTake(s_wake, portMAX_DELAY);
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(UIANIM_STEP_MS));
        step_all();
    }
}

void UiAnim_Initialize(void)
{
    s_active = 0u;
    for (uint32_t i = 0u; i < UIANIM_SLOTS; i++) { s_slot[i].fn = NULL; s_slot[i].ctx = NULL; }

    /* Binary rather than counting: several Starts before the ticker runs should wake it
     * once, and it re-checks s_active on every pass anyway. */
    s_wake = xSemaphoreCreateBinaryStatic(&s_wake_buf);

    /* Priority matches the other UI helper tasks (the bus and log polls). Above them would
     * let a fling preempt telemetry for the whole of its duration. */
    (void)xTaskCreateStatic(anim_task, "UiAnim",
                            (uint32_t)(sizeof s_stack / sizeof s_stack[0]),
                            NULL, 2u, s_stack, &s_tcb);
}
