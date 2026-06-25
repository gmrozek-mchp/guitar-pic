#ifndef UI_COMPOSITOR_H
#define UI_COMPOSITOR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Assigns marvin-owned static framebuffers to the canvas objects Legato
 * renders the Marvin screen's two layers into, and advances the canvas state
 * machine to RUNNING. Call once from APP_Initialize (before the scheduler).
 * The Marvin screen's On-Show hook (Marvin_OnShow) then binds the canvases to
 * LCDC layers and presents the dashboard. See docs/ui_compositor.md. */
void Compositor_Initialize(void);

/* Reveal / dismiss the navigation menu (canvas 1 on OVR1). Present iteration
 * is an instant show/hide; the slide animation is a follow-on. */
void Compositor_NavOpen(void);
void Compositor_NavClose(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_COMPOSITOR_H */
