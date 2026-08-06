#ifndef UI_SCREEN_SONG_SELECT_H
#define UI_SCREEN_SONG_SELECT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Song/mode-select dialog — a Marvin layer-screen (renders into CANVAS_SONGSEL).
 * It owns its canvas surface; ui_manager binds that canvas to a hardware layer at
 * display time. The album-art strip is a SEPARATE layer-screen on top of it
 * (CANVAS_ALBUM_ART) whose widgets this module also builds — see the .c header.
 *   ScreenSongSelect_InitSurface — assign the canvas pixel buffer; call once pre-scheduler,
 *                         before the canvas state machine is RUNNING.
 *   ScreenSongSelect_Setup       — build the dialog's widget tree into the empty MGS
 *                         panels and position the canvas window (centered); call once
 *                         after screenShow_Marvin.
 *   ScreenSongSelect_RoundCorners — re-cut the dialog's rounded corners against the base
 *                         view behind it; ui_manager calls this on open. */
void ScreenSongSelect_InitSurface(void);
void ScreenSongSelect_Setup(void);
void ScreenSongSelect_RoundCorners(void);

/* Absolute screen position of the album-art rect inside the dialog. The art strip is a
 * separate layer-screen that has to sit exactly there, and this is the dialog's layout
 * to know — screen_album_art.c asks rather than repeating the arithmetic. */
void ScreenSongSelect_ArtOrigin(int *x, int *y);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_SONG_SELECT_H */
