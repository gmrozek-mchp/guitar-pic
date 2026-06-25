#ifndef UI_NAV_H
#define UI_NAV_H

#ifdef __cplusplus
extern "C" {
#endif

/* Navigation drawer — a resident overlay on its own LCDC layer (OVR1). Owned by
 * ui/nav, orchestrated by ui_manager:
 *   Nav_InitSurface — assign the nav canvas buffer; call before the canvas state
 *                     machine is RUNNING / before the first render.
 *   Nav_OnShow      — bind the layer + wire the nav buttons; call from the Marvin
 *                     screen OnShow hook.
 * Open/close is driven internally by the drawer's hamburger and entry events. */
void Nav_InitSurface(void);
void Nav_OnShow(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_NAV_H */
