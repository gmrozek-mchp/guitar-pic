#ifndef UI_NODE_ART_H
#define UI_NODE_ART_H

#include <stdbool.h>
#include <stdint.h>

/* Board photos for the system-info screen: decodes one photo per node from
 * <card>/system/nodes/<name>.png once at boot into static, fixed-size DDR slots.
 * Runtime access is then an O(1) pixel pointer — no card I/O, no decode, no
 * allocation when a node card is tapped.
 *
 * One slot geometry, NODE_ART_W x NODE_ART_H RGBA8888, matching the photo column in
 * screen_system.c and the OVR1 hardware layer the slot is scanned out on directly
 * (UiManager_NodePhotoShow) rather than drawn into a canvas. 32bpp is the point: the
 * BASE canvas is RGB565, which quantizes a photo to 65k colours, and the LCDC can
 * scan a full-colour overlay for the cost of the layer's own read bandwidth.
 *
 * Files must be exactly slot-sized and PNG (a runtime JPEG decode lands as noise
 * here — see decode_one); tools/node-photos prepares them, including the 1px rounded
 * frame, which is baked into the asset because nothing draws over this layer.
 *
 * Photos live on the card rather than in the design, so swapping one needs no
 * rebuild and no MGS Generate. A missing, oversized, wrong-size or corrupt file
 * leaves its slot empty; the lookup returns NULL and the screen keeps its own empty
 * frame — the loader never fails boot. */

#define NODE_ART_W  288u
#define NODE_ART_H  620u

void NodeArt_Initialize(void);   /* state only; no I/O (call before scheduler) */

/* Decode progress, for a caller that wants to show it (the boot splash): the photo count
 * as the pass starts, then after every photo. */
typedef void (*node_art_progress_fn)(uint32_t done, uint32_t total);
void NodeArt_SetProgressCallback(node_art_progress_fn fn);

/* Mount the card and decode every node photo. Idempotent after the first pass.
 * Returns the number decoded. Must run from a task (blocks on SD I/O) and after
 * Legato's image decoders are up — i.e. during the boot/splash sequence. */
int  NodeArt_LoadAll(void);
bool NodeArt_IsLoaded(void);

/* Decoded RGBA8888 pixels for a node's T1S PLCA id (see net/t1s/t1s_link.c), or NULL
 * if absent/failed. The pointer is stable for the life of the cache and is handed
 * straight to the LCDC as a layer base address. */
const void *NodeArt_Pixels(uint8_t node_id);

int  NodeArt_Count(void);

#endif
