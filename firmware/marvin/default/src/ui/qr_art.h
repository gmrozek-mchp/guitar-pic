#ifndef UI_QR_ART_H
#define UI_QR_ART_H

#include <stdbool.h>

#include "gfx/legato/image/legato_image.h"   /* leImage */

/* QR codes for the system-info screen: encodes one tile per distinct URL into static,
 * fixed-size DDR slots. Runtime access is then an O(1) leImage* — nothing is encoded
 * or allocated when a node card is tapped.
 *
 * Unlike the board photos and album covers, nothing is read from the card and nothing
 * is pre-rendered by a tool: a QR is a pure function of its URL, so the URL itself is
 * the asset and lives beside the part number in the NODE table (screen_system.c). That
 * is the whole reason this is generated rather than baked — there is no image to keep
 * in step with the table, and no MGS Generate in the loop.
 *
 * Slots are RGB565 to match the BASE canvas the tile is drawn into, so the blit is a
 * native 2D-engine copy, the same as the small album-art tier.
 *
 * Encoding needs no I/O and no scheduler, so it can run at screen-setup time. A URL
 * that fails to encode (too long for the pinned QR version — see qr_raster.h) returns
 * NULL and the caller hides its QR card; nothing fails boot. */

void QrArt_Initialize(void);   /* state only (call before any Get) */

/* The tile for `url`, encoding it on first request and returning the cached leImage
 * on later ones. Repeat URLs share a slot — four nodes carry the same MCU. NULL if
 * url is NULL/empty, does not fit the pinned version, or the cache is full. */
const leImage *QrArt_Get(const char *url);

#endif
