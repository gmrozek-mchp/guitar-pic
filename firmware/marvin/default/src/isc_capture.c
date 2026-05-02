#include "isc_capture.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "definitions.h"
#include "system/int/sys_int.h"
#include "vision/drivers/csi/drv_csi.h"
#include "vision/drivers/csi2dc/drv_csi2dc.h"
#include "vision/drivers/image_sensor/drv_image_sensor.h"
#include "vision/drivers/isc/drv_isc.h"

#define ISC_CAP_MAX_W        1920u
#define ISC_CAP_MAX_H        1080u
#define ISC_CAP_BPP          4u    /* BYPASS+PACKED32+RMS=0+BPS=FORTY: BGRX32 4 B/pixel */
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

    /* BYPASS RLP + IMODE=PACKED32 + BPS=FORTY + CSI2DC RMS=0:
     * CSI2DC emits demux_data = 0x00_00RRGGBB on the 40-bit VP bus
     * (one pixel per word, per Table 49.25). RLP BYPASS samples the
     * low 32 bits (0x00RRGGBB) unchanged; PACKED32 stores each 32-bit
     * sample as 4 bytes in DDR — little-endian order -> B G R 00 =
     * BGRX32. Costs 33% more bandwidth than RMS=1 packed BGR but
     * matches LCDC/GFX2D native 32 bpp format. The X byte is 0x00
     * (RLP ALPHA register requires RGB32 RLP mode, which does not
     * work on MIPI bypass — see journal). */
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
     * fires. DRV_ISC_Configure does NOT touch these registers, so our
     * writes here persist through to capture start.
     *
     * Note: emirror/libcamera does NOT call DRV_ISC_Configure_DMA
     * separately; DRV_ISC_Configure calls it internally (line 381 of
     * drv_isc.c). A second explicit call would reset DCFG back to the
     * MCC BEATS8 default and clobber our overrides. Don't add one. */
    /* RMS=0 -> CSI2DC emits one 32-bit VP word per pixel. PACKED32 IMODE
     * writes 4 bytes per sample to DDR. COLMAX is in sample units, which
     * now matches pixels 1:1. */
    ISC_REGS->ISC_PFE_CFG1 = ISC_PFE_CFG1_COLMIN(0u)
                           | ISC_PFE_CFG1_COLMAX((uint32_t)width - 1u);
    ISC_REGS->ISC_PFE_CFG2 = ISC_PFE_CFG2_ROWMIN(0u)
                           | ISC_PFE_CFG2_ROWMAX((uint32_t)height - 1u);
    ISC_REGS->ISC_PFE_CFG0 |= ISC_PFE_CFG0_COLEN_1 | ISC_PFE_CFG0_ROWEN_1;

    /* Override ISC DMA config:
     *  - IMODE=PACKED32: store RLP's 32-bit output verbatim (see file-top
     *    comment for full pipeline rationale).
     *  - YMBSIZE/CMBSIZE=BEATS32: sama7g5's full AXI4 burst size per
     *    Linux mchp-isc driver (MCC default BEATS8 is sama5d2-era). */
    ISC_REGS->ISC_DCFG = ISC_DCFG_IMODE_PACKED32
                       | ISC_DCFG_YMBSIZE_BEATS32
                       | ISC_DCFG_CMBSIZE_BEATS32;

    printf("ISC_Capture: configured %lux%lu BGRX32 (%lu bytes/frame)\r\n",
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

uint32_t ISC_Capture_GetBufferAddress(void)
{
    return (uint32_t)(uintptr_t)g_framebuffer;
}

