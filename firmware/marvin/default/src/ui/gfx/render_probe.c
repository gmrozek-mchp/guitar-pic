#include "ui/gfx/render_probe.h"

#include "FreeRTOS.h"
#include "task.h"

#include "definitions.h"                          /* SYS_TIME_* */
#include "ui/ui_manager.h"                        /* RenderLock / RenderUnlock */
#include "gfx/legato/renderer/legato_renderer.h"  /* leRenderer_Paint */

/* Total time the probe may spend inside the render lock, across all iterations. Each
 * iteration holds it for one whole paint, and a paint can be very slow — the wiimotes tilt
 * gauge measured 203 ms — so a fixed iteration count froze the UI for seconds. Budgeting
 * the total instead keeps the averaging for cheap widgets and degrades to a single
 * measurement for expensive ones, which is exactly where averaging matters least. */
#define PROBE_BUDGET_US  250000u

uint32_t RenderProbe_WidgetUs(leWidget *target, unsigned iters)
{
    uint32_t hz = SYS_TIME_FrequencyGet();
    uint64_t ticks = 0u;
    unsigned done  = 0u;

    if (target == NULL || iters == 0u || hz == 0u) { return 0u; }

    for (unsigned i = 0u; i < iters; i++)
    {
        UiManager_RenderLock();          /* renderer idle and suspended from here */

        uint64_t t0;

        target->fn->invalidate(target);

        t0 = SYS_TIME_Counter64Get();
        leRenderer_Paint();              /* one whole frame, synchronously */
        ticks += SYS_TIME_Counter64Get() - t0;

        UiManager_RenderUnlock();

        done++;

        /* Yield between iterations so this can never look like a runaway to the health
         * supervisor, and so the renderer is genuinely idle at each lock. */
        vTaskDelay(1);

        if (((ticks * 1000000u) / hz) >= PROBE_BUDGET_US) { break; }
    }

    return (uint32_t)((ticks * 1000000u) / ((uint64_t)hz * done));
}
