#include "flash/settings.h"
#include "flash/qspi_layout.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "definitions.h"
#include "log.h"

#define SETTINGS_VERSION         1u
#define SETTINGS_DEFAULT_BL      50u    /* default boot brightness % (no record yet) */

#define SETTINGS_MAGIC           0x4D565354u   /* 'M''V''S''T' */

#define SLOT_SIZE                QSPI_PAGE_SIZE                       /* 256 */
#define NSLOTS                   (QSPI_SETTINGS_SIZE / SLOT_SIZE)     /* 64 */
#define SLOTS_PER_SECTOR         (QSPI_SECTOR_SIZE / SLOT_SIZE)       /* 16 */
#define POLL_GUARD               2000000u

/* On-flash record. One per 256-byte page slot; trailing bytes stay 0xFF. crc is
 * computed over everything preceding it (magic..data). */
typedef struct
{
    uint32_t   magic;
    uint32_t   seq;
    settings_t data;
    uint32_t   crc;
} settings_record_t;

static settings_t s_cache;
static uint32_t   s_cur_seq;     /* seq of the cached record; 0 = none yet */
static int32_t    s_cur_slot;    /* slot index of the current record; -1 = empty */
static bool       s_loaded;

/* 256-byte scratch buffers, static (static-allocation discipline; not reentrant,
 * settings ops are never concurrent). */
static uint8_t s_page[SLOT_SIZE] __attribute__((aligned(4)));
static uint8_t s_scan[SLOT_SIZE] __attribute__((aligned(4)));

static void set_defaults(void)
{
    s_cache.version       = SETTINGS_VERSION;
    s_cache.backlight_pct = SETTINGS_DEFAULT_BL;
    s_cache.reserved0     = 0u;
}

/* Bitwise CRC-32 (poly 0xEDB88320). No table; records are tiny and writes rare. */
static uint32_t crc32(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0u; i < len; i++)
    {
        crc ^= p[i];
        for (uint32_t b = 0u; b < 8u; b++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

/* Bounded poll. The QSPI SST26 ops complete synchronously, so this returns on
 * the first iteration; the guard only protects against a wedged controller. No
 * yielding, so it is safe before the scheduler is running. */
static bool wait_done(DRV_HANDLE h)
{
    for (uint32_t g = 0u; g < POLL_GUARD; g++)
    {
        DRV_SST26_TRANSFER_STATUS st = DRV_SST26_TransferStatusGet(h);
        if (st == DRV_SST26_TRANSFER_COMPLETED)     { return true; }
        if (st == DRV_SST26_TRANSFER_ERROR_UNKNOWN) { return false; }
    }
    return false;
}

static bool record_valid(const settings_record_t *r)
{
    return (r->magic == SETTINGS_MAGIC) &&
           (crc32(r, offsetof(settings_record_t, crc)) == r->crc);
}

void Settings_Load(void)
{
    set_defaults();
    s_cur_seq  = 0u;
    s_cur_slot = -1;
    s_loaded   = true;

    DRV_HANDLE h = DRV_SST26_Open(DRV_SST26_INDEX, DRV_IO_INTENT_READ);
    if (h == DRV_HANDLE_INVALID)
    {
        LOG_ERROR("SETTINGS: open FAILED, using defaults\r\n");
        return;
    }

    for (uint32_t slot = 0u; slot < NSLOTS; slot++)
    {
        uint32_t addr = QSPI_SETTINGS_OFFSET + (slot * SLOT_SIZE);
        if (!DRV_SST26_Read(h, s_scan, sizeof(settings_record_t), addr) ||
            !wait_done(h))
        {
            continue;
        }
        const settings_record_t *r = (const settings_record_t *)(const void *)s_scan;
        if (record_valid(r) && ((s_cur_slot < 0) || (r->seq > s_cur_seq)))
        {
            s_cur_seq  = r->seq;
            s_cur_slot = (int32_t)slot;
            s_cache    = r->data;
        }
    }
    DRV_SST26_Close(h);

    if (s_cur_slot < 0)
    {
        LOG_INFO("SETTINGS: no valid record, defaults (backlight=%u%%)\r\n",
                 (unsigned)s_cache.backlight_pct);
    }
    else
    {
        LOG_INFO("SETTINGS: loaded seq=%lu slot=%ld (v%u backlight=%u%%)\r\n",
                 (unsigned long)s_cur_seq, (long)s_cur_slot,
                 (unsigned)s_cache.version, (unsigned)s_cache.backlight_pct);
    }
}

const settings_t *Settings_Get(void)
{
    if (!s_loaded) { Settings_Load(); }
    return &s_cache;
}

bool Settings_Save(void)
{
    if (!s_loaded) { Settings_Load(); }

    DRV_HANDLE h = DRV_SST26_Open(DRV_SST26_INDEX, DRV_IO_INTENT_READWRITE);
    if (h == DRV_HANDLE_INVALID)
    {
        LOG_ERROR("SETTINGS: save open FAILED\r\n");
        return false;
    }

    int32_t  next = (s_cur_slot < 0) ? 0 : ((s_cur_slot + 1) % (int32_t)NSLOTS);
    uint32_t addr = QSPI_SETTINGS_OFFSET + ((uint32_t)next * SLOT_SIZE);

    /* Crossing into a sector: erase it first. It is always ahead of the current
     * record (current is in the previous slot), so this never erases live data. */
    if (((uint32_t)next % SLOTS_PER_SECTOR) == 0u)
    {
        if (!DRV_SST26_SectorErase(h, addr) || !wait_done(h))
        {
            LOG_ERROR("SETTINGS: erase FAILED @0x%06lX\r\n", (unsigned long)addr);
            DRV_SST26_Close(h);
            return false;
        }
    }

    (void)memset(s_page, 0xFF, sizeof s_page);
    settings_record_t *r = (settings_record_t *)(void *)s_page;
    r->magic = SETTINGS_MAGIC;
    r->seq   = s_cur_seq + 1u;
    r->data  = s_cache;
    r->crc   = crc32(s_page, offsetof(settings_record_t, crc));

    if (!DRV_SST26_PageWrite(h, s_page, addr) || !wait_done(h))
    {
        LOG_ERROR("SETTINGS: write FAILED @0x%06lX\r\n", (unsigned long)addr);
        DRV_SST26_Close(h);
        return false;
    }
    DRV_SST26_Close(h);

    s_cur_slot = next;
    s_cur_seq  = r->seq;
    LOG_INFO("SETTINGS: saved seq=%lu slot=%ld (backlight=%u%%)\r\n",
             (unsigned long)s_cur_seq, (long)s_cur_slot, (unsigned)s_cache.backlight_pct);
    return true;
}

bool Settings_SetBacklight(uint8_t pct)
{
    if (!s_loaded) { Settings_Load(); }
    if (pct > 100u) { pct = 100u; }
    s_cache.backlight_pct = pct;
    return Settings_Save();
}

void Settings_Dump(void)
{
    DRV_HANDLE h = DRV_SST26_Open(DRV_SST26_INDEX, DRV_IO_INTENT_READ);
    if (h == DRV_HANDLE_INVALID)
    {
        LOG_ERROR("SETTINGS: dump open FAILED\r\n");
        return;
    }
    uint32_t valid = 0u;
    for (uint32_t slot = 0u; slot < NSLOTS; slot++)
    {
        uint32_t addr = QSPI_SETTINGS_OFFSET + (slot * SLOT_SIZE);
        if (!DRV_SST26_Read(h, s_scan, sizeof(settings_record_t), addr) || !wait_done(h))
        {
            continue;
        }
        const settings_record_t *r = (const settings_record_t *)(const void *)s_scan;
        if (record_valid(r))
        {
            valid++;
            LOG_INFO("SETTINGS:  slot %2lu: seq=%lu v%u bl=%u%%%s\r\n",
                     (unsigned long)slot, (unsigned long)r->seq, (unsigned)r->data.version,
                     (unsigned)r->data.backlight_pct,
                     ((int32_t)slot == s_cur_slot) ? "  <- current" : "");
        }
    }
    DRV_SST26_Close(h);
    LOG_INFO("SETTINGS: %lu/%u slots valid; current seq=%lu slot=%ld\r\n",
             (unsigned long)valid, (unsigned)NSLOTS,
             (unsigned long)s_cur_seq, (long)s_cur_slot);
}

bool Settings_Wipe(void)
{
    DRV_HANDLE h = DRV_SST26_Open(DRV_SST26_INDEX, DRV_IO_INTENT_READWRITE);
    if (h == DRV_HANDLE_INVALID)
    {
        LOG_ERROR("SETTINGS: wipe open FAILED\r\n");
        return false;
    }
    for (uint32_t a = QSPI_SETTINGS_OFFSET;
         a < (QSPI_SETTINGS_OFFSET + QSPI_SETTINGS_SIZE);
         a += QSPI_SECTOR_SIZE)
    {
        if (!DRV_SST26_SectorErase(h, a) || !wait_done(h))
        {
            LOG_ERROR("SETTINGS: wipe erase FAILED @0x%06lX\r\n", (unsigned long)a);
            DRV_SST26_Close(h);
            return false;
        }
    }
    DRV_SST26_Close(h);

    set_defaults();
    s_cur_seq  = 0u;
    s_cur_slot = -1;
    s_loaded   = true;
    LOG_INFO("SETTINGS: wiped -> defaults (backlight=%u%%)\r\n", (unsigned)s_cache.backlight_pct);
    return true;
}

bool Settings_Stress(uint32_t n)
{
    if (n == 0u) { n = 200u; }
    LOG_INFO("SETTINGS: stress %lu saves (wipes first; ring=%u slots, %u/sector)\r\n",
             (unsigned long)n, (unsigned)NSLOTS, (unsigned)SLOTS_PER_SECTOR);

    if (!Settings_Wipe()) { return false; }

    for (uint32_t i = 0u; i < n; i++)
    {
        if (!Settings_SetBacklight((uint8_t)(i % 101u)))   /* distinct, recomputable value */
        {
            LOG_ERROR("SETTINGS: stress save %lu/%lu FAILED\r\n",
                      (unsigned long)i, (unsigned long)n);
            return false;
        }
    }

    /* After a clean wipe, save k (1-based) lands in slot (k-1)%NSLOTS with seq=k. */
    uint8_t  exp_bl   = (uint8_t)((n - 1u) % 101u);
    uint32_t exp_seq  = n;
    int32_t  exp_slot = (int32_t)((n - 1u) % NSLOTS);

    bool cache_ok = (s_cache.backlight_pct == exp_bl) &&
                    (s_cur_seq == exp_seq) && (s_cur_slot == exp_slot);

    /* The real proof: drop RAM state and re-scan flash from scratch. If wrap /
     * sector-erase ever clobbered the live record or left a higher stale seq,
     * this reload disagrees with the expected last write. */
    Settings_Load();
    bool reload_ok = (s_cache.backlight_pct == exp_bl) &&
                     (s_cur_seq == exp_seq) && (s_cur_slot == exp_slot);

    bool pass = cache_ok && reload_ok;
    LOG_INFO("SETTINGS: stress %s — expect seq=%lu slot=%ld bl=%u%%; reload seq=%lu slot=%ld bl=%u%% (cache %s)\r\n",
             pass ? "PASS" : "FAIL",
             (unsigned long)exp_seq, (long)exp_slot, (unsigned)exp_bl,
             (unsigned long)s_cur_seq, (long)s_cur_slot, (unsigned)s_cache.backlight_pct,
             cache_ok ? "ok" : "MISMATCH");
    return pass;
}
