#include "isc_capture.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "definitions.h"
#include "system/cache/sys_cache.h"
#include "system/int/sys_int.h"
#include "vision/drivers/csi/drv_csi.h"
#include "vision/drivers/csi2dc/drv_csi2dc.h"
#include "vision/drivers/image_sensor/drv_image_sensor.h"
#include "vision/drivers/isc/drv_isc.h"

#define ISC_CAP_MAX_W        1920u
#define ISC_CAP_MAX_H        1080u
#define ISC_CAP_BPP          3u    /* BYPASS+PACKED8+RMS=1: dense BGR bytes, 3 B/pixel */
#define ISC_CAP_NUM_BUFFERS  2u
#define ISC_CAP_CSI_BITRATE  0x14u

static __attribute__((__section__(".region_cache_aligned")))
       __attribute__((__aligned__(32)))
       uint8_t g_framebuffer[ISC_CAP_MAX_W * ISC_CAP_MAX_H * ISC_CAP_BPP * ISC_CAP_NUM_BUFFERS];

static DRV_CSI_OBJ*    csiObj;
static DRV_CSI2DC_OBJ* csi2dcObj;
static DRV_ISC_OBJ*    iscObj;

static volatile uint32_t g_frame_count;
static bool              g_running;

static void isc_frame_done(uintptr_t ctx)
{
    (void)ctx;
    g_frame_count++;
}

void ISC_Capture_Initialize(void)
{
    iscObj    = DRV_ISC_Initialize();
    csi2dcObj = DRV_CSI2DC_Initalize();
    csiObj    = DRV_CSI_Initalize();

    if (iscObj == NULL || csi2dcObj == NULL || csiObj == NULL)
    {
        printf("ISC_Capture: driver init returned NULL\r\n");
        return;
    }

    /* RGB888 byte-stream dump: CSI2DC VP RMS=1 emits raw bytes, ISC RLP in
     * BYPASS samples them, DMA PACKED8 writes one byte per clock. This is
     * the only config that captures all 3 bytes/pixel for RGB888 (RMS=0
     * collapses to 1 sample per pixel). Tradeoff: RMS=1 hits a ~25% ISC
     * throughput cliff on this chip. Memory: packed BGR triples. */
    iscObj->inputFormat           = ISC_INPUT_FORMAT_TYPE;  /* RGB */
    iscObj->inputBits             = ISC_INPUT_BIT_WIDTH;    /* 8-bit */
    iscObj->rlpMode               = ISC_RLP_CFG_MODE_BYPASS;
    iscObj->layout                = ISC_LAYOUT_PACKED8;
    iscObj->bayerPattern          = ISC_BAYER_PATTERN_TYPE;
    iscObj->enableMIPI            = ISC_ENABLE_MIPI_INTERFACE;
    iscObj->enableVideoMode       = ISC_ENABLE_VIDEO_MODE;
    iscObj->enableProgressiveMode = ISC_ENABLE_PROGRESSIVE_MODE;
    iscObj->enableScaling         = ISC_ENABLE_SCALING;
    iscObj->dpc.enableDPC         = ISC_ENABLE_DPC;
    iscObj->dpc.enableGDC         = ISC_ENABLE_GDC;
    iscObj->dpc.enableBLC         = ISC_ENABLE_BLC;
    iscObj->cbc.enableCBC         = ISC_ENABLE_BRIGHTNESS_CONTRAST;
    iscObj->whiteBalance.enableWB = ISC_ENABLE_WHITE_BALANCE;
    iscObj->gamma.enableGamma     = ISC_ENABLE_GAMMA;
    iscObj->enableHistogram       = ISC_ENABLE_HISTOGRAM;
    iscObj->dmaDescSize           = (uint8_t)ISC_CAP_NUM_BUFFERS;
    iscObj->dma.callback          = isc_frame_done;

    /* DRV_CSI_Initalize hardcodes csiBitRate = 0x16; override here. */
    csiObj->csiBitRate = ISC_CAP_CSI_BITRATE;

    /* TC358743 uses continuous-clock mode (TXOPTIONCNTRL = CONTCLKMODE).
     * CSI2DC must match: MIPIFRN = 0 (free-running). MCC default is gated
     * (false -> MIPIFRN=1); mismatch leaves CSI2DC.GSR.ARSTIP stuck. */
    csi2dcObj->enableMIPIFreeRun = true;

    printf("ISC_Capture: initialized\r\n");
}

static void diag_dump_rx(const char *tag)
{
    printf("ISC_Capture diag (%s):\r\n", tag);
    printf("  CSI_PHY_RX=0x%08lX  STOPSTATE=0x%08lX\r\n",
           (unsigned long)CSI_REGS->CSI_PHY_RX,
           (unsigned long)CSI_REGS->CSI_PHY_STOPSTATE);
    printf("  CSI_INT_ST_MAIN=0x%08lX\r\n",
           (unsigned long)CSI_REGS->CSI_INT_ST_MAIN);
    printf("  CSI_INT_ST  PHY_FATAL=0x%08lX  PKT_FATAL=0x%08lX  FRAME_FATAL=0x%08lX\r\n",
           (unsigned long)CSI_REGS->CSI_INT_ST_PHY_FATAL,
           (unsigned long)CSI_REGS->CSI_INT_ST_PKT_FATAL,
           (unsigned long)CSI_REGS->CSI_INT_ST_FRAME_FATAL);
    printf("  CSI_INT_ST  PHY=0x%08lX  PKT=0x%08lX\r\n",
           (unsigned long)CSI_REGS->CSI_INT_ST_PHY,
           (unsigned long)CSI_REGS->CSI_INT_ST_PKT);
    printf("  CSI2DC_GSR=0x%08lX  GISR=0x%08lX  VPISR=0x%08lX\r\n",
           (unsigned long)CSI2DC_REGS->CSI2DC_GSR,
           (unsigned long)CSI2DC_REGS->CSI2DC_GISR,
           (unsigned long)CSI2DC_REGS->CSI2DC_VPISR);
    printf("  CSI2DC_FNVC0R=0x%08lX  LNVC0R=0x%08lX\r\n",
           (unsigned long)CSI2DC_REGS->CSI2DC_FNVC0R,
           (unsigned long)CSI2DC_REGS->CSI2DC_LNVC0R);
    printf("  ISC_INTSR=0x%08lX  ISC_DCTRL=0x%08lX  ISC_DCFG=0x%08lX\r\n",
           (unsigned long)ISC_Interrupt_Status(),
           (unsigned long)ISC_REGS->ISC_DCTRL,
           (unsigned long)ISC_REGS->ISC_DCFG);
    printf("  ISC_RLP_CFG=0x%08lX  ISC_PFE_CFG0=0x%08lX\r\n",
           (unsigned long)ISC_REGS->ISC_RLP_CFG,
           (unsigned long)ISC_REGS->ISC_PFE_CFG0);
    printf("  CSI2DC_VPCFGR=0x%08lX  CSI2DC_GCFGR=0x%08lX\r\n",
           (unsigned long)CSI2DC_REGS->CSI2DC_VPCFGR,
           (unsigned long)CSI2DC_REGS->CSI2DC_GCFGR);
}

bool ISC_Capture_Configure(uint32_t width, uint32_t height)
{
    if (g_running) { ISC_Capture_Stop(); }

    if (width == 0u || height == 0u
        || width > ISC_CAP_MAX_W || height > ISC_CAP_MAX_H)
    {
        printf("ISC_Capture: %lux%lu out of range\r\n",
               (unsigned long)width, (unsigned long)height);
        return false;
    }

    uint32_t frame_size = width * height * ISC_CAP_BPP;

    csiObj->csiFrameWidth  = width;
    csiObj->csiFrameHeight = height;
    csiObj->csiFps         = 60u;

    iscObj->imageWidth   = (uint16_t)width;
    iscObj->imageHeight  = (uint16_t)height;
    iscObj->outputWidth  = (uint16_t)width;
    iscObj->outputHeight = (uint16_t)height;
    iscObj->dma.address0 = (uint32_t)(uintptr_t)g_framebuffer;
    iscObj->dma.size     = frame_size;

    g_frame_count = 0u;

    /* Configure order mirrors emirror's working CAMERA_Open:
     * CSI2DC -> CSI -> ISC. RX side must be ready before source
     * transmits, so the D-PHY catches the LP11->HS transition. */
    if (!DRV_CSI2DC_Configure(csi2dcObj))
    {
        printf("ISC_Capture: DRV_CSI2DC_Configure failed\r\n");
        return false;
    }
    if (!DRV_CSI_Configure(csiObj))
    {
        printf("ISC_Capture: DRV_CSI_Configure failed\r\n");
        return false;
    }
    if (DRV_ISC_Configure(iscObj) != 0u)
    {
        printf("ISC_Capture: DRV_ISC_Configure failed\r\n");
        return false;
    }
    if (DRV_ISC_Configure_DMA(iscObj) != 0u)
    {
        printf("ISC_Capture: DRV_ISC_Configure_DMA failed\r\n");
        return false;
    }

    /* Pre-fill both buffers with a sentinel so the probe can tell which
     * memory regions DMA actually wrote vs. which are still untouched.
     * Flush cache to DDR so DMA writes land on top of known sentinel. */
    size_t fill_bytes = (size_t)frame_size * ISC_CAP_NUM_BUFFERS;
    (void)memset(g_framebuffer, 0x55, fill_bytes);
    SYS_CACHE_CleanDCache_by_Addr((uint32_t *)g_framebuffer, (int32_t)fill_bytes);

    printf("ISC_Capture: configured %lux%lu ARGB32 (%lu bytes/frame); "
           "buffers pre-filled with 0x55\r\n",
           (unsigned long)width, (unsigned long)height,
           (unsigned long)frame_size);
    return true;
}

bool ISC_Capture_Start(void)
{
    diag_dump_rx("pre-start");
    if (!DRV_ISC_Start_Capture(iscObj))
    {
        printf("ISC_Capture: DRV_ISC_Start_Capture failed\r\n");
        diag_dump_rx("post-fail");
        return false;
    }

    SYS_INT_SourceEnable(ID_ISC);
    g_running = true;
    printf("ISC_Capture: capture started\r\n");
    return true;
}

void ISC_Capture_Stop(void)
{
    if (!g_running) { return; }
    SYS_INT_SourceDisable(ID_ISC);
    DRV_ISC_Stop_Capture();
    g_running = false;
    printf("ISC_Capture: stopped\r\n");
}

uint32_t ISC_Capture_FrameCount(void)
{
    return g_frame_count;
}

bool ISC_Capture_IsRunning(void)
{
    return g_running;
}

bool ISC_Capture_ProbeFrame(void)
{
    if (!g_running || iscObj == NULL) { return false; }

    uint32_t w = iscObj->imageWidth;
    uint32_t h = iscObj->imageHeight;
    if (w == 0u || h == 0u) { return false; }

    uint32_t frame_size = w * h * ISC_CAP_BPP;

    /* frameIndex points at the NEXT buffer ISC will write (ISR increments
     * after each DDONE). Last completed buffer is the one before that. */
    uint32_t buf_idx = ((uint32_t)iscObj->frameIndex + ISC_CAP_NUM_BUFFERS - 1u)
                       % ISC_CAP_NUM_BUFFERS;
    uint8_t *buf = &g_framebuffer[buf_idx * frame_size];

    SYS_CACHE_InvalidateDCache_by_Addr((uint32_t *)buf, (int32_t)frame_size);

    static const struct { const char *tag; uint32_t xf, yf; } pts[] = {
        {"TL", 0, 0}, {"TM", 1, 0}, {"TR", 2, 0},
        {"ML", 0, 1}, {"CT", 1, 1}, {"MR", 2, 1},
        {"BL", 0, 2}, {"BM", 1, 2}, {"BR", 2, 2},
    };

    uint32_t vpisr  = CSI2DC_REGS->CSI2DC_VPISR;
    uint32_t gisr   = CSI2DC_REGS->CSI2DC_GISR;
    uint32_t fnvc0  = CSI2DC_REGS->CSI2DC_FNVC0R;
    uint32_t vpcolr = CSI2DC_REGS->CSI2DC_VPCOLR;
    uint32_t vprowr = CSI2DC_REGS->CSI2DC_VPROWR;

    printf("ISC probe (frame=%lu, buf=%lu, %lux%lu) "
           "VPISR=0x%08lX GISR=0x%08lX FNVC0=0x%08lX "
           "VPCOL=%lu VPROW=%lu\r\n",
           (unsigned long)g_frame_count, (unsigned long)buf_idx,
           (unsigned long)w, (unsigned long)h,
           (unsigned long)vpisr, (unsigned long)gisr, (unsigned long)fnvc0,
           (unsigned long)vpcolr, (unsigned long)vprowr);

    /* CSI2DC Image Data Snoop (IDS) — authoritative per-packet metadata
     * captured directly from the CSI-2 stream. Each entry holds one
     * {DT, VC} combination; WC is the MIPI long-packet word count (bytes)
     * and RC+1 is how many packets of that DT/VC the snoop has seen since
     * last reset. We reset IDS at end-of-probe so each probe window shows
     * exactly 1 second (~60 frames) of accumulated counts — divide RC by
     * frame delta to get per-frame row count authoritatively. */
    uint32_t idsisr = CSI2DC_REGS->CSI2DC_IDSISR;
    printf("  IDSISR=0x%08lX\r\n", (unsigned long)idsisr);
    for (int i = 0; i < CSI2DC_IDSEW_NUMBER; i++)
    {
        uint32_t w0 = CSI2DC_REGS->CSI2DC_IDSEW[i].CSI2DC_IDSEW0R;
        uint32_t w1 = CSI2DC_REGS->CSI2DC_IDSEW[i].CSI2DC_IDSEW1R;
        if ((w0 | w1) == 0u) { continue; }
        uint32_t dt = (w0 & CSI2DC_IDSEW0R_DT_Msk) >> CSI2DC_IDSEW0R_DT_Pos;
        uint32_t vc = (w0 & CSI2DC_IDSEW0R_VC_Msk) >> CSI2DC_IDSEW0R_VC_Pos;
        uint32_t wc = (w1 & CSI2DC_IDSEW1R_WC_Msk) >> CSI2DC_IDSEW1R_WC_Pos;
        uint32_t rc = (w1 & CSI2DC_IDSEW1R_RC_Msk) >> CSI2DC_IDSEW1R_RC_Pos;
        printf("  IDS[%d]: DT=0x%02lX VC=%lu WC=%lu rows=%lu\r\n",
               i, (unsigned long)dt, (unsigned long)vc,
               (unsigned long)wc, (unsigned long)(rc + 1u));
    }
    /* Reset IDS so next probe shows a fresh accumulation window. */
    CSI2DC_REGS->CSI2DC_IDSCR = CSI2DC_IDSCR_SWRST_1;

    uint8_t b0min = 0xFFu, b0max = 0u;
    uint8_t b1min = 0xFFu, b1max = 0u;
    uint8_t b2min = 0xFFu, b2max = 0u;
    uint8_t b3min = 0xFFu, b3max = 0u;

    for (size_t i = 0; i < sizeof(pts) / sizeof(pts[0]); i++)
    {
        uint32_t x = (pts[i].xf == 0u) ? 0u
                   : (pts[i].xf == 1u) ? (w / 2u)
                                       : (w - 1u);
        uint32_t y = (pts[i].yf == 0u) ? 0u
                   : (pts[i].yf == 1u) ? (h / 2u)
                                       : (h - 1u);
        uint32_t off = ((y * w) + x) * ISC_CAP_BPP;

        uint8_t b0 = buf[off + 0u];
        uint8_t b1 = buf[off + 1u];
        uint8_t b2 = buf[off + 2u];
        uint8_t b3 = buf[off + 3u];

        printf("  %s (%4lu,%4lu) mem: %02X %02X %02X %02X\r\n",
               pts[i].tag, (unsigned long)x, (unsigned long)y, b0, b1, b2, b3);

        if (b0 < b0min) { b0min = b0; } if (b0 > b0max) { b0max = b0; }
        if (b1 < b1min) { b1min = b1; } if (b1 > b1max) { b1max = b1; }
        if (b2 < b2min) { b2min = b2; } if (b2 > b2max) { b2max = b2; }
        if (b3 < b3min) { b3min = b3; } if (b3 > b3max) { b3max = b3; }
    }

    printf("  9-pt range:  b0=[%02X-%02X] b1=[%02X-%02X] b2=[%02X-%02X] b3=[%02X-%02X]\r\n",
           b0min, b0max, b1min, b1max, b2min, b2max, b3min, b3max);

    /* Raw hex dumps: several (row, x) offsets so we can see the actual
     * memory pattern without any pixel-layout assumption. 48 bytes = 16
     * pixels at 3 B/px, 12 pixels at 4 B/px. */
    struct { uint32_t y; uint32_t x; } dumps[] = {
        { 0u,   0u  },     /* row 0 left edge (likely black margin) */
        { 0u,   w/2u },    /* row 0 center (content) */
        { 10u,  w/2u },    /* row 10 center — iter 3 captured region */
        { 50u,  w/2u },    /* row 50 center — edge of iter 3 cliff */
        { 100u, w/2u },    /* row 100 — past iter 3 cliff, within iter 5 */
        { 200u, w/2u },    /* row 200 — past iter 3, within iter 5 */
    };
    for (size_t r = 0; r < sizeof(dumps) / sizeof(dumps[0]); r++)
    {
        uint32_t y = dumps[r].y;
        uint32_t x = dumps[r].x;
        if (y >= h || x >= w) { continue; }
        uint32_t off_start = ((y * w) + x) * ISC_CAP_BPP;
        printf("  r%3lu x%3lu+: ", (unsigned long)y, (unsigned long)x);
        for (uint32_t i = 0; i < 48u; i++)
        {
            printf("%02X%s", buf[off_start + i], (i & 3u) == 3u ? " " : "");
        }
        printf("\r\n");
    }

    /* Scan a known-captured row (y=30) for byte-position ranges so we can
     * see which byte lanes actually carry data. */
    uint32_t y_scan = 30u;
    uint8_t r0min = 0xFFu, r0max = 0u;
    uint8_t r1min = 0xFFu, r1max = 0u;
    uint8_t r2min = 0xFFu, r2max = 0u;
    uint8_t r3min = 0xFFu, r3max = 0u;
    for (uint32_t x = 0; x < w; x++)
    {
        uint32_t off = ((y_scan * w) + x) * ISC_CAP_BPP;
        uint8_t v0 = buf[off + 0u];
        uint8_t v1 = buf[off + 1u];
        uint8_t v2 = buf[off + 2u];
        uint8_t v3 = buf[off + 3u];
        if (v0 < r0min) { r0min = v0; } if (v0 > r0max) { r0max = v0; }
        if (v1 < r1min) { r1min = v1; } if (v1 > r1max) { r1max = v1; }
        if (v2 < r2min) { r2min = v2; } if (v2 > r2max) { r2max = v2; }
        if (v3 < r3min) { r3min = v3; } if (v3 > r3max) { r3max = v3; }
    }
    printf("  row=%lu rng: b0=[%02X-%02X] b1=[%02X-%02X] b2=[%02X-%02X] b3=[%02X-%02X]\r\n",
           (unsigned long)y_scan,
           r0min, r0max, r1min, r1max, r2min, r2max, r3min, r3max);

    /* Vertical extent of DMA writes: scan col w/2 for first non-sentinel
     * and last non-sentinel row. Sentinel 0x55 means DMA never wrote. */
    uint32_t first_sentinel_row = h;
    uint32_t last_written_row   = 0;
    for (uint32_t y = 0; y < h; y++)
    {
        uint32_t off = ((y * w) + (w / 2u)) * ISC_CAP_BPP;
        if (buf[off] != 0x55u)
        {
            last_written_row = y;
        }
        else if (first_sentinel_row == h)
        {
            first_sentinel_row = y;
        }
    }
    printf("  vertical extent: rows 0..%lu written, first sentinel at row %lu (of %lu)\r\n",
           (unsigned long)last_written_row,
           (unsigned long)first_sentinel_row,
           (unsigned long)h);
    printf("  BYPASS+PACKED8+RMS=1: 3 bytes/pixel, GRB byte order (Phase 5b). "
           "Solid R=FF through A2D gain ~0.753 lands as 00 C0 00 repeating.\r\n");

    return true;
}
