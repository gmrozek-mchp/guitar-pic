#ifndef MARVIN_NODE_ART_H
#define MARVIN_NODE_ART_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/legato/image/legato_image.h"   /* leImage */

/* Board photos for the system-info screen: decodes one photo per node from
 * <card>/system/nodes/<name>.png once at boot into static, fixed-size DDR slots.
 * Runtime access is then an O(1) leImage* — no card I/O, no decode, no allocation
 * when a node card is tapped.
 *
 * One slot geometry, NODE_ART_W x NODE_ART_H RGB565, matching the photo column in
 * screen_system.c and the RGB565 BASE layer it is drawn on, so the blit is a native
 * copy with no runtime colour conversion. Files must be exactly that size and PNG (a
 * runtime JPEG decode lands as noise here — see decode_one); tools/node-photos
 * prepares them.
 *
 * Photos live on the card rather than in the design, so swapping one needs no
 * rebuild and no MGS Generate. A missing, oversized, wrong-size or corrupt file
 * leaves its slot empty; the lookup returns NULL and the screen shows a blank frame
 * — the loader never fails boot. */

#define NODE_ART_W  288u
#define NODE_ART_H  620u

void NodeArt_Initialize(void);   /* state only; no I/O (call before scheduler) */

/* Mount the card and decode every node photo. Idempotent after the first pass.
 * Returns the number decoded. Must run from a task (blocks on SD I/O) and after
 * Legato's image decoders are up — i.e. during the boot/splash sequence. */
int  NodeArt_LoadAll(void);
bool NodeArt_IsLoaded(void);

/* Decoded photo for a node's T1S PLCA id (see net/t1s/t1s_link.c), or NULL if
 * absent/failed. The pointer is stable for the life of the cache. */
const leImage *NodeArt_Photo(uint8_t node_id);

int  NodeArt_Count(void);

#endif
