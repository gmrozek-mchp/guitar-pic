#ifndef UI_LOADER_H
#define UI_LOADER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Boot loader — a one-shot task that runs the startup sequence the splash
 * fronts: mount the SD card, read the splash image onto the (already-shown)
 * splash screen, light the backlight, run asset pre-load (album art, …), then
 * hand BASE from the splash to the dashboard and bring the camera up. Create
 * from APP_Initialize (pre-scheduler); the task self-deletes after the handoff.
 *
 * Why a task and not APP_Initialize: SD mount + FatFs + JPEG decode all block
 * on I/O and need the scheduler running, whereas APP_Initialize runs before it. */
void Loader_Start(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_LOADER_H */
