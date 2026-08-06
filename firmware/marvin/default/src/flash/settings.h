#ifndef MARVIN_SETTINGS_H
#define MARVIN_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

/* Persistent operator/UI settings, stored in the QSPI NOR settings region
 * (flash/qspi_layout.h) as a power-fail-safe ring-log — see flash/settings.c
 * and the journal (2026-06-28). Survives with no SD card present. */
/* Boot-time profile: how long each of the splash progress bar's *work* stages took on
 * the last boot, so the next boot's bar is marked from measurement instead of guesswork.
 * The bar's final stage is slack (it waits out the minimum splash hold), so it is derived
 * rather than stored — see ui/screens/splash/splash_progress.h. */
#define SETTINGS_BOOT_STAGES  4u

typedef struct
{
    uint16_t version;        /* SETTINGS_VERSION of this record's layout */
    uint8_t  backlight_pct;  /* 0..100 */
    uint8_t  reserved0;
    uint16_t boot_stage_ms[SETTINGS_BOOT_STAGES];   /* all zero = not calibrated yet */
} settings_t;

/* Scan the QSPI ring and populate the RAM cache, or fall back to compiled
 * defaults if no valid record exists. Idempotent; safe to call before or after
 * the scheduler starts. */
void Settings_Load(void);

/* Current cached settings (loads on first use if not yet loaded). */
const settings_t *Settings_Get(void);

/* Append the current cache to the ring as a new record. Power-fail safe.
 * Returns false on a flash error. */
bool Settings_Save(void);

/* Clamp pct to 0..100, update the cache, and persist. */
bool Settings_SetBacklight(uint8_t pct);

/* Record the per-stage boot profile the splash progress bar marks itself from, and
 * persist. Each entry saturates at UINT16_MAX. See ui/screens/splash/splash_progress.h. */
bool Settings_SetBootStages(const uint32_t *stage_ms);

/* Diagnostics (console `settings`). */
void Settings_Dump(void);   /* log the ring scan + current record */
bool Settings_Wipe(void);   /* erase the whole settings region -> defaults */

/* Ring stress test: wipe, perform `n` saves (default 200 — enough to wrap the
 * 64-slot ring and force sector erases), then reload from flash and verify the
 * highest-seq record matches the last write. Returns true on PASS. Destructive
 * (wipes settings). */
bool Settings_Stress(uint32_t n);

#endif /* MARVIN_SETTINGS_H */
