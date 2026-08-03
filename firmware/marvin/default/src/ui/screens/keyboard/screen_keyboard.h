#ifndef UI_SCREENS_KEYBOARD_SCREEN_KEYBOARD_H
#define UI_SCREENS_KEYBOARD_SCREEN_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* On-screen touch keyboard modal (Marvin layer 5 / CANVAS_KEYBOARD, a 1060x560
 * centered dialog). The whole keyboard is built programmatically here — an edit
 * buffer plus a QWERTY grid of leButtonWidgets laid out from a table, so a keypress
 * repaints only the pressed key + the text/counter labels, never the whole surface.
 * ui_manager owns show/hide (binds the canvas to a hardware layer); this module owns
 * the content, the edit buffer, and the key dispatch. */

/* Invoked when the user confirms (OK). `text` is the edited string (never NULL);
 * copy it if you need it past the call. The dialog is closed immediately after. */
typedef void (*keyboard_commit_fn)(const char *text);

/* Assign the layer-5 canvas surface. Pre-scheduler (from UiManager_Initialize). */
void ScreenKeyboard_InitSurface(void);

/* Build the dialog chrome + key grid once, into Marvin_PANEL_KEYBOARD. Called from
 * ui_manager's screen setup; leaves the dialog input-gated off (not shown). */
void ScreenKeyboard_Setup(void);

/* Seed one editing session: title text, initial buffer contents, max length (capped
 * to the internal limit), and the commit callback. Call before showing the dialog
 * (UiManager_OpenKeyboard does this). */
void ScreenKeyboard_Prepare(const char *title, const char *initial,
                            uint32_t maxlen, keyboard_commit_fn commit);

/* Gate the dialog's subtree in/out of touch picking (ui_manager toggles this on
 * open/close so the always-attached layer doesn't swallow touches while hidden). */
void ScreenKeyboard_SetInput(bool on);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREENS_KEYBOARD_SCREEN_KEYBOARD_H */
