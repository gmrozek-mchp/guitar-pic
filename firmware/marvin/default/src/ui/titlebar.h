#ifndef UI_TITLEBAR_H
#define UI_TITLEBAR_H

#include <stdbool.h>

#include "gfx/legato/legato.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared base-view titlebar: the hamburger (nav-drawer toggle), the GUITAR / PIC /
 * Microchip logo images, two live metric tiles (CPU and T1S bus load, each a value plus a
 * scrolling sparkline) and the pulsing system status LED. Built programmatically from
 * static storage into `parent` (a full-screen base-view root panel); call once from the
 * screen's Setup. The hamburger toggles the navigation drawer
 * (ScreenNavigation_ToggleDrawer).
 *
 * The bar sits at (12,12) and is 1256x53, so it occupies the top ~65 px — lay the
 * screen's own content below that. Gating the parent panel's pickability
 * (LE_WIDGET_ENABLED) gates the hamburger with it. */
/* Returns the bar widget (the titlebar's own container), so a caller can gate it
 * in/out of picking later — e.g. the dashboard hides its chrome from touches while
 * the video is fullscreen. NULL if the instance pool is exhausted. */
leWidget *Titlebar_Add(leWidget *parent);

/* Start the tick that samples the metrics and animates the tiles + status LED. Call once
 * from the end of the boot sequence: the tick takes the render lock, which must not be
 * done while the boot sequence has the render tasks suspended. */
void Titlebar_Start(void);

/* Tell the titlebar which of its instances is on screen. Every screen that owns a
 * titlebar reports its own from its shown/hidden hook, and only the reported one is
 * repainted — the others sit on canvases that are not bound to a hardware layer, where a
 * repaint would cost DDR bandwidth to write pixels nobody can see. The sample history is
 * shared across instances, so whichever bar comes on screen arrives complete.
 *
 * `shown = false` is ignored unless `bar` is the instance currently reported, so an
 * outgoing screen cannot switch off the incoming one whichever order the two land in. */
void Titlebar_SetShown(leWidget *bar, bool shown);

/* Runtime A/B for the two things the tick costs the renderer, so the render load can be
 * attributed without a reflash (the `titlebar` console command drives these; read the
 * per-task share back with `health`). Both default on.
 *
 * They are separate because they damage at very different rates: the tiles repaint two
 * 158x42 cards once a second, while the pulse damages a 12x12 dot ten times a second —
 * and with LE_PREEMPTION_LEVEL 0 a Legato frame runs to completion inside one
 * LEGATO_Tasks tick, so cost tracks *frames*, not damaged pixels. Turning the pulse off
 * settles the dot at full opacity. */
void Titlebar_SetPulseEnabled(bool on);
void Titlebar_SetTilesEnabled(bool on);

typedef struct {
    bool     pulse;
    bool     tiles;
    bool     live;           /* a titlebar instance is on screen */
    uint32_t cpu_permille;
    uint32_t bus_permille;
} Titlebar_Status;

void Titlebar_GetStatus(Titlebar_Status *out);

/* Time one Legato frame, `iters` times, damaging a w*h rect, and return the mean in
 * microseconds (0 if no titlebar is on screen).
 *
 * The rect belongs to an invisible probe widget over the empty middle of the bar, so the
 * only thing painted inside it is the parent panel's background fill — a cost strictly
 * proportional to w*h. That matters: probing a metric card instead measured whatever
 * shape the sparkline happened to be that second, which moves by milliseconds between
 * runs and made the per-pixel figure meaningless. Sweep two sizes and the fixed
 * per-frame overhead is the intercept.
 *
 * Legitimate to drive the painter from here because it runs under the render lock, which
 * exists precisely to guarantee that LEGATO_Tasks is suspended and no paint is in flight. */
uint32_t Titlebar_ProbeFrameUs(unsigned iters, uint32_t w, uint32_t h);

/* Time one frame damaging a real part of the titlebar, `iters` times, mean microseconds.
 * Subtracting the contentless probe at the same rect size gives that part's drawing cost:
 *
 *   PART_DOT   12x12   the status LED's AA capsule
 *   PART_PLOT  80x28   the sparkline: antialiased polyline + end marker
 *   PART_CARD  158x42  a whole metric tile: card fill, AA corners, 3 labels, the plot
 *
 * PART_PLOT and PART_CARD are data-dependent — the polyline's cost tracks the shape of the
 * samples it happens to be showing — so treat them as an order of magnitude, not a constant. */
typedef enum {
    TITLEBAR_PART_DOT,
    TITLEBAR_PART_PLOT,
    TITLEBAR_PART_CARD,
    TITLEBAR_PART_N
} Titlebar_Part;

uint32_t Titlebar_ProbePartUs(unsigned iters, Titlebar_Part part);

#ifdef __cplusplus
}
#endif

#endif /* UI_TITLEBAR_H */
