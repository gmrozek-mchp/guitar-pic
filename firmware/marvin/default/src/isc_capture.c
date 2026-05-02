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
#define ISC_CAP_BPP          3u    /* BYPASS+PACKED8+RMS=1: dense bytes, 3 B/pixel */
#define ISC_CAP_NUM_BUFFERS  2u
/* HSFREQRANGE for SAM9X75 D-PHY RX. SAM9X75 is DWC Gen3 per Linux DT
 * (snps,dw-dphy-rx with snps,phy_type=<0>, 8-bit bus). For 972 Mbps/lane
 * in Gen3 table: 0x0A (band covers ≤1000 Mbps). Was 0x14 at 297 Mbps
 * (works in both Gen2 and Gen3 — bands coincide there). Changes with bitrate. */
#define ISC_CAP_CSI_BITRATE  0x0Au

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

    /* Program PFE crop window to the configured image size. The MCC
     * ISC driver defines ISC_PFE_Crop_Area() but never calls it — the
     * PFE_CFG1/2 registers stay at reset value 0, which tells the PFE
     * to expect a 1×1 pixel frame. Setting the crop values alone isn't
     * enough: the Linux mchp-isc driver also sets COLEN+ROWEN in
     * PFE_CFG0 to activate the crop window; without these bits the
     * PFE doesn't properly detect line/frame boundaries and VD never
     * fires. */
    ISC_REGS->ISC_PFE_CFG1 = ISC_PFE_CFG1_COLMIN(0u)
                           | ISC_PFE_CFG1_COLMAX((uint32_t)width - 1u);
    ISC_REGS->ISC_PFE_CFG2 = ISC_PFE_CFG2_ROWMIN(0u)
                           | ISC_PFE_CFG2_ROWMAX((uint32_t)height - 1u);
    ISC_REGS->ISC_PFE_CFG0 |= ISC_PFE_CFG0_COLEN_1 | ISC_PFE_CFG0_ROWEN_1;

    if (DRV_ISC_Configure_DMA(iscObj) != 0u)
    {
        printf("ISC_Capture: DRV_ISC_Configure_DMA failed\r\n");
        return false;
    }

    /* Override ISC DMA burst size from BEATS8 (MCC / sama5d2-era default)
     * to BEATS32 (sama7g5 / full AXI4 default per Linux mchp-isc driver).
     * SAM9X75 uses the sama7g5 ISC variant — the RAM access port is full
     * 32-bit AXI4 and wants 32-beat bursts. At BEATS8, the AHB transaction
     * overhead is 4x what it should be. At 480p (83 MB/s) this is fine;
     * at 720p60 RGB888 (165 MB/s) the ISC FIFO back-pressures, HSYNC
     * detection stalls, HDTO fires after ~180 rows and DDONE never fires. */
    ISC_REGS->ISC_DCFG = ISC_DCFG_IMODE_PACKED8
                       | ISC_DCFG_YMBSIZE_BEATS32
                       | ISC_DCFG_CMBSIZE_BEATS32;
    printf("ISC_Capture: post-override ISC_DCFG=0x%08lX (expected 0x%08lX)\r\n",
           (unsigned long)ISC_REGS->ISC_DCFG,
           (unsigned long)(ISC_DCFG_IMODE_PACKED8
                          | ISC_DCFG_YMBSIZE_BEATS32
                          | ISC_DCFG_CMBSIZE_BEATS32));

    /* Pre-fill both buffers with a sentinel so the probe can tell which
     * memory regions DMA actually wrote vs. which are still untouched.
     * Flush cache to DDR so DMA writes land on top of known sentinel. */
    size_t fill_bytes = (size_t)frame_size * ISC_CAP_NUM_BUFFERS;
    (void)memset(g_framebuffer, 0x55, fill_bytes);
    SYS_CACHE_CleanDCache_by_Addr((uint32_t *)g_framebuffer, (int32_t)fill_bytes);

    printf("ISC_Capture: configured %lux%lu RGB888 packed (%lu bytes/frame); "
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

    /* Sample points are at w/4, w/2, 3w/4 horizontally (stays inside
     * pillarbox for 4:3-in-16:9 sources), and fractional Y capped by the
     * captured extent so we don't just read sentinel on unused rows. */
    static const struct { const char *tag; uint32_t xf, yf; } pts[] = {
        {"TL", 1, 1}, {"TM", 2, 1}, {"TR", 3, 1},
        {"ML", 1, 2}, {"CT", 2, 2}, {"MR", 3, 2},
        {"BL", 1, 3}, {"BM", 2, 3}, {"BR", 3, 3},
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

    /* Cliff diagnostic: figure out WHY ISC is writing partial frames and
     * never firing DDONE. Split four ways:
     *  (a) FNVC0 delta over 1 probe tick — if > 0, CSI2DC is receiving
     *      60 fps worth of frames even when ISC isn't completing them.
     *      Rules D-PHY / CSI2DC problems out.
     *  (b) iscObj->frameCount (VD pulses) — fires on every VSYNC edge
     *      even if the frame doesn't complete. Non-zero here confirms
     *      ISC's VD detection works.
     *  (c) iscObj->frameIndex (DDONE counter) — only increments when a
     *      full frame DMA completes. 0 means DDONE never fires.
     *  (d) PFE crop window — if COLMAX/ROWMAX don't match w-1 / h-1,
     *      ISC expects a different-shape frame than what's arriving.
     *  Plus ISC_INTSR error bits (HDTO/VDTO = horiz/vert timeout,
     *  DAOV = DMA overflow, RERR/WERR = bus access errors). */
    {
        static uint32_t last_fnvc0        = 0u;
        static uint32_t last_frame_count  = 0u;
        static uint8_t  last_frame_index  = 0u;
        static bool     cliff_diag_seeded = false;

        uint32_t pfe_cfg0 = ISC_REGS->ISC_PFE_CFG0;
        uint32_t pfe_cfg1 = ISC_REGS->ISC_PFE_CFG1;
        uint32_t pfe_cfg2 = ISC_REGS->ISC_PFE_CFG2;
        uint32_t ctrlsr   = ISC_REGS->ISC_CTRLSR;
        uint32_t dctrl    = ISC_REGS->ISC_DCTRL;
        uint32_t dnda     = ISC_REGS->ISC_DNDA;
        uint32_t intsr    = ISC_Interrupt_Status();  /* cleared on read */

        uint32_t col_min = (pfe_cfg1 & ISC_PFE_CFG1_COLMIN_Msk)
                           >> ISC_PFE_CFG1_COLMIN_Pos;
        uint32_t col_max = (pfe_cfg1 & ISC_PFE_CFG1_COLMAX_Msk)
                           >> ISC_PFE_CFG1_COLMAX_Pos;
        uint32_t row_min = (pfe_cfg2 & ISC_PFE_CFG2_ROWMIN_Msk)
                           >> ISC_PFE_CFG2_ROWMIN_Pos;
        uint32_t row_max = (pfe_cfg2 & ISC_PFE_CFG2_ROWMAX_Msk)
                           >> ISC_PFE_CFG2_ROWMAX_Pos;

        uint8_t  cur_frame_index = iscObj->frameIndex;
        uint32_t cur_frame_count = iscObj->frameCount;

        int32_t  fnvc0_delta       = cliff_diag_seeded
                                     ? (int32_t)(fnvc0 - last_fnvc0) : 0;
        int32_t  frame_count_delta = cliff_diag_seeded
                                     ? (int32_t)(cur_frame_count - last_frame_count) : 0;
        int32_t  frame_index_delta = cliff_diag_seeded
                                     ? (int32_t)((uint8_t)(cur_frame_index - last_frame_index)) : 0;

        printf("  cliff: PFE_CFG0=0x%08lX PFE_CFG1=0x%08lX PFE_CFG2=0x%08lX\r\n",
               (unsigned long)pfe_cfg0,
               (unsigned long)pfe_cfg1,
               (unsigned long)pfe_cfg2);
        printf("  cliff: PFE crop col[%lu..%lu] row[%lu..%lu]"
               "  expected col[0..%lu] row[0..%lu]\r\n",
               (unsigned long)col_min, (unsigned long)col_max,
               (unsigned long)row_min, (unsigned long)row_max,
               (unsigned long)(w - 1u), (unsigned long)(h - 1u));
        printf("  cliff: CTRLSR=0x%08lX DCTRL=0x%08lX DNDA=0x%08lX\r\n",
               (unsigned long)ctrlsr,
               (unsigned long)dctrl,
               (unsigned long)dnda);
        printf("  cliff: INTSR=0x%08lX [%s%s%s%s%s%s%s%s]\r\n",
               (unsigned long)intsr,
               (intsr & ISC_INTSR_VD_Msk)     ? "VD "     : "",
               (intsr & ISC_INTSR_HD_Msk)     ? "HD "     : "",
               (intsr & ISC_INTSR_DDONE_Msk)  ? "DDONE "  : "",
               (intsr & ISC_INTSR_LDONE_Msk)  ? "LDONE "  : "",
               (intsr & ISC_INTSR_HDTO_Msk)   ? "HDTO! "  : "",
               (intsr & ISC_INTSR_VDTO_Msk)   ? "VDTO! "  : "",
               (intsr & ISC_INTSR_DAOV_Msk)   ? "DAOV! "  : "",
               (intsr & (ISC_INTSR_WERR_Msk | ISC_INTSR_RERR_Msk))
                                              ? "BUSERR! " : "");
        printf("  cliff: FNVC0 delta=%ld (since prev probe); "
               "iscObj frameIndex=%u (d=%ld) frameCount=%lu (d=%ld) "
               "g_frame_count=%lu\r\n",
               (long)fnvc0_delta,
               (unsigned)cur_frame_index, (long)frame_index_delta,
               (unsigned long)cur_frame_count, (long)frame_count_delta,
               (unsigned long)g_frame_count);

        last_fnvc0         = fnvc0;
        last_frame_count   = cur_frame_count;
        last_frame_index   = cur_frame_index;
        cliff_diag_seeded  = true;
    }

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
        /* xf: 1/2/3 -> w/4, w/2, 3w/4 (avoid pillarbox at x=0 and x=w-1
         *               for 4:3 sources in a 16:9 frame).
         * yf: 1/2/3 -> h/8, h/2, 7h/8 (bias top sample slightly down to
         *               avoid top-edge overscan). */
        uint32_t x = (w * pts[i].xf) / 4u;
        if (x >= w) { x = w - 1u; }
        uint32_t y = (pts[i].yf == 1u) ? (h / 8u)
                   : (pts[i].yf == 2u) ? (h / 2u)
                                       : ((h * 7u) / 8u);
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
    /* Dumps at x=w/4 (inside pillarbox for 4:3-in-16:9 sources) across
     * several rows. Rows chosen to span the typical captured range
     * regardless of frame size — 5, 20, 60 are within any cliff we've
     * seen; 100 and 200 are past 480p cliff but still within 720p's. */
    struct { uint32_t y; uint32_t x; } dumps[] = {
        {   5u, w / 4u },
        {  20u, w / 4u },
        {  60u, w / 4u },
        { 100u, w / 4u },
        { 200u, w / 4u },
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

    /* Per-row byte-alignment phase scan: for each captured row, find the
     * mod-3 offset of the first non-trivial byte in the middle of the row.
     * For a solid-color source the phase should be constant across rows;
     * changes indicate the CSI/ISC stream is shifting within the row. */
    int prev_phase = -2;
    uint32_t transitions = 0;
    for (uint32_t y = 0; y <= last_written_row && y < h; y++)
    {
        uint32_t row_base = y * w * ISC_CAP_BPP;
        int phase = -1;
        uint32_t scan_lo = (w / 3u) * ISC_CAP_BPP;
        uint32_t scan_hi = (2u * w / 3u) * ISC_CAP_BPP;
        for (uint32_t b = scan_lo; b < scan_hi; b++)
        {
            if (buf[row_base + b] >= 0x40u)
            {
                phase = (int)(b % 3u);
                break;
            }
        }
        if (phase != prev_phase)
        {
            printf("  phase: row %3lu -> %d\r\n", (unsigned long)y, phase);
            prev_phase = phase;
            if (++transitions >= 16u)
            {
                printf("  phase: ... (16 transitions, truncated)\r\n");
                break;
            }
        }
    }
    printf("  BYPASS+PACKED8+RMS=1: 3 bytes/pixel packed. Byte order TBD on\r\n"
           "  this source; feed solid primaries (R/G/B) to identify the map.\r\n");

    return true;
}
