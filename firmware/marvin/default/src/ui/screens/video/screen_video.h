#ifndef UI_SCREENS_VIDEO_SCREEN_VIDEO_H
#define UI_SCREENS_VIDEO_SCREEN_VIDEO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Windowed live-video rect on the panel (HEO layer). Single source of truth for
 * the compositor's default video placement; the fullscreen rect is the whole
 * panel (BASE_W x BASE_H). Tap the video to toggle between the two. */
#define SCREEN_VIDEO_WIN_X   280u
#define SCREEN_VIDEO_WIN_Y    77u
#define SCREEN_VIDEO_WIN_W   720u
#define SCREEN_VIDEO_WIN_H   480u

/* Wire the dashboard tap handler that toggles the live video between the windowed
 * rect and fullscreen. Call once after the dashboard panel is built. */
void ScreenVideo_Setup(void);

/* Put the video back in its windowed rect (leaving fullscreen if active) and
 * restore dashboard input. Called at reveal and whenever the song-select dialog
 * closes, so the video returns to a known windowed state. */
void ScreenVideo_ShowWindowed(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREENS_VIDEO_SCREEN_VIDEO_H */
