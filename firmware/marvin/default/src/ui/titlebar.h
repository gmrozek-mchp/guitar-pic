#ifndef UI_TITLEBAR_H
#define UI_TITLEBAR_H

#include "gfx/legato/legato.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared base-view titlebar: the hamburger (nav-drawer toggle) + the GUITAR /
 * PIC / Microchip logo images, matching the dashboard/wiimotes chrome. Built
 * programmatically from static storage into `parent` (a full-screen base-view
 * root panel); call once from the screen's Setup. The hamburger toggles the
 * navigation drawer (ScreenNavigation_ToggleDrawer).
 *
 * The bar sits at (12,12) and is 1256x53, so it occupies the top ~65 px — lay the
 * screen's own content below that. Gating the parent panel's pickability
 * (LE_WIDGET_ENABLED) gates the hamburger with it. */
/* Returns the bar widget (the titlebar's own container), so a caller can gate it
 * in/out of picking later — e.g. the dashboard hides its chrome from touches while
 * the video is fullscreen. NULL if the instance pool is exhausted. */
leWidget *Titlebar_Add(leWidget *parent);

#ifdef __cplusplus
}
#endif

#endif /* UI_TITLEBAR_H */
