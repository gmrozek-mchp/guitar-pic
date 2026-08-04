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
void Titlebar_Add(leWidget *parent);

#ifdef __cplusplus
}
#endif

#endif /* UI_TITLEBAR_H */
