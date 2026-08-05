#include "health/nocache_guard.h"

#include "log.h"

#define GUARD_WORDS  32u   /* 128 B per block — trivial to re-check at task rate */

/* Pattern is derived from the word's own address, so a corrupting write is very
 * unlikely to reproduce it by accident, and the mismatch reveals whether the
 * offending data looks like pixels, a pointer, or a length. */
#define GUARD_WORD(addr)  (0xC0DEA5A5u ^ (uint32_t)(uintptr_t)(addr))

#define GUARD_NOCACHE  __attribute__((section(".region_nocache"), aligned (32)))

static uint32_t GUARD_NOCACHE s_guard[NOCACHE_GUARD_BLOCKS][GUARD_WORDS];

static uint32_t s_trips;
static bool     s_latched;
static uint32_t s_bad_block;
static uint32_t s_bad_offset;
static uint32_t s_bad_expected;
static uint32_t s_bad_found;

void NocacheGuard_Initialize(void)
{
    for (uint32_t b = 0u; b < NOCACHE_GUARD_BLOCKS; b++)
    {
        for (uint32_t i = 0u; i < GUARD_WORDS; i++)
        {
            s_guard[b][i] = GUARD_WORD(&s_guard[b][i]);
        }
    }

    s_trips   = 0u;
    s_latched = false;

    /* Log the placement: link order decides it, so this is the only way to know
     * which block is near the region base (and therefore near the USB endpoint
     * objects). Cross-reference with the .map. */
    for (uint32_t b = 0u; b < NOCACHE_GUARD_BLOCKS; b++)
    {
        LOG_INFO("GUARD: nocache block %lu at %p (%lu B)\r\n",
                 (unsigned long)b, (const void *)&s_guard[b][0],
                 (unsigned long)(GUARD_WORDS * sizeof(uint32_t)));
    }
}

bool NocacheGuard_Check(void)
{
    for (uint32_t b = 0u; b < NOCACHE_GUARD_BLOCKS; b++)
    {
        for (uint32_t i = 0u; i < GUARD_WORDS; i++)
        {
            uint32_t want = GUARD_WORD(&s_guard[b][i]);
            uint32_t got  = s_guard[b][i];
            if (got == want) { continue; }

            s_trips++;

            if (!s_latched)
            {
                s_latched      = true;
                s_bad_block    = b;
                s_bad_offset   = i * (uint32_t)sizeof(uint32_t);
                s_bad_expected = want;
                s_bad_found    = got;

                LOG_WARN("GUARD: nocache CORRUPTED at %p (block %lu +%lu): "
                         "expected %08lX found %08lX\r\n",
                         (const void *)&s_guard[b][i],
                         (unsigned long)b, (unsigned long)s_bad_offset,
                         (unsigned long)want, (unsigned long)got);
                LOG_WARN("GUARD: a stray write is loose in .region_nocache — if USB "
                         "goes quiet, suspect this, not the CDC stack\r\n");
            }
            return false;
        }
    }
    return true;
}

void NocacheGuard_GetState(uint32_t *trips, uint32_t *block, uint32_t *offset,
                           uint32_t *expected, uint32_t *found)
{
    if (trips    != NULL) { *trips    = s_trips; }
    if (block    != NULL) { *block    = s_bad_block; }
    if (offset   != NULL) { *offset   = s_bad_offset; }
    if (expected != NULL) { *expected = s_bad_expected; }
    if (found    != NULL) { *found    = s_bad_found; }
}

const void *NocacheGuard_BlockAddr(uint32_t block)
{
    return (block < NOCACHE_GUARD_BLOCKS) ? (const void *)&s_guard[block][0] : NULL;
}
