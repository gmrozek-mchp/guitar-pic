#ifndef UI_GFX_QR_RASTER_H
#define UI_GFX_QR_RASTER_H

#include <stdbool.h>
#include <stdint.h>

/* Rasterize a URL into an RGB565 QR tile: black modules on a white field, with the
 * spec's 4-module quiet zone inside the tile so nothing behind it has to be light.
 *
 * Geometry is fixed at compile time. The version is pinned rather than chosen per
 * string so every QR on the System Info screen is the same size on the panel, and so
 * a URL that no longer fits fails the encode instead of silently changing size —
 * qrcodegen picks the version from the payload when given a range, and a 29-module
 * QR dropped into a 33-module tile would be a layout bug found on hardware.
 *
 * Version 4 holds 62 bytes in byte mode at ECC M; the longest URL in the NODE table
 * (see screen_system.c) is 58. boostEcl then lifts the level for the shorter ones,
 * so they get more error correction at the same module count for free.
 *
 * Depends on nothing but qrcodegen and the C library — no Legato, no Harmony — so
 * tools/qr-verify can link this exact translation unit on the host and decode what it
 * produces. Pixels are only ever 0x0000 or 0xFFFF, which are byte-order and
 * channel-order agnostic; stride, orientation and inversion are what the host test
 * is actually checking. */

#define QR_RASTER_VERSION  4u
#define QR_RASTER_MODULES  (QR_RASTER_VERSION * 4u + 17u)   /* 33 */
#define QR_RASTER_QUIET    4u                               /* spec minimum */
#define QR_RASTER_SCALE    4u                               /* px per module */

/* 164 px square: (33 + 8) * 4. About 22 mm on the 10.1-inch panel, so a 0.68 mm
 * module — a phone scans it from arm's length, not across the room. */
#define QR_RASTER_W        ((QR_RASTER_MODULES + 2u * QR_RASTER_QUIET) * QR_RASTER_SCALE)
#define QR_RASTER_H        QR_RASTER_W
#define QR_RASTER_BYTES    ((size_t)QR_RASTER_W * QR_RASTER_H * 2u)

/* Write a QR_RASTER_W x QR_RASTER_H tile for `text` at `px`, `stride_px` uint16s per
 * row. False (leaving `px` untouched) if text is NULL, empty, or does not fit the
 * pinned version. */
bool QrRaster_Render(const char *text, uint16_t *px, unsigned stride_px);

#endif
