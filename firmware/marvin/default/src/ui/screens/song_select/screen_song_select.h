#ifndef UI_SCREEN_SONG_SELECT_H
#define UI_SCREEN_SONG_SELECT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Song/mode-select dialog — a Marvin layer-screen (renders into CANVAS_SONGSEL).
 * It owns its canvas surface; ui_manager binds that canvas to a hardware layer at
 * display time.
 *   SongSel_InitSurface — assign the canvas pixel buffer; call once pre-scheduler,
 *                         before the canvas state machine is RUNNING.
 *   SongSel_Setup       — position the canvas window (centered) and wire content +
 *                         events on the widgets MGS already built; call once after
 *                         screenInit_Marvin. */
void SongSel_InitSurface(void);
void SongSel_Setup(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_SONG_SELECT_H */
