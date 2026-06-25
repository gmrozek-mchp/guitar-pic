#ifndef UI_UI_MANAGER_H
#define UI_UI_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Top-level UI orchestrator. Owns the canvas surface pool and LCDC layer
 * mapping, screen startup (string table + screenInit/Show — the MGS screen
 * state machine is disabled; see docs/ui_compositor.md), and the BASE/dashboard
 * layer. Per-panel modules (ui/nav, future dialogs) own their own widgets and
 * interactions and are hosted here. Call once from APP_Initialize, before the
 * scheduler and after Legato_Initialize. */
void UiManager_Initialize(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_UI_MANAGER_H */
