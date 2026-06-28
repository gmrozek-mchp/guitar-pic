#ifndef MARVIN_QSPI_LAYOUT_H
#define MARVIN_QSPI_LAYOUT_H

/* Partition map for the onboard 8 MiB QSPI NOR (SST26VF064BA). Single source of
 * truth for region offsets/sizes — consumers (splash loader, settings store)
 * and the qspi_smoke dev tools all reference these. All boundaries are 4 KiB
 * sector-aligned (the erase granularity). See firmware/marvin/docs/journal.md
 * (2026-06-28) for the rationale.
 *
 *   0x000000  +-------------------------------+
 *             | Splash blob (raw framebuffer) | 4 MiB
 *   0x400000  +-------------------------------+
 *             | UI assets (icons/fonts/etc.)  | ~3.98 MiB
 *   0x7FC000  +-------------------------------+
 *             | Settings (EEPROM-emul ring)   | 16 KiB (4 sectors)
 *   0x800000  +-------------------------------+
 *
 * QSPI is reached either through the SST26 driver (DRV_SST26_*) or, for fast
 * bulk reads, by adding QSPIMEM_ADDR (0x60000000) to a region offset and
 * reading via XDMAC (see flash/qspi_smoke.c). */

#define QSPI_FLASH_SIZE        0x800000u   /* 8 MiB total */
#define QSPI_SECTOR_SIZE       0x001000u   /* 4 KiB erase block */
#define QSPI_PAGE_SIZE         0x000100u   /* 256 B program page */

/* Splash: raw, pre-decoded framebuffer streamed straight to the LCD framebuffer
 * (no decode, no filesystem). 4 MiB holds a full-panel RGBA8888 image
 * (1280x800x4 = 3.91 MiB); an RGB565 splash uses only the lower ~1.95 MiB. */
#define QSPI_SPLASH_OFFSET     0x000000u
#define QSPI_SPLASH_SIZE       0x400000u   /* 4 MiB reserved */

/* UI assets: read-mostly blobs (icons, fonts, album-art templates). Indexed by
 * compile-time offsets for now (flashed alongside firmware); a manifest sector
 * or littlefs can be carved from the front of this region later if assets need
 * to update independently of the firmware image. */
#define QSPI_ASSETS_OFFSET     0x400000u
#define QSPI_ASSETS_SIZE       0x3FC000u   /* ~3.98 MiB */

/* Settings: power-fail-safe ring-log (EEPROM-emulation). Isolated at the top so
 * its erase cycling is confined away from the read-mostly bulk. */
#define QSPI_SETTINGS_OFFSET   0x7FC000u
#define QSPI_SETTINGS_SIZE     0x004000u   /* 16 KiB = 4 sectors */

#endif /* MARVIN_QSPI_LAYOUT_H */
