#include "isc_capture.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "definitions.h"
#include "log.h"
#include "system/int/sys_int.h"
#include "vision/drivers/csi/drv_csi.h"
#include "vision/drivers/csi2dc/drv_csi2dc.h"
#include "vision/drivers/image_sensor/drv_image_sensor.h"
#include "vision/drivers/isc/drv_isc.h"

#define ISC_CAP_MAX_W        1920u
#define ISC_CAP_MAX_H        1080u
#define ISC_CAP_BPP          3u    /* BYPASS+PACKED32+RMS=1+BPS=FORTY: dense BGR888 3 B/pixel */
/* At 60 fps depth N gives a subscriber holding a buffer pointer N×16.6 ms
 * before the producer laps. 4 → ~50 ms read window. Static pool max is
 * 4×1920×1080×3 ≈ 24 MB, trivial on 1 GB DDR3. */
#define ISC_CAP_NUM_BUFFERS  4u
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

static volatile ISC_Capture_FrameCallback s_frame_cb;
static volatile uintptr_t                 s_frame_ctx;

static void isc_frame_done(uintptr_t ctx)
{
    /* The MCC ISC driver increments DrvISCObj.frameIndex before invoking
     * this callback (drv_isc.c ISC_Handler), so frameIndex now points at
     * the descriptor hardware is about to write. The just-completed
     * buffer is the previous slot in the descriptor ring. */
    DRV_ISC_OBJ *obj = (DRV_ISC_OBJ *)ctx;
    uint32_t n = obj->dmaDescSize;
    uint32_t completed = (obj->frameIndex == 0u)
                       ? (n - 1u)
                       : (obj->frameIndex - 1u);
    uint32_t addr = (uint32_t)(uintptr_t)g_framebuffer
                  + completed * obj->dma.size;

    g_frame_count++;
    ISC_Capture_FrameCallback cb = s_frame_cb;
    if (cb != NULL) { cb(g_frame_count, addr, s_frame_ctx); }
}

void ISC_Capture_SetFrameCallback(ISC_Capture_FrameCallback cb, uintptr_t ctx)
{
    s_frame_ctx = ctx;
    s_frame_cb  = cb;
}

void ISC_Capture_Initialize(void)
{
    iscObj    = DRV_ISC_Initialize();
    csi2dcObj = DRV_CSI2DC_Initalize();
    csiObj    = DRV_CSI_Initalize();

    if (iscObj == NULL || csi2dcObj == NULL || csiObj == NULL)
    {
        LOG_ERROR("ISC_Capture: driver init returned NULL\r\n");
        return;
    }

    /* BYPASS RLP + IMODE=PACKED32 + BPS=FORTY + CSI2DC RMS=1:
     * CSI2DC byte-stream packs the MIPI RGB888 stream per CSI-2
     * Recommended Memory Storage spec (Table 49.27): four BGR pixels
     * across 12 bytes, dense 3 B/pixel BGR888 in memory. ISC PACKED32
     * DMA writes 4 bytes per CSI2DC word (samples-per-row =
     * (width × 3) / 4, COLMAX programmed accordingly). LCDC reads
     * natively in RGB_888_PACKED mode (memory order B, G, R per pixel
     * — Table 44.26). 25% less DDR bandwidth than the ARGB_8888 /
     * BGRX32 path (capture write + display read both shrink) while
     * preserving full RGB888 quality for vision consumers. */
    iscObj->inputFormat           = ISC_INPUT_FORMAT_TYPE;  /* RGB */
    iscObj->inputBits             = DRV_IMAGE_SENSOR_40_BIT; /* BPS=FORTY */
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

    /* PA=0 (VPCFGR bit 14): leave video pipe output LSB-aligned. PA=1 is
     * intended for 10/12-bit Bayer sensors being packed onto ISC's 12-bit
     * internal data bus. */
    csi2dcObj->videoPipeAlign = false;

    LOG_INFO("ISC_Capture: initialized\r\n");
}

static void diag_dump_rx(const char *tag)
{
    /* Skip the dozen register reads when DEBUG level isn't selected. */
    if ((int)log_get_level() < (int)LOG_LEVEL_DEBUG) { return; }

    LOG_DEBUG("ISC_Capture diag (%s):\r\n", tag);
    LOG_DEBUG("  CSI_PHY_RX=0x%08lX  STOPSTATE=0x%08lX\r\n",
              (unsigned long)CSI_REGS->CSI_PHY_RX,
              (unsigned long)CSI_REGS->CSI_PHY_STOPSTATE);
    LOG_DEBUG("  CSI_INT_ST_MAIN=0x%08lX\r\n",
              (unsigned long)CSI_REGS->CSI_INT_ST_MAIN);
    LOG_DEBUG("  CSI_INT_ST  PHY_FATAL=0x%08lX  PKT_FATAL=0x%08lX  FRAME_FATAL=0x%08lX\r\n",
              (unsigned long)CSI_REGS->CSI_INT_ST_PHY_FATAL,
              (unsigned long)CSI_REGS->CSI_INT_ST_PKT_FATAL,
              (unsigned long)CSI_REGS->CSI_INT_ST_FRAME_FATAL);
    LOG_DEBUG("  CSI_INT_ST  PHY=0x%08lX  PKT=0x%08lX\r\n",
              (unsigned long)CSI_REGS->CSI_INT_ST_PHY,
              (unsigned long)CSI_REGS->CSI_INT_ST_PKT);
    LOG_DEBUG("  CSI2DC_GSR=0x%08lX  GISR=0x%08lX  VPISR=0x%08lX\r\n",
              (unsigned long)CSI2DC_REGS->CSI2DC_GSR,
              (unsigned long)CSI2DC_REGS->CSI2DC_GISR,
              (unsigned long)CSI2DC_REGS->CSI2DC_VPISR);
    LOG_DEBUG("  CSI2DC_FNVC0R=0x%08lX  LNVC0R=0x%08lX\r\n",
              (unsigned long)CSI2DC_REGS->CSI2DC_FNVC0R,
              (unsigned long)CSI2DC_REGS->CSI2DC_LNVC0R);
    LOG_DEBUG("  ISC_INTSR=0x%08lX  ISC_DCTRL=0x%08lX  ISC_DCFG=0x%08lX\r\n",
              (unsigned long)ISC_Interrupt_Status(),
              (unsigned long)ISC_REGS->ISC_DCTRL,
              (unsigned long)ISC_REGS->ISC_DCFG);
    LOG_DEBUG("  ISC_RLP_CFG=0x%08lX  ISC_PFE_CFG0=0x%08lX\r\n",
              (unsigned long)ISC_REGS->ISC_RLP_CFG,
              (unsigned long)ISC_REGS->ISC_PFE_CFG0);
    LOG_DEBUG("  CSI2DC_VPCFGR=0x%08lX  CSI2DC_GCFGR=0x%08lX\r\n",
              (unsigned long)CSI2DC_REGS->CSI2DC_VPCFGR,
              (unsigned long)CSI2DC_REGS->CSI2DC_GCFGR);
}

bool ISC_Capture_Configure(uint32_t width, uint32_t height)
{
    if (g_running) { ISC_Capture_Stop(); }

    if (width == 0u || height == 0u
        || width > ISC_CAP_MAX_W || height > ISC_CAP_MAX_H)
    {
        LOG_ERROR("ISC_Capture: %lux%lu out of range\r\n",
                  (unsigned long)width, (unsigned long)height);
        return false;
    }

    uint32_t frame_size = width * height * ISC_CAP_BPP;

    csiObj->csiFrameWidth  = width;
    csiObj->csiFrameHeight = height;
    csiObj->csiFps         = 60u;

    /* imageWidth/Height stay in pixel units. With RMS=0 + PACKED32, the
     * ISC per-row sample counter is also in pixel units (1 sample = 1
     * 32-bit word = 1 pixel), so COLMAX = width - 1 programmed below. */
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
        LOG_ERROR("ISC_Capture: DRV_CSI2DC_Configure failed\r\n");
        return false;
    }
    if (!DRV_CSI_Configure(csiObj))
    {
        LOG_ERROR("ISC_Capture: DRV_CSI_Configure failed\r\n");
        return false;
    }
    if (DRV_ISC_Configure(iscObj) != 0u)
    {
        LOG_ERROR("ISC_Capture: DRV_ISC_Configure failed\r\n");
        return false;
    }

    /* Program PFE crop window to the configured image size. The MCC
     * ISC driver defines ISC_PFE_Crop_Area() but never calls it — the
     * PFE_CFG1/2 registers stay at reset value 0, which tells the PFE
     * to expect a 1×1 pixel frame. Setting the crop values alone isn't
     * enough: the Linux mchp-isc driver also sets COLEN+ROWEN in
     * PFE_CFG0 to activate the crop window; without these bits the
     * PFE doesn't properly detect line/frame boundaries and VD never
     * fires. DRV_ISC_Configure does NOT touch these registers, so our
     * writes here persist through to capture start.
     *
     * Note: emirror/libcamera does NOT call DRV_ISC_Configure_DMA
     * separately; DRV_ISC_Configure calls it internally (line 381 of
     * drv_isc.c). A second explicit call would reset DCFG back to the
     * MCC BEATS8 default and clobber our overrides. Don't add one. */
    /* RMS=1 byte-stream + IMODE=PACKED32: each ISC sample is one 32-bit
     * CSI2DC word; the byte stream emits (width × 3) bytes per row =
     * (width × 3 / 4) sample-words. COLMAX is samples-1 in this mode. */
    uint32_t isc_samples_per_row = (width * ISC_CAP_BPP) / 4u;
    ISC_REGS->ISC_PFE_CFG1 = ISC_PFE_CFG1_COLMIN(0u)
                           | ISC_PFE_CFG1_COLMAX(isc_samples_per_row - 1u);
    ISC_REGS->ISC_PFE_CFG2 = ISC_PFE_CFG2_ROWMIN(0u)
                           | ISC_PFE_CFG2_ROWMAX((uint32_t)height - 1u);
    ISC_REGS->ISC_PFE_CFG0 |= ISC_PFE_CFG0_COLEN_1 | ISC_PFE_CFG0_ROWEN_1;

    /* Override ISC DMA config:
     *  - IMODE=PACKED32: each ISC sample = one 32-bit CSI2DC VP word; DMA
     *    writes 4 bytes per sample to DDR. With RMS=1's byte-packed stream
     *    that works out to (width × 3)/4 samples per row × 4 B/sample =
     *    width × 3 bytes per row = dense 3 B/pixel BGR. PACKED8 would only
     *    write 1 byte per sample, dropping 3/4 of the data per scanline.
     *  - YMBSIZE/CMBSIZE=BEATS32: sama7g5's full AXI4 burst size per
     *    Linux mchp-isc driver (MCC default BEATS8 is sama5d2-era). */
    ISC_REGS->ISC_DCFG = ISC_DCFG_IMODE_PACKED32
                       | ISC_DCFG_YMBSIZE_BEATS32
                       | ISC_DCFG_CMBSIZE_BEATS32;

    LOG_INFO("ISC_Capture: configured %lux%lu BGR888 packed (%lu bytes/frame)\r\n",
             (unsigned long)width, (unsigned long)height,
             (unsigned long)frame_size);
    return true;
}

bool ISC_Capture_Start(void)
{
    if (!DRV_ISC_Start_Capture(iscObj))
    {
        LOG_ERROR("ISC_Capture: DRV_ISC_Start_Capture failed\r\n");
        diag_dump_rx("post-fail");
        return false;
    }

    SYS_INT_SourceEnable(ID_ISC);
    g_running = true;
    LOG_INFO("ISC_Capture: capture started\r\n");
    return true;
}

void ISC_Capture_Stop(void)
{
    if (!g_running) { return; }
    SYS_INT_SourceDisable(ID_ISC);
    DRV_ISC_Stop_Capture();
    g_running = false;
    LOG_INFO("ISC_Capture: stopped\r\n");
}

uint32_t ISC_Capture_FrameCount(void)
{
    return g_frame_count;
}

bool ISC_Capture_IsRunning(void)
{
    return g_running;
}

uint32_t ISC_Capture_GetBufferAddress(void)
{
    return (uint32_t)(uintptr_t)g_framebuffer;
}

