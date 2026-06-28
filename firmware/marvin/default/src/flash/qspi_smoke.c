#include "flash/qspi_smoke.h"

#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "definitions.h"
#include "peripheral/mmu/plib_mmu.h"
#include "peripheral/xdmac/plib_xdmac.h"
#include "log.h"

#define SMOKE_PAGE_SIZE   DRV_SST26_PAGE_SIZE   /* 256 */
#define POLL_MAX_TICKS    1000u                 /* ~1 s ceiling at 1 ms/tick */

#define BENCH_CHUNK       (64u * 1024u)         /* per-read granularity */
#define BENCH_MAX_MB      8u                    /* device is 8 MiB */

/* XDMAC channel for the mem2mem bench. Channel 0 is the ISC histogram DMA, so
 * we drive channel 1's registers directly (the PLIB only manages ch0). The
 * MMU's strongly-ordered attribute on the QSPI window does not apply to XDMAC
 * (a separate bus master, no MMU), so it can issue real AHB bursts. */
#define QSPI_DMA_CH       1u
#define DMA_STALL_GUARD   100000000u

/* Cacheable DDR destination (default .bss → 0x22000000+, cacheable-WB) so the
 * bench measures QSPI *read* throughput, not the strongly-ordered framebuffer
 * write. The .region_nocache framebuffer (0x20000000) is strongly-ordered, so a
 * CPU blit there is separately write-bound — the production splash path uses
 * DMA / LCDC scanout, neither of which is subject to that CPU MMU attribute. */
static uint8_t s_bench_dst[BENCH_CHUNK] __attribute__((aligned(32)));

/* Scratch buffers are static (not on the caller's stack) to honour the
 * static-allocation discipline and keep the CLI task stack small. The smoke
 * test is never run concurrently with itself. */
static uint8_t s_wr[SMOKE_PAGE_SIZE] __attribute__((aligned(4)));
static uint8_t s_rd[SMOKE_PAGE_SIZE] __attribute__((aligned(4)));

/* Block until the in-flight transfer finishes, yielding so DRV_SST26_Tasks
 * (if the driver is state-machine driven) can advance. Bounded so a stuck
 * transfer reports a timeout instead of hanging the console task. */
static DRV_SST26_TRANSFER_STATUS wait_done(DRV_HANDLE h)
{
    DRV_SST26_TRANSFER_STATUS st = DRV_SST26_TransferStatusGet(h);
    uint32_t guard = 0u;
    while ((st == DRV_SST26_TRANSFER_BUSY) && (guard < POLL_MAX_TICKS))
    {
        vTaskDelay(1);
        st = DRV_SST26_TransferStatusGet(h);
        guard++;
    }
    return st;
}

bool QspiSmoke_Run(void)
{
    DRV_HANDLE h = DRV_SST26_Open(DRV_SST26_INDEX, DRV_IO_INTENT_READWRITE);
    if (h == DRV_HANDLE_INVALID)
    {
        LOG_ERROR("QSPI: open FAILED\r\n");
        return false;
    }
    LOG_INFO("QSPI: open ok\r\n");

    /* --- JEDEC ID (expect BF 26 43 for SST26VF064B/BA) --- */
    uint8_t jedec[4] = {0};
    if (!DRV_SST26_ReadJedecId(h, jedec))
    {
        LOG_ERROR("QSPI: ReadJedecId FAILED\r\n");
        DRV_SST26_Close(h);
        return false;
    }
    LOG_INFO("QSPI: JEDEC ID = %02X %02X %02X  (expect BF 26 43)\r\n",
             jedec[0], jedec[1], jedec[2]);

    /* --- geometry --- */
    DRV_SST26_GEOMETRY geo;
    if (!DRV_SST26_GeometryGet(h, &geo))
    {
        LOG_ERROR("QSPI: GeometryGet FAILED\r\n");
        DRV_SST26_Close(h);
        return false;
    }
    uint32_t total = geo.read_blockSize * geo.read_numBlocks * geo.numReadRegions;
    LOG_INFO("QSPI: read=%lux%lu write=%lux%lu erase=%lux%lu total=%lu (%lu KiB)\r\n",
             (unsigned long)geo.read_blockSize,  (unsigned long)geo.read_numBlocks,
             (unsigned long)geo.write_blockSize, (unsigned long)geo.write_numBlocks,
             (unsigned long)geo.erase_blockSize, (unsigned long)geo.erase_numBlocks,
             (unsigned long)total, (unsigned long)(total / 1024u));

    /* Scratch = last erase block; low offsets stay reserved for splash/assets. */
    uint32_t addr = total - geo.erase_blockSize;
    LOG_INFO("QSPI: scratch sector @ 0x%06lX\r\n", (unsigned long)addr);

    /* --- erase + verify blank --- */
    if (!DRV_SST26_SectorErase(h, addr) ||
        (wait_done(h) != DRV_SST26_TRANSFER_COMPLETED))
    {
        LOG_ERROR("QSPI: SectorErase FAILED (block protection not unlocked?)\r\n");
        DRV_SST26_Close(h);
        return false;
    }
    if (!DRV_SST26_Read(h, s_rd, SMOKE_PAGE_SIZE, addr) ||
        (wait_done(h) != DRV_SST26_TRANSFER_COMPLETED))
    {
        LOG_ERROR("QSPI: Read (post-erase) FAILED\r\n");
        DRV_SST26_Close(h);
        return false;
    }
    bool blank = true;
    for (uint32_t i = 0u; i < SMOKE_PAGE_SIZE; i++)
    {
        if (s_rd[i] != 0xFFu) { blank = false; break; }
    }
    LOG_INFO("QSPI: erase ok (blank read-back: %s)\r\n", blank ? "yes" : "NO");

    /* --- program a known pattern --- */
    for (uint32_t i = 0u; i < SMOKE_PAGE_SIZE; i++)
    {
        s_wr[i] = (uint8_t)(i ^ 0xA5u);
    }
    if (!DRV_SST26_PageWrite(h, s_wr, addr) ||
        (wait_done(h) != DRV_SST26_TRANSFER_COMPLETED))
    {
        LOG_ERROR("QSPI: PageWrite FAILED\r\n");
        DRV_SST26_Close(h);
        return false;
    }
    LOG_INFO("QSPI: wrote %u-byte pattern\r\n", (unsigned)SMOKE_PAGE_SIZE);

    /* --- read back + verify --- */
    (void)memset(s_rd, 0, sizeof s_rd);
    if (!DRV_SST26_Read(h, s_rd, SMOKE_PAGE_SIZE, addr) ||
        (wait_done(h) != DRV_SST26_TRANSFER_COMPLETED))
    {
        LOG_ERROR("QSPI: Read (verify) FAILED\r\n");
        DRV_SST26_Close(h);
        return false;
    }

    DRV_SST26_Close(h);

    uint32_t mism = 0u;
    for (uint32_t i = 0u; i < SMOKE_PAGE_SIZE; i++)
    {
        if (s_rd[i] != s_wr[i]) { mism++; }
    }
    if (mism == 0u)
    {
        LOG_INFO("QSPI: read-back PASS (%u/%u match) -- smoke test PASS\r\n",
                 (unsigned)SMOKE_PAGE_SIZE, (unsigned)SMOKE_PAGE_SIZE);
        return true;
    }
    LOG_ERROR("QSPI: read-back FAIL (%lu/%u bytes mismatch)\r\n",
              (unsigned long)mism, (unsigned)SMOKE_PAGE_SIZE);
    return false;
}

/* Terminate a memory-mapped (SMM) read frame opened by raw reads of the
 * 0x60000000 window. The SST26 driver does this internally after each read;
 * raw memcpy / XDMAC reads do not, leaving CS asserted and blocking the next
 * QSPI command. Mirrors the tail of QSPI_MemoryRead. Bounded so it can't hang. */
static void qspi_end_smm_read(void)
{
    uint32_t g = 0u;
    while (((QSPI_REGS->QSPI_SR & QSPI_SR_RBUSY_Msk)   != 0u) && (g++ < 2000000u)) { }
    while (((QSPI_REGS->QSPI_SR & QSPI_SR_SYNCBSY_Msk) != 0u) && (g++ < 4000000u)) { }
    QSPI_REGS->QSPI_CR = QSPI_CR_LASTXFER_Msk;
    g = 0u;
    while (((QSPI_REGS->QSPI_ISR & QSPI_ISR_CSRA_Msk) == 0u) && (g++ < 2000000u)) { }
}

/* Position-dependent pattern: each word is derived from its absolute flash
 * byte address, so a shifted or misaddressed read fails verification, not just
 * stuck bits. */
static inline uint32_t pat_word(uint32_t addr)
{
    return (addr * 2654435761u) ^ 0xA5A5A5A5u;   /* Knuth multiplicative hash */
}

/* Configure XDMAC channel 1 for mem2mem, word width, 16-beat bursts. */
static void dma_cc_init(void)
{
    XDMAC_REGS->XDMAC_CHID[QSPI_DMA_CH].XDMAC_CC =
          XDMAC_CC_TYPE_MEM_TRAN | XDMAC_CC_MBSIZE_SIXTEEN
        | XDMAC_CC_SAM_INCREMENTED_AM | XDMAC_CC_DAM_INCREMENTED_AM
        | XDMAC_CC_SIF_AHB_IF1 | XDMAC_CC_DIF_AHB_IF1 | XDMAC_CC_DWIDTH_WORD;
    XDMAC_REGS->XDMAC_CHID[QSPI_DMA_CH].XDMAC_CNDC = 0u;
    XDMAC_REGS->XDMAC_CHID[QSPI_DMA_CH].XDMAC_CBC  = 0u;
}

/* One mem2mem block: QSPIMEM+src_off -> dst, len bytes (word multiple).
 * Returns false on stall. Caller owns cache maintenance + frame termination. */
static bool dma_read_block(uint32_t src_off, void *dst, uint32_t len)
{
    (void)XDMAC_REGS->XDMAC_CHID[QSPI_DMA_CH].XDMAC_CIS;
    XDMAC_REGS->XDMAC_CHID[QSPI_DMA_CH].XDMAC_CSA  = (uint32_t)QSPIMEM_ADDR + src_off;
    XDMAC_REGS->XDMAC_CHID[QSPI_DMA_CH].XDMAC_CDA  = (uint32_t)dst;
    XDMAC_REGS->XDMAC_CHID[QSPI_DMA_CH].XDMAC_CUBC = XDMAC_CUBC_UBLEN(len / 4u);
    __DMB();
    XDMAC_REGS->XDMAC_GE = (XDMAC_GE_EN0_Msk << QSPI_DMA_CH);

    uint32_t guard = 0u;
    while (((XDMAC_REGS->XDMAC_GS & (XDMAC_GS_ST0_Msk << QSPI_DMA_CH)) != 0u) &&
           (guard < DMA_STALL_GUARD))
    {
        guard++;
    }
    if (guard >= DMA_STALL_GUARD)
    {
        XDMAC_REGS->XDMAC_GD = (XDMAC_GD_DI0_Msk << QSPI_DMA_CH);
        return false;
    }
    return true;
}

static void bench_report(const char *name, uint32_t bytes, uint64_t t0, uint64_t t1)
{
    uint32_t freq = SYS_TIME_FrequencyGet();
    if (freq == 0u) { LOG_ERROR("QSPI bench: no timer\r\n"); return; }

    uint64_t us = ((t1 - t0) * 1000000ULL) / freq;
    if (us == 0u) { us = 1u; }
    /* bytes/us == MB/s (MB = 1e6); keep one decimal via x10. */
    uint32_t mbps_x10 = (uint32_t)(((uint64_t)bytes * 10ULL) / us);

    LOG_INFO("QSPI bench: %s %lu KiB in %lu.%03lu ms = %lu.%lu MB/s\r\n",
             name,
             (unsigned long)(bytes / 1024u),
             (unsigned long)(us / 1000u), (unsigned long)(us % 1000u),
             (unsigned long)(mbps_x10 / 10u), (unsigned long)(mbps_x10 % 10u));
}

void QspiSmoke_Bench(uint32_t mb)
{
    if (mb == 0u)            { mb = 4u; }
    if (mb > BENCH_MAX_MB)   { mb = BENCH_MAX_MB; }
    uint32_t total  = mb * 1024u * 1024u;
    uint32_t chunks = total / BENCH_CHUNK;

    LOG_INFO("QSPI bench: reading %lu MiB from offset 0 (%lu x %lu KiB chunks)\r\n",
             (unsigned long)mb, (unsigned long)chunks, (unsigned long)(BENCH_CHUNK / 1024u));

    /* --- path A: DRV_SST26_Read --- */
    DRV_HANDLE h = DRV_SST26_Open(DRV_SST26_INDEX, DRV_IO_INTENT_READ);
    if (h == DRV_HANDLE_INVALID)
    {
        LOG_ERROR("QSPI bench: open FAILED\r\n");
        return;
    }
    uint64_t t0 = SYS_TIME_Counter64Get();
    bool err = false;
    for (uint32_t c = 0u; (c < chunks) && !err; c++)
    {
        if (!DRV_SST26_Read(h, s_bench_dst, BENCH_CHUNK, c * BENCH_CHUNK) ||
            (wait_done(h) != DRV_SST26_TRANSFER_COMPLETED))
        {
            err = true;
        }
    }
    uint64_t t1 = SYS_TIME_Counter64Get();
    DRV_SST26_Close(h);
    if (err) { LOG_ERROR("QSPI bench: DRV_SST26_Read FAILED\r\n"); }
    else     { bench_report("DRV_SST26_Read", total, t0, t1); }

    /* --- path B: direct memcpy from the SMM-mapped XIP region --- */
    const uint8_t *xip = (const uint8_t *)(QSPIMEM_ADDR);
    uint64_t t2 = SYS_TIME_Counter64Get();
    for (uint32_t c = 0u; c < chunks; c++)
    {
        (void)memcpy(s_bench_dst, xip + (c * BENCH_CHUNK), BENCH_CHUNK);
    }
    uint64_t t3 = SYS_TIME_Counter64Get();
    qspi_end_smm_read();   /* close the frame left open by raw reads */
    bench_report("XIP memcpy   ", total, t2, t3);

    /* --- path C: XDMAC mem2mem burst from the XIP region (channel 1, driven
     *     directly — ch0 is the ISC histogram DMA). XDMAC bypasses the MMU, so
     *     it issues real AHB bursts the QSPI SMM slave can stream. --- */
    dma_cc_init();
    bool dma_err = false;
    uint64_t t4 = SYS_TIME_Counter64Get();
    for (uint32_t c = 0u; (c < chunks) && !dma_err; c++)
    {
        /* DMA writes bypass the cache; drop any stale lines for the dest. */
        dcache_CleanInvalidateByAddr(s_bench_dst, (int32_t)BENCH_CHUNK);
        if (!dma_read_block(c * BENCH_CHUNK, s_bench_dst, BENCH_CHUNK)) { dma_err = true; }
    }
    uint64_t t5 = SYS_TIME_Counter64Get();
    qspi_end_smm_read();   /* close the frame left open by raw DMA reads */
    if (dma_err) { LOG_ERROR("QSPI bench: XDMAC stalled (SMM may not stream bursts)\r\n"); }
    else         { bench_report("XDMAC mem2mem ", total, t4, t5); }

    LOG_INFO("QSPI bench: done (full 4.10 MB RGBA8888 splash scales from the above)\r\n");
}

bool QspiSmoke_Verify(uint32_t kb, uint32_t passes)
{
    uint32_t size = ((kb == 0u) ? 256u : kb) * 1024u;
    size &= ~(4096u - 1u);                 /* sector-align */
    if (size == 0u)     { size = 4096u; }
    if (passes == 0u)   { passes = 4u; }

    DRV_HANDLE h = DRV_SST26_Open(DRV_SST26_INDEX, DRV_IO_INTENT_READWRITE);
    if (h == DRV_HANDLE_INVALID)
    {
        LOG_ERROR("QSPI verify: open FAILED\r\n");
        return false;
    }

    DRV_SST26_GEOMETRY geo;
    if (!DRV_SST26_GeometryGet(h, &geo))
    {
        LOG_ERROR("QSPI verify: GeometryGet FAILED\r\n");
        DRV_SST26_Close(h);
        return false;
    }
    uint32_t total = geo.read_blockSize * geo.read_numBlocks * geo.numReadRegions;
    if (size > total) { size = total; }
    uint32_t base = total - size;          /* top of device, away from low offsets */

    LOG_INFO("QSPI verify: region 0x%06lX..0x%06lX (%lu KiB) x %lu passes, DRV + XDMAC\r\n",
             (unsigned long)base, (unsigned long)(base + size),
             (unsigned long)(size / 1024u), (unsigned long)passes);

    /* erase */
    for (uint32_t a = base; a < (base + size); a += geo.erase_blockSize)
    {
        if (!DRV_SST26_SectorErase(h, a) || (wait_done(h) != DRV_SST26_TRANSFER_COMPLETED))
        {
            LOG_ERROR("QSPI verify: erase FAILED @0x%06lX\r\n", (unsigned long)a);
            DRV_SST26_Close(h);
            return false;
        }
    }

    /* write the position-dependent pattern, one 256-byte page at a time */
    for (uint32_t a = base; a < (base + size); a += SMOKE_PAGE_SIZE)
    {
        uint32_t *wp = (uint32_t *)(void *)s_wr;
        for (uint32_t w = 0u; w < (SMOKE_PAGE_SIZE / 4u); w++)
        {
            wp[w] = pat_word(a + (w * 4u));
        }
        if (!DRV_SST26_PageWrite(h, s_wr, a) || (wait_done(h) != DRV_SST26_TRANSFER_COMPLETED))
        {
            LOG_ERROR("QSPI verify: write FAILED @0x%06lX\r\n", (unsigned long)a);
            DRV_SST26_Close(h);
            return false;
        }
    }

    /* verify: each pass reads the whole region via the driver AND via XDMAC */
    uint32_t mism_drv = 0u, mism_dma = 0u;
    bool dma_ok = true;
    uint32_t *rp = (uint32_t *)(void *)s_bench_dst;
    dma_cc_init();

    for (uint32_t p = 0u; p < passes; p++)
    {
        for (uint32_t off = 0u; off < size; off += BENCH_CHUNK)
        {
            uint32_t a    = base + off;
            uint32_t clen = ((size - off) < BENCH_CHUNK) ? (size - off) : BENCH_CHUNK;
            uint32_t nw   = clen / 4u;

            /* driver read (CPU; coherent for the CPU compare below) */
            if (DRV_SST26_Read(h, s_bench_dst, clen, a) &&
                (wait_done(h) == DRV_SST26_TRANSFER_COMPLETED))
            {
                for (uint32_t w = 0u; w < nw; w++)
                {
                    if (rp[w] != pat_word(a + (w * 4u))) { mism_drv++; }
                }
            }
            else { mism_drv++; }

            /* XDMAC read (the fast path); manage cache around the DMA write */
            if (dma_ok)
            {
                dcache_CleanInvalidateByAddr(s_bench_dst, (int32_t)clen);
                if (dma_read_block(a, s_bench_dst, clen))
                {
                    dcache_InvalidateByAddr(s_bench_dst, (int32_t)clen);
                    for (uint32_t w = 0u; w < nw; w++)
                    {
                        if (rp[w] != pat_word(a + (w * 4u))) { mism_dma++; }
                    }
                }
                else { dma_ok = false; }
            }
        }
        LOG_INFO("QSPI verify: pass %lu/%lu (DRV mism=%lu, XDMAC mism=%lu)\r\n",
                 (unsigned long)(p + 1u), (unsigned long)passes,
                 (unsigned long)mism_drv, (unsigned long)mism_dma);
    }

    qspi_end_smm_read();
    DRV_SST26_Close(h);

    if (!dma_ok) { LOG_ERROR("QSPI verify: XDMAC stalled mid-test\r\n"); }
    bool pass = (mism_drv == 0u) && (mism_dma == 0u) && dma_ok;
    LOG_INFO("QSPI verify: %s (%lu KiB x %lu passes; DRV mism=%lu, XDMAC mism=%lu)\r\n",
             pass ? "PASS" : "FAIL", (unsigned long)(size / 1024u), (unsigned long)passes,
             (unsigned long)mism_drv, (unsigned long)mism_dma);
    return pass;
}
