#ifndef MARVIN_SETTINGS_H
#define MARVIN_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

/* Persistent operator/UI settings, stored in the QSPI NOR settings region
 * (flash/qspi_layout.h) as a power-fail-safe ring-log — see flash/settings.c
 * and the journal (2026-06-28). Survives with no SD card present. */
typedef struct
{
    uint16_t version;        /* SETTINGS_VERSION of this record's layout */
    uint8_t  backlight_pct;  /* 0..100 */
    uint8_t  reserved0;
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

/* Diagnostics (console `settings`). */
void Settings_Dump(void);   /* log the ring scan + current record */
bool Settings_Wipe(void);   /* erase the whole settings region -> defaults */

/* Ring stress test: wipe, perform `n` saves (default 200 — enough to wrap the
 * 64-slot ring and force sector erases), then reload from flash and verify the
 * highest-seq record matches the last write. Returns true on PASS. Destructive
 * (wipes settings). */
bool Settings_Stress(uint32_t n);

#endif /* MARVIN_SETTINGS_H */
