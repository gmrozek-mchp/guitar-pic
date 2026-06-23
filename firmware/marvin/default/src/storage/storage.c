#include "storage.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "definitions.h"
#include "log.h"

#define STG_DEV          "/dev/mmcblka1"
#define STG_MOUNT        "/mnt/marvin"
#define STG_TESTFILE     STG_MOUNT "/sdtest.bin"

#define STG_MOUNT_TRIES  50u
#define STG_MOUNT_WAIT_MS 20u

#define STG_CHUNK        (16u * 1024u)   /* I/O unit for the throughput test */
#define STG_BENCH_MAX_MB 64u

#define STG_SECTOR_BYTES 512u            /* SYS_FS sector size for capacity math */

static bool s_mounted = false;

/* Scratch for the bench write/read. Static so it stays off the caller's
 * (console) task stack. Cache-line aligned so disk_write takes the multi-block
 * fast path: the FatFs diskio bounces a cache-unaligned write buffer through a
 * single 512 B sector buffer (~10x slower), while reads stay multi-block even
 * when unaligned — which is why an unaligned buffer is write-slow but read-fast.
 * Any buffer handed to SYS_FS_FileWrite for throughput wants this alignment. */
static uint8_t s_chunk[STG_CHUNK] __ALIGNED(CACHE_LINE_SIZE);

/* ---- helpers ------------------------------------------------------------ */

static void diag_printf(storage_print_fn out, void *ctx, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out(ctx, buf);
}

/* Throughput in whole MB/s ×10 (one decimal), integer math. */
static uint32_t mbps_x10(uint32_t bytes, uint32_t ms)
{
    if (ms == 0u) { return 0u; }
    return (uint32_t)(((uint64_t)bytes * 1000u * 10u) / ((uint64_t)1024u * 1024u * ms));
}

/* Deterministic pattern as a function of absolute byte offset, so a read can
 * verify without retaining what was written. */
static void fill_chunk(uint8_t *b, uint32_t chunk_index)
{
    uint32_t base = chunk_index * STG_CHUNK;
    for (uint32_t i = 0u; i < STG_CHUNK; i++)
    {
        uint32_t off = base + i;
        b[i] = (uint8_t)(off ^ (off >> 8));
    }
}

static bool verify_chunk(const uint8_t *b, uint32_t chunk_index)
{
    uint32_t base = chunk_index * STG_CHUNK;
    for (uint32_t i = 0u; i < STG_CHUNK; i++)
    {
        uint32_t off = base + i;
        if (b[i] != (uint8_t)(off ^ (off >> 8))) { return false; }
    }
    return true;
}

/* ---- mount lifecycle ---------------------------------------------------- */

void Storage_Initialize(void)
{
    s_mounted = false;
    /* No mount here: APP_Initialize runs before the scheduler, so the SDMMC
     * task hasn't analyzed the card yet and SYS_FS calls would block/fail.
     * Mounting happens from a task (the `sd` console command today; a SYS_FS
     * event handler once hotplug is wired). */
}

bool Storage_Mount(void)
{
    if (s_mounted) { return true; }

    for (uint32_t i = 0u; i < STG_MOUNT_TRIES; i++)
    {
        if (SYS_FS_Mount(STG_DEV, STG_MOUNT, FAT, 0u, NULL) == SYS_FS_RES_SUCCESS)
        {
            s_mounted = true;
            LOG_INFO("STG: mounted %s at %s\r\n", STG_DEV, STG_MOUNT);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(STG_MOUNT_WAIT_MS));
    }

    LOG_WARN("STG: mount failed (fs err %d)\r\n", (int)SYS_FS_Error());
    return false;
}

bool Storage_Unmount(void)
{
    if (!s_mounted) { return true; }

    if (SYS_FS_Unmount(STG_MOUNT) == SYS_FS_RES_SUCCESS)
    {
        s_mounted = false;
        LOG_INFO("STG: unmounted %s\r\n", STG_MOUNT);
        return true;
    }
    return false;
}

bool Storage_IsMounted(void)      { return s_mounted; }
const char *Storage_MountPoint(void) { return STG_MOUNT; }

/* ---- diagnostics -------------------------------------------------------- */

void Storage_DiagInfo(storage_print_fn out, void *ctx)
{
    if (!Storage_Mount())
    {
        diag_printf(out, ctx, "not mounted (no card?)");
        return;
    }

    diag_printf(out, ctx, "mount:  %s", STG_MOUNT);

    char     label[32] = { 0 };
    uint32_t serial = 0u;
    if (SYS_FS_DriveLabelGet(STG_MOUNT, label, &serial) == SYS_FS_RES_SUCCESS)
    {
        diag_printf(out, ctx, "label:  '%s'  serial %08lX",
                    label, (unsigned long)serial);
    }

    uint32_t total = 0u, freeSec = 0u;
    if (SYS_FS_DriveSectorGet(STG_MOUNT, &total, &freeSec) == SYS_FS_RES_SUCCESS)
    {
        uint32_t per_mb = (1024u * 1024u) / STG_SECTOR_BYTES;   /* sectors/MB */
        diag_printf(out, ctx, "size:   %lu MB total, %lu MB free",
                    (unsigned long)(total / per_mb),
                    (unsigned long)(freeSec / per_mb));
    }
}

void Storage_DiagList(storage_print_fn out, void *ctx, const char *path)
{
    if (!Storage_Mount())
    {
        diag_printf(out, ctx, "not mounted");
        return;
    }

    const char *dir = (path != NULL && path[0] != '\0') ? path : STG_MOUNT;

    SYS_FS_HANDLE h = SYS_FS_DirOpen(dir);
    if (h == SYS_FS_HANDLE_INVALID)
    {
        diag_printf(out, ctx, "opendir '%s' failed (fs err %d)",
                    dir, (int)SYS_FS_Error());
        return;
    }

    SYS_FS_FSTAT st;
    uint32_t n = 0u;
    for (;;)
    {
        memset(&st, 0, sizeof(st));
        st.lfname = NULL;   /* use the fname field (long name) */
        st.lfsize = 0u;

        if (SYS_FS_DirRead(h, &st) != SYS_FS_RES_SUCCESS) { break; }
        if (st.fname[0] == '\0') { break; }   /* end of directory */

        bool is_dir = (st.fattrib & SYS_FS_ATTR_DIR) != 0u;
        diag_printf(out, ctx, "%c %10lu  %s",
                    is_dir ? 'd' : '-', (unsigned long)st.fsize, st.fname);
        n++;
    }
    (void)SYS_FS_DirClose(h);

    diag_printf(out, ctx, "(%lu entries)", (unsigned long)n);
}

void Storage_DiagBench(storage_print_fn out, void *ctx, uint32_t mbytes)
{
    if (!Storage_Mount())
    {
        diag_printf(out, ctx, "not mounted");
        return;
    }

    if (mbytes == 0u)               { mbytes = 4u; }
    if (mbytes > STG_BENCH_MAX_MB)  { mbytes = STG_BENCH_MAX_MB; }

    uint32_t chunks      = (mbytes * 1024u * 1024u) / STG_CHUNK;
    uint32_t total_bytes = chunks * STG_CHUNK;

    /* write */
    SYS_FS_HANDLE h = SYS_FS_FileOpen(STG_TESTFILE, SYS_FS_FILE_OPEN_WRITE);
    if (h == SYS_FS_HANDLE_INVALID)
    {
        diag_printf(out, ctx, "open(write) failed (fs err %d)", (int)SYS_FS_Error());
        return;
    }
    TickType_t t0 = xTaskGetTickCount();
    bool write_err = false;
    for (uint32_t c = 0u; c < chunks; c++)
    {
        fill_chunk(s_chunk, c);
        if (SYS_FS_FileWrite(h, s_chunk, STG_CHUNK) != STG_CHUNK)
        {
            write_err = true;
            break;
        }
    }
    (void)SYS_FS_FileClose(h);
    uint32_t wms = (uint32_t)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS);
    if (write_err)
    {
        diag_printf(out, ctx, "write failed (fs err %d)", (int)SYS_FS_Error());
        return;
    }
    diag_printf(out, ctx, "write:  %lu KB in %lu ms = %lu.%lu MB/s",
                (unsigned long)(total_bytes / 1024u), (unsigned long)wms,
                (unsigned long)(mbps_x10(total_bytes, wms) / 10u),
                (unsigned long)(mbps_x10(total_bytes, wms) % 10u));

    /* read back + verify */
    h = SYS_FS_FileOpen(STG_TESTFILE, SYS_FS_FILE_OPEN_READ);
    if (h == SYS_FS_HANDLE_INVALID)
    {
        diag_printf(out, ctx, "open(read) failed (fs err %d)", (int)SYS_FS_Error());
        return;
    }
    TickType_t r0 = xTaskGetTickCount();
    bool read_err = false, mismatch = false;
    for (uint32_t c = 0u; c < chunks; c++)
    {
        if (SYS_FS_FileRead(h, s_chunk, STG_CHUNK) != STG_CHUNK)
        {
            read_err = true;
            break;
        }
        if (!verify_chunk(s_chunk, c))
        {
            mismatch = true;
            break;
        }
    }
    (void)SYS_FS_FileClose(h);
    uint32_t rms = (uint32_t)((xTaskGetTickCount() - r0) * portTICK_PERIOD_MS);
    if (read_err)
    {
        diag_printf(out, ctx, "read failed (fs err %d)", (int)SYS_FS_Error());
        return;
    }
    diag_printf(out, ctx, "read:   %lu KB in %lu ms = %lu.%lu MB/s",
                (unsigned long)(total_bytes / 1024u), (unsigned long)rms,
                (unsigned long)(mbps_x10(total_bytes, rms) / 10u),
                (unsigned long)(mbps_x10(total_bytes, rms) % 10u));
    diag_printf(out, ctx, "verify: %s", mismatch ? "MISMATCH" : "ok");
}
