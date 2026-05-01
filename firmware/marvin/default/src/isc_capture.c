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
#define ISC_CAP_BPP          4u    /* ARGB32 */
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

    iscObj->inputFormat           = ISC_INPUT_FORMAT_TYPE;
    iscObj->inputBits             = ISC_INPUT_BIT_WIDTH;
    iscObj->rlpMode               = ISC_OUTPUT_FORMAT_TYPE;
    iscObj->layout                = ISC_OUTPUT_LAYOUT_TYPE;
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
    printf("  CSI2DC_GSR=0x%08lX  GISR=0x%08lX\r\n",
           (unsigned long)CSI2DC_REGS->CSI2DC_GSR,
           (unsigned long)CSI2DC_REGS->CSI2DC_GISR);
    printf("  CSI2DC_FNVC0R=0x%08lX  LNVC0R=0x%08lX\r\n",
           (unsigned long)CSI2DC_REGS->CSI2DC_FNVC0R,
           (unsigned long)CSI2DC_REGS->CSI2DC_LNVC0R);
    printf("  ISC_INTSR=0x%08lX\r\n",
           (unsigned long)ISC_Interrupt_Status());
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

    printf("ISC_Capture: configured %lux%lu ARGB32 (%lu bytes/frame)\r\n",
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
