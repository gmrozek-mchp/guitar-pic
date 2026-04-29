/*******************************************************************************
  MPLAB Harmony XLCDC Generated Driver Implementation File

  File Name:
    drv_gfx_xlcdc.c

  Summary:
    Build-time generated implementation for the XLCDC Driver for SAM9X7/SAMA7D MPUs.

  Description:
    Contains function definitions and the data types that make up the interface to the XLCDC
    Graphics Controller for SAM9X7/SAMA7D MPUs.

    Created with MPLAB Harmony Version 3.0
*******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
* Copyright (C) 2023 Microchip Technology Inc. and its subsidiaries.
*
* Subject to your compliance with these terms, you may use Microchip software
* and any derivatives exclusively with Microchip products. It is your
* responsibility to comply with third party license terms applicable to your
* use of third party software (including open source software) that may
* accompany Microchip software.
*
* THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
* EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
* WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
* PARTICULAR PURPOSE.
*
* IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
* INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
* WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
* BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
* FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
* ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
* THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
*******************************************************************************/
// DOM-IGNORE-END

#include "toolchain_specifics.h"
#include "gfx/driver/gfx_driver.h"
#include "gfx/driver/controller/xlcdc/drv_gfx_xlcdc.h"
#include "gfx/driver/controller/xlcdc/plib/plib_xlcdc.h"

/* Utility Macros */
/* Math */
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define ABS(x) ((x) >= 0 ? (x) : -(x))
/* Alignment Check */
#define IS_ALIGNED(ptr, align) (((uintptr_t)(ptr) & ((align) - 1)) == 0)
/* Frame Buffer Macros */
/* Cached, Cache Aligned */
#define FB_CACHE_CA             CACHE_ALIGN
/* Not Cached */
#define FB_CACHE_NC             __attribute__ ((section(".region_nocache"), aligned (32)))
/* Frame Buffer Pointer Type */
#define FB_COL_MODE             XLCDC_RGB_COLOR_MODE_RGBA_8888
#define FB_BPP_TYPE             uint32_t
#define FB_PTR_TYPE             FB_BPP_TYPE *
#define FB_TYPE_SZ              sizeof(FB_BPP_TYPE)

/* Driver Settings */
#define XLCDC_HOR_RES           800
#define XLCDC_VER_RES           480
#define XLCDC_TOT_LAYERS        1
#define XLCDC_BUF_PER_LAYER     1

/* Local Data */
/* Driver */
typedef enum
{
    INIT = 0,
    DRAW
} DRV_STATE;

/* Generated Layer Order */
static const char layerOrder[XLCDC_TOT_LAYERS] = {
    XLCDC_LAYER_BASE,
};

const char *DRIVER_NAME = "XLCDC";
static volatile DRV_STATE state;
static volatile uint32_t vsyncCount = 0;
static volatile bool vblankSync = false;

/* Layer */
typedef enum
{
    LAYER_LOCK_UNLOCKED,
    LAYER_LOCK_LOCKED,
    LAYER_LOCK_PENDING,
} LAYER_LOCK_STATUS;

typedef struct
{
    bool enabled;
    int pixelformat;
    uint32_t resx;
    uint32_t resy;
    uint32_t startx;
    uint32_t starty;
    uint32_t sizex;
    uint32_t sizey;
    uint32_t alpha;
    FB_PTR_TYPE baseaddr[XLCDC_BUF_PER_LAYER];
    gfxPixelBuffer pixelBuffer[XLCDC_BUF_PER_LAYER];
    volatile uint32_t frontBufferIdx;
    volatile LAYER_LOCK_STATUS updateLock;
} LAYER_ATTRIBUTES;

static uint32_t activeLayer = 0;
static gfxRect srcRect, destRect;
static LAYER_ATTRIBUTES drvLayer[XLCDC_TOT_LAYERS];

/* Layer Frame Buffers */
FB_BPP_TYPE FB_CACHE_NC frame_buffer [XLCDC_TOT_LAYERS * XLCDC_BUF_PER_LAYER][XLCDC_HOR_RES * XLCDC_VER_RES];

/* Local Functions */
/* Convert XLCDC Color Mode to GFX Color Mode */
static gfxColorMode DRV_XLCDC_ColorModeGFXFromXLCDC(XLCDC_RGB_COLOR_MODE mode)
{
    switch(mode)
    {
        case XLCDC_RGB_COLOR_MODE_CLUT:
            return GFX_COLOR_MODE_GS_8;
        case XLCDC_RGB_COLOR_MODE_RGB_565:
            return GFX_COLOR_MODE_RGB_565;
        case XLCDC_RGB_COLOR_MODE_RGB_888:
            return GFX_COLOR_MODE_RGB_888;
        case XLCDC_RGB_COLOR_MODE_ARGB_8888:
            return GFX_COLOR_MODE_ARGB_8888;
        case XLCDC_RGB_COLOR_MODE_RGBA_8888:
            return GFX_COLOR_MODE_RGBA_8888;
        default:
            return GFX_COLOR_MODE_RGBA_8888;
    }
}

/* Frame Buffer specific fill */
static void DRV_XLCDC_ColorSet (void * fb)
{
    register FB_BPP_TYPE *ptr = (FB_PTR_TYPE)fb;
    register uint32_t size = XLCDC_HOR_RES * XLCDC_VER_RES;
    while(size-- > 0) *ptr++ = 0x0;
}

/* Perform a CPU based Blit */
static gfxResult DRV_XLCDC_CPU_Blit(const gfxPixelBuffer* restrict source,
                                   const gfxRect* restrict rectSrc,
                                   const gfxPixelBuffer* restrict dest,
                                   const gfxRect* restrict rectDest)
{
    if (!source || !rectSrc || !dest || !rectDest)
        return GFX_FAILURE;

    // Calculate dimensions
    const uint32_t width = MIN(rectSrc->width, rectDest->width);
    const uint32_t height = MIN(rectSrc->height, rectDest->height);

    if (width == 0 || height == 0)
        return GFX_FAILURE;

    // Calculate row size in bytes
    const uint32_t pixelSize = gfxColorInfoTable[dest->mode].size;
    const uint32_t rowSize = width * pixelSize;

    // Calculate source and destination strides based on buffer widths
    const uint32_t srcStride = source->size.width * pixelSize;
    const uint32_t destStride = dest->size.width * pixelSize;

    uint8_t* restrict srcBase = (uint8_t*)gfxPixelBufferOffsetGet(source, rectSrc->x, rectSrc->y);
    uint8_t* restrict destBase = (uint8_t*)gfxPixelBufferOffsetGet(dest, rectDest->x, rectDest->y);

    // Check if we can do a single large transfer i.e. we have contiguous data
    if (width == source->size.width && width == dest->size.width)
    {
        const uint32_t totalSize = rowSize * height;
        memcpy(destBase, srcBase, totalSize);

        return GFX_SUCCESS;
    }

    // Row by row processing for non-contiguous data
    for (uint32_t row = 0; row < height; row++)
    {
        uint8_t* restrict src = srcBase + row * srcStride;
        uint8_t* restrict dst = destBase + row * destStride;
        memcpy(dst, src, rowSize);
    }

    return GFX_SUCCESS;
}

void DRV_XLCDC_Update(void)
{
    switch(state)
    {
        case INIT:
        {
            state = DRAW;
            break;
        }
        case DRAW:
        default:
            break;
    }
}

gfxResult DRV_XLCDC_Initialize(void)
{
    /* Clear the Layer Attributes */
    memset(drvLayer, 0, sizeof(drvLayer));

    /* Initialize Layer Attributes */
    for (uint32_t layerCount = 0; layerCount < XLCDC_TOT_LAYERS; layerCount++)
    {
        drvLayer[layerCount].pixelformat = FB_COL_MODE;
        drvLayer[layerCount].resx = XLCDC_HOR_RES;
        drvLayer[layerCount].resy = XLCDC_VER_RES;
        drvLayer[layerCount].startx = 0;
        drvLayer[layerCount].starty = 0;
        drvLayer[layerCount].sizex = drvLayer[layerCount].resx;
        drvLayer[layerCount].sizey = drvLayer[layerCount].resy;
        drvLayer[layerCount].alpha = 255;
        drvLayer[layerCount].enabled = true;
        drvLayer[layerCount].updateLock = LAYER_LOCK_UNLOCKED;
        drvLayer[layerCount].frontBufferIdx = 0;

        for (uint32_t bufferCount = 0; bufferCount < XLCDC_BUF_PER_LAYER; ++bufferCount)
        {
            drvLayer[layerCount].baseaddr[bufferCount] = (FB_PTR_TYPE)frame_buffer[layerCount + bufferCount * XLCDC_TOT_LAYERS];

            DRV_XLCDC_ColorSet(drvLayer[layerCount].baseaddr[bufferCount]);

            gfxPixelBufferCreate(XLCDC_HOR_RES,
                                 XLCDC_VER_RES,
                                 DRV_XLCDC_ColorModeGFXFromXLCDC(drvLayer[layerCount].pixelformat),
                                 drvLayer[layerCount].baseaddr[bufferCount],
                                 &drvLayer[layerCount].pixelBuffer[bufferCount]);
        }

        XLCDC_SetLayerEnable(layerOrder[layerCount], false, true);
        XLCDC_SetLayerAddress(layerOrder[layerCount], (uint32_t) drvLayer[layerCount].baseaddr[drvLayer[layerCount].frontBufferIdx], false);
        XLCDC_SetLayerOpts(layerOrder[layerCount], 255, true, false);
        XLCDC_SetLayerWindowXYPos(layerOrder[layerCount], 0, 0, false);
        XLCDC_SetLayerWindowXYSize(layerOrder[layerCount], XLCDC_HOR_RES, XLCDC_VER_RES, false);
        XLCDC_SetLayerEnable(layerOrder[layerCount], drvLayer[layerCount].enabled, true);
    }

    return GFX_SUCCESS;
}

gfxResult DRV_XLCDC_BlitBuffer(int32_t x, int32_t y, gfxPixelBuffer* buf)
{
    gfxResult result = GFX_FAILURE;

    if (state != DRAW)
        return result;

    gfxPixelBuffer_SetLocked(buf, GFX_TRUE);

    srcRect.x = 0;
    srcRect.y = 0;
    srcRect.height = buf->size.height;
    srcRect.width = buf->size.width;

    destRect.x = x;
    destRect.y = y;
    destRect.height = buf->size.height;
    destRect.width = buf->size.width;

    result = DRV_XLCDC_CPU_Blit(buf,
                                &srcRect,
                                &drvLayer[activeLayer].pixelBuffer[drvLayer[activeLayer].frontBufferIdx],
                                &destRect);

    gfxPixelBuffer_SetLocked(buf, GFX_FALSE);

    return result;
}

gfxDriverIOCTLResponse DRV_XLCDC_IOCTL(gfxDriverIOCTLRequest request, void* arg)
{
    gfxIOCTLArg_Value *val;
    gfxIOCTLArg_DisplaySize *disp;
    gfxIOCTLArg_LayerRect *rect;

    switch (request)
    {
        case GFX_IOCTL_LAYER_SWAP:
        {
            return GFX_IOCTL_OK;
        }

        case GFX_IOCTL_FRAME_END:
        {
            return GFX_IOCTL_OK;
        }

        case GFX_IOCTL_GET_BUFFER_COUNT:
        {
            val = (gfxIOCTLArg_Value *)arg;

            val->value.v_uint = XLCDC_BUF_PER_LAYER;

            return GFX_IOCTL_OK;
        }

        case GFX_IOCTL_GET_DISPLAY_SIZE:
        {
            disp = (gfxIOCTLArg_DisplaySize *)arg;

            disp->width = XLCDC_HOR_RES;
            disp->height = XLCDC_VER_RES;

            return GFX_IOCTL_OK;
        }

        case GFX_IOCTL_GET_LAYER_COUNT:
        {
            val = (gfxIOCTLArg_Value *)arg;

            val->value.v_uint = XLCDC_TOT_LAYERS;

            return GFX_IOCTL_OK;
        }

        case GFX_IOCTL_GET_ACTIVE_LAYER:
        {
            val = (gfxIOCTLArg_Value *)arg;

            val->value.v_uint = activeLayer;

            return GFX_IOCTL_OK;
        }

        case GFX_IOCTL_SET_ACTIVE_LAYER:
        {
            gfxDriverIOCTLResponse response = GFX_IOCTL_OK;

            val = (gfxIOCTLArg_Value *)arg;

            if (val->value.v_uint >= XLCDC_TOT_LAYERS)
            {
                response =  GFX_IOCTL_ERROR_UNKNOWN;
            }
            else
            {
                activeLayer = val->value.v_uint;
            }

            return response;
        }

        case GFX_IOCTL_GET_LAYER_RECT:
        {
            rect = (gfxIOCTLArg_LayerRect *)arg;

            if (rect->layer.id >= XLCDC_TOT_LAYERS)
            {
                return GFX_IOCTL_ERROR_UNKNOWN;
            }

            rect->x = drvLayer[rect->layer.id].startx;
            rect->y = drvLayer[rect->layer.id].starty;
            rect->width = drvLayer[rect->layer.id].sizex;
            rect->height = drvLayer[rect->layer.id].sizey;

            return GFX_IOCTL_OK;
        }

        case GFX_IOCTL_GET_VSYNC_COUNT:
        {
            val = (gfxIOCTLArg_Value *)arg;

            val->value.v_uint = vsyncCount;

            return GFX_IOCTL_OK;
        }

        case GFX_IOCTL_GET_FRAMEBUFFER:
        {
            val = (gfxIOCTLArg_Value *)arg;

            val->value.v_pbuffer = &drvLayer[activeLayer].pixelBuffer[drvLayer[activeLayer].frontBufferIdx];

            return GFX_IOCTL_OK;
        }

        case GFX_IOCTL_SET_PALETTE:
        {
            return GFX_IOCTL_UNSUPPORTED;
        }

        case GFX_IOCTL_SET_VBLANK_SYNC:
        {
            vblankSync = ((gfxIOCTLArg_Value *)arg)->value.v_bool;

            return GFX_IOCTL_OK;
        }

        case GFX_IOCTL_GET_STATUS:
        {
            val = (gfxIOCTLArg_Value *)arg;
            val->value.v_uint = 0;

            return GFX_IOCTL_OK;
        }

        default:
        {
                break;
        }
    }

    return GFX_IOCTL_UNSUPPORTED;
}
