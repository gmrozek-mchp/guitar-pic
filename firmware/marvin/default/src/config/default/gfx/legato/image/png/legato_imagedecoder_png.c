// DOM-IGNORE-BEGIN
/*******************************************************************************
* Copyright (C) 2020 Microchip Technology Inc. and its subsidiaries.
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


#include "gfx/legato/image/png/legato_imagedecoder_png.h"

#if LE_ENABLE_PNG_DECODER == 1

#include "gfx/legato/common/legato_math.h"
#include "gfx/legato/core/legato_state.h"
#include "gfx/legato/core/legato_stream.h"
#include "gfx/legato/image/legato_palette.h"
#include "gfx/legato/memory/legato_memory.h"
#include "gfx/legato/renderer/legato_renderer.h"

#include "gfx/legato/image/png/lodepng.h"

static LE_COHERENT_ATTR leImageDecoder decoder;

static LE_COHERENT_ATTR struct leImage decodedImage;

static leBool _supportsImage(const leImage* img)
{
#if LE_STREAMING_ENABLED == 0
    if(img->header.location != LE_STREAM_LOCATION_ID_INTERNAL)
        return LE_FALSE;
#endif

    return img->format == LE_IMAGE_FORMAT_PNG;
}

/* Decode `src` (a PNG leImage) into the file-scope `decodedImage` as a RAW leImage
 * over freshly-malloc'd pixels, byte-swapped from lodepng's [R,G,B,A] output to
 * Legato's channel order for src->buffer.mode. On success returns LE_SUCCESS and
 * hands the malloc'd buffer back via *outData (caller must LE_FREE it under the
 * LE_PNG_USE_SCRATCH==0 policy); on failure returns LE_FAILURE (nothing to free). */
static leResult _pngDecodeToImage(const leImage* src, uint8_t** outData)
{
    uint8_t* encodedData = NULL;
    uint8_t* decodedData = NULL;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t itr, clr;
    uint8_t  rSwap, gSwap, bSwap, aSwap;
    uint8_t* ptr;
    int32_t  pngError;
#if LE_STREAMING_ENABLED == 1
    leStream stream;
#endif

    *outData = NULL;

#if LE_STREAMING_ENABLED == 1
    if(src->header.location != LE_STREAM_LOCATION_ID_INTERNAL)
    {
        encodedData = LE_MALLOC(src->header.size);
        if(encodedData == NULL)
            return LE_FAILURE;
        leStream_Init(&stream, &src->header, 0, NULL, NULL);
        if(leStream_Open(&stream) == LE_FAILURE)
        {
            LE_FREE(encodedData);
            return LE_FAILURE;
        }
        stream.flags |= SF_BLOCKING;
        if(leStream_Read(&stream, (size_t)src->header.address, src->header.size,
                         encodedData, NULL) == LE_FAILURE)
        {
            LE_FREE(encodedData);
            return LE_FAILURE;
        }
    }
    else
#endif
    {
        encodedData = src->header.address;
    }

    pngError = lodepng_decode_memory(&decodedData,
                                     (unsigned int*)&width,
                                     (unsigned int*)&height,
                                     encodedData,
                                     src->header.size,
                                     src->buffer.mode == LE_COLOR_MODE_RGBA_8888 ? LCT_RGBA : LCT_RGB,
                                     8);

#if LE_STREAMING_ENABLED == 1
    if(src->header.location != LE_STREAM_LOCATION_ID_INTERNAL)
        LE_FREE(encodedData);
#endif

    if(pngError != 0 || decodedData == NULL)
        return LE_FAILURE;

    leImage_Create(&decodedImage,
                   width,
                   height,
                   src->buffer.mode,
                   decodedData,
                   LE_STREAM_LOCATION_ID_INTERNAL);

    /* lodepng emits [R,G,B,A] byte order; swap to Legato's channel order. */
    if(decodedImage.buffer.mode == LE_COLOR_MODE_RGBA_8888)
    {
        for(itr = 0; itr < decodedImage.buffer.pixel_count; ++itr)
        {
            clr = ((uint32_t*) decodedImage.buffer.pixels)[itr];
            rSwap = (clr >> 24) & 0xFF;
            gSwap = (clr >> 16) & 0xFF;
            bSwap = (clr >>  8) & 0xFF;
            aSwap = (clr >>  0) & 0xFF;
            ((uint32_t*) decodedImage.buffer.pixels)[itr] =
                ((uint32_t)aSwap << 24) | ((uint32_t)bSwap << 16) |
                ((uint32_t)gSwap <<  8) | ((uint32_t)rSwap <<  0);
        }
    }
    else if(decodedImage.buffer.mode == LE_COLOR_MODE_RGB_888)
    {
        for(itr = 0; itr < decodedImage.buffer.pixel_count; ++itr)
        {
            ptr = &((uint8_t*)decodedImage.buffer.pixels)[itr * 3];
            clr = 0;
            memcpy(&clr, ptr, 3);
            clr = leColorSwap(clr, decodedImage.buffer.mode);
            memcpy(ptr, &clr, 3);
        }
    }

    *outData = decodedData;
    return LE_SUCCESS;
}

static leResult _draw(const leImage* img,
                      const leRect* srcRect,
                      int32_t x,
                      int32_t y,
                      uint32_t a)
{
    leRect   imgRect, sourceClipRect;
    uint8_t* decodedData;

    imgRect.x = 0;
    imgRect.y = 0;
    imgRect.width = img->buffer.size.width;
    imgRect.height = img->buffer.size.height;

    /* make sure the source rect is within the source bounds */
    if(leRectIntersects(&imgRect, srcRect) == LE_FALSE)
        return LE_FAILURE;

    leRectClip(&imgRect, srcRect, &sourceClipRect);

    if(sourceClipRect.width <= 0 || sourceClipRect.height <= 0)
        return LE_FAILURE;

    if(_pngDecodeToImage(img, &decodedData) != LE_SUCCESS)
        return LE_FAILURE;

    /* draw the decoded image to the active frame buffer */
    leImage_Draw(&decodedImage, srcRect, x, y, a);

#if defined LE_PNG_USE_SCRATCH && LE_PNG_USE_SCRATCH == 0
    LE_FREE(decodedData);
#endif

    return LE_SUCCESS;
}

static leResult _render(const leImage* src,
                        const leRect* srcRect,
                        int32_t x,
                        int32_t y,
                        leBool ignoreMask,
                        leBool ignoreAlpha,
                        leImage* dst)
{
    /* Decodes into the shared decodedImage, then copies it into the destination
     * buffer (vs _draw(), which draws it to the active frame buffer). */
    leRect   imgRect, sourceClipRect;
    uint8_t* decodedData;
    (void)ignoreMask;   // unused
    (void)ignoreAlpha;  // unused

    imgRect.x = 0;
    imgRect.y = 0;
    imgRect.width = src->buffer.size.width;
    imgRect.height = src->buffer.size.height;

    /* make sure the source rect is within the source bounds */
    if(leRectIntersects(&imgRect, srcRect) == LE_FALSE)
        return LE_FAILURE;

    leRectClip(&imgRect, srcRect, &sourceClipRect);

    if(sourceClipRect.width <= 0 || sourceClipRect.height <= 0)
        return LE_FAILURE;

    if(_pngDecodeToImage(src, &decodedData) != LE_SUCCESS)
        return LE_FAILURE;

    /* copy the decoded image into the destination buffer (color-converted by
     * lePixelBufferCopy if dst->buffer.mode differs from the decoded mode) */
    lePixelBufferCopy(&dst->buffer, x, y, &decodedImage.buffer, srcRect);

#if defined LE_PNG_USE_SCRATCH && LE_PNG_USE_SCRATCH == 0
    LE_FREE(decodedData);
#endif

    return LE_SUCCESS;
}

static void _decoderCleanup(void)
{
}

static leResult _decoderExec(void)
{
    return LE_SUCCESS;
}

static leBool _decoderIsDone(void)
{
    return LE_TRUE;
}

leImageDecoder* _lePNGImageDecoder_Init(void)
{
    decoder.supportsImage = _supportsImage;
    decoder.draw = _draw;
    decoder.copy = NULL;
    decoder.render = _render;
    decoder.resize = NULL;
    decoder.resizeDraw = NULL;
    decoder.rotate = NULL;
    decoder.rotateDraw = NULL;
    decoder.exec = _decoderExec;
    decoder.isDone = _decoderIsDone;
    decoder.free = _decoderCleanup;

    return &decoder;
}

#endif /* LE_ENABLE_PNG_DECODER */
