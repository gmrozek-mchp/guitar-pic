#ifndef MARVIN_STORAGE_H
#define MARVIN_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

/* SD-card storage: owns the FAT mount lifecycle and the bring-up diagnostics.
 *
 * Mounting is manual for now — DRV_SDMMC uses polled card detection and SYS_FS
 * automount is disabled, so the app issues SYS_FS_Mount itself. The mount
 * trigger is isolated in Storage_Mount()/Storage_Unmount() so it can later be
 * replaced by a SYS_FS mount/unmount event handler (for hotplug) without
 * touching the diagnostics, which depend only on Storage_IsMounted() and the
 * mount-point string.
 *
 * The mount path uses SYS_FS calls that block on SD I/O, so Storage_Mount and
 * every Storage_Diag* must be called from a task (after the scheduler is up),
 * not from APP_Initialize. */

void Storage_Initialize(void);

/* Mount the SD volume. Bounded internal retry — after insertion the polling
 * driver needs a moment before SYS_FS reports the media ready. Idempotent:
 * returns true immediately if already mounted. */
bool        Storage_Mount(void);
bool        Storage_Unmount(void);
bool        Storage_IsMounted(void);
const char *Storage_MountPoint(void);

/* Output sink for the diagnostics: one already-formatted line, no newline.
 * Lets storage emit to the operator console without depending on it. */
typedef void (*storage_print_fn)(void *ctx, const char *line);

/* Bring-up diagnostics (driven by the `sd` console command). Each mounts on
 * demand if needed and reports through `out`. */
void Storage_DiagInfo (storage_print_fn out, void *ctx);
void Storage_DiagList (storage_print_fn out, void *ctx, const char *path);
void Storage_DiagBench(storage_print_fn out, void *ctx, uint32_t mbytes);

#endif
