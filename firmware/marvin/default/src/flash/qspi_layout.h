#ifndef MARVIN_QSPI_LAYOUT_H
#define MARVIN_QSPI_LAYOUT_H

/* Partition map for the onboard 8 MiB QSPI NOR (SST26VF064BA). Single source of
 * truth for region offsets/sizes — consumers (splash loader, settings store)
 * and the qspi_smoke dev tools all reference these. All boundaries are 64 KiB
 * aligned. See firmware/marvin/docs/journal.md (2026-08-06) for the rationale.
 *
 *   0x000000  +-------------------------------+
 *             | Reserved (non-uniform blocks) | 64 KiB
 *   0x010000  +-------------------------------+
 *             | Splash blob (raw framebuffer) | 4,128,768 B
 *   0x400000  +-------------------------------+
 *             | UI assets (icons/fonts/etc.)  | 4,128,768 B
 *   0x7F0000  +-------------------------------+
 *             | Reserved (non-uniform blocks) | 48 KiB
 *   0x7FC000  +-------------------------------+
 *             | Settings (EEPROM-emul ring)   | 16 KiB (4 sectors)
 *   0x800000  +-------------------------------+
 *
 * The two reserved bands are the part's non-uniform Block-Erase regions: the
 * bottom 64 KiB is four 8 KiB blocks plus one 32 KiB block, and the top 64 KiB
 * mirrors it. Block-Erase (D8h) covers 8, 32 or 64 KiB *depending on address*,
 * so a host tool that assumes uniform 64 KiB blocks — u-boot `sf erase` does —
 * silently under-erases there, and the following write then lands on un-erased
 * NOR. Splash and assets therefore live entirely inside the 64 KiB-uniform
 * middle, which is the only window openocd/program-qspi.sh may touch. Firmware
 * reaches the reserved bands and the settings ring with the uniform 4 KiB
 * Sector-Erase (20h), which has no such address dependence.
 *
 * QSPI is reached either through the SST26 driver (DRV_SST26_*) or, for fast
 * bulk reads, by adding QSPIMEM_ADDR (0x60000000) to a region offset and
 * reading via XDMAC (see flash/qspi_smoke.c). */

#define QSPI_FLASH_SIZE        0x800000u   /* 8 MiB total */
#define QSPI_SECTOR_SIZE       0x001000u   /* 4 KiB erase block */
#define QSPI_PAGE_SIZE         0x000100u   /* 256 B program page */
#define QSPI_BLOCK_SIZE        0x010000u   /* 64 KiB — uniform only mid-array */

/* Window a host `sf`-style flasher may erase/write: 64 KiB-uniform blocks only.
 * openocd/program-qspi.sh mirrors these two bounds and refuses to step outside
 * them; everything beyond is firmware-only. */
#define QSPI_HOST_WINDOW_OFFSET 0x010000u
#define QSPI_HOST_WINDOW_LIMIT  0x7F0000u  /* exclusive end */

/* The non-uniform Block-Erase bands, held out of the host window. Unallocated;
 * any future use must erase them via 4 KiB sectors from firmware. */
#define QSPI_RESERVED_LO_OFFSET 0x000000u
#define QSPI_RESERVED_LO_SIZE   0x010000u  /* 4x 8 KiB + 1x 32 KiB block */
#define QSPI_RESERVED_HI_OFFSET 0x7F0000u
#define QSPI_RESERVED_HI_SIZE   0x00C000u  /* 1x 32 KiB + 2x 8 KiB block */

/* Splash: raw, pre-decoded framebuffer streamed straight to the LCD framebuffer
 * (no decode, no filesystem). The reservation holds a full-panel RGBA8888 image
 * (1280x800x4 = 4,096,000 B) with 32 KiB spare; an RGB565 splash uses half. */
#define QSPI_SPLASH_OFFSET     0x010000u
#define QSPI_SPLASH_SIZE       0x3F0000u   /* 4,128,768 B */

/* UI assets: read-mostly blobs (icons, fonts, album-art templates). Indexed by
 * compile-time offsets for now (flashed alongside firmware); a manifest sector
 * or littlefs can be carved from the front of this region later if assets need
 * to update independently of the firmware image. */
#define QSPI_ASSETS_OFFSET     0x400000u
#define QSPI_ASSETS_SIZE       0x3F0000u   /* 4,128,768 B */

/* Settings: power-fail-safe ring-log (EEPROM-emulation). Isolated at the top so
 * its erase cycling is confined away from the read-mostly bulk. */
#define QSPI_SETTINGS_OFFSET   0x7FC000u
#define QSPI_SETTINGS_SIZE     0x004000u   /* 16 KiB = 4 sectors */

#endif /* MARVIN_QSPI_LAYOUT_H */
