#ifndef HEALTH_NOCACHE_GUARD_H
#define HEALTH_NOCACHE_GUARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Corruption detector for the non-cached region.
 *
 * `.region_nocache` holds the LCDC/2D-engine framebuffers, the ISC capture pool,
 * Legato's renderer scratch — and, at its very base, the UDPHS driver's endpoint
 * objects. Nothing bounds-checks any of it, and the USB state being the first thing
 * in the region means a stray write near the base silently destroys USB while the
 * rest of the system carries on. That failure presents as "the device stops talking
 * over CDC but the UART console is fine", which is indistinguishable from a dozen
 * other faults and cost a very long debugging session to pin down once.
 *
 * This places known patterns in the region and re-checks them, so the next
 * occurrence is reported as memory corruption immediately instead of being chased
 * through its symptoms. Detection only — it cannot prevent the write.
 *
 * Placement within the region is decided by link order and cannot be relied on, so
 * NocacheGuard_Initialize logs each block's address. Compare those against the map
 * to know what a given block actually neighbours; a block adjacent to the region
 * base is the one that matters for USB. */

#define NOCACHE_GUARD_BLOCKS  4u

void NocacheGuard_Initialize(void);

/* Re-check every block. Returns false on the first mismatch found, latching the
 * failure for NocacheGuard_GetState. Cheap enough to call from a periodic task. */
bool NocacheGuard_Check(void);

/* trips = how many checks have failed since boot; block/offset/expected/found
 * describe the first mismatch seen. Any out pointer may be NULL. */
void NocacheGuard_GetState(uint32_t *trips, uint32_t *block, uint32_t *offset,
                           uint32_t *expected, uint32_t *found);

/* Base address of a guard block, for correlating with the linker map. */
const void *NocacheGuard_BlockAddr(uint32_t block);

#ifdef __cplusplus
}
#endif

#endif /* HEALTH_NOCACHE_GUARD_H */
