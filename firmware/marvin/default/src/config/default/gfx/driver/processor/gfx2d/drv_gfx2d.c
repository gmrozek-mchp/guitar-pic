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

/********************************************************************************
  GFX GFX2D Driver Functions

  Company:
    Microchip Technology Inc.

  File Name:
    drv_gfx2d.c

  Summary:
    Source code for the GFX GFX2D driver static implementation.

  Description:
    This file contains the source code for the static implementation of the
    GFX GFX2D driver.
*******************************************************************************/


// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include "gfx/driver/processor/gfx2d/drv_gfx2d.h"
#include "definitions.h"

// *****************************************************************************
// *****************************************************************************
// Section: Global Data
// *****************************************************************************
// *****************************************************************************

GFX2D_PIXEL_FORMAT gfx2dFormats[GFX_COLOR_MODE_LAST + 1] =
{
    -1, // GFX_COLOR_MODE_GS_8
    -1, // GFX_COLOR_MODE_RGB_332,
    GFX2D_RGB16, // GFX_COLOR_MODE_RGB_565
    -1, // GFX_COLOR_MODE_RGBA_5551
    -1, // GFX_COLOR_MODE_RGB_888
    GFX2D_ARGB32, // GFX_COLOR_MODE_RGBA_8888
    -1, // GFX_COLOR_MODE_ARGB_8888
    -1, // GFX_COLOR_MODE_INDEX_1
    -1, // GFX_COLOR_MODE_INDEX_4
    GFX2D_IDX8, //GFX_COLOR_MODE_INDEX_8
};

static  gfxBlend blendState = GFX_BLEND_NONE;

/**** End Hardware Abstraction Interfaces ****/

void DRV_GFX2D_Initialize()
{
    PLIB_GFX2D_Initialize();
    PLIB_GFX2D_SetOutstandingRegulationEnable(true);
    PLIB_GFX2D_SetQOSLatency(GFX2D_QOS_15_CYCLES,
                             GFX2D_QOS_31_CYCLES,
                             GFX2D_QOS_63_CYCLES);
    PLIB_GFX2D_Enable();
}

gfxResult DRV_GFX2D_Fill(gfxPixelBuffer* dest,
                           const gfxRect* clipRect,
                           const gfxColor color)
{
    GFX2D_BUFFER    dest_buffer;
    GFX2D_RECTANGLE dest_rect;
    gfxColor        fill_color;

    if(dest->orientation != GFX_ORIENT_0)
		return GFX_FAILURE;

    dest_buffer.width = dest->size.width;
    dest_buffer.height = dest->size.height;
    dest_buffer.format = gfx2dFormats[dest->mode];
    dest_buffer.dir = GFX2D_XY00;
    dest_buffer.addr = (uint32_t)dest->pixels;

    dest_rect.x = (uint32_t)clipRect->x;
    dest_rect.y = (uint32_t)clipRect->y;
    dest_rect.width = (uint32_t)clipRect->width;
    dest_rect.height = (uint32_t)clipRect->height;

    if (dest->mode == GFX_COLOR_MODE_RGB_565)
    {
        fill_color = gfxColorConvert(dest->mode, GFX_COLOR_MODE_ARGB_8888, color);
    }
    else
    {
        fill_color = color;
    }

    PLIB_GFX2D_Fill(&dest_buffer, &dest_rect, fill_color);

    while(PLIB_GFX2D_GetGlobalStatusBusy() == true);

    return GFX_SUCCESS;
}

/* MARVIN re-apply patch #14 — cache-maintain only the rows a blit touches.
 *
 * Stock DRV_GFX2D_Blit cleans+invalidates each buffer over its whole `buffer_length`,
 * which for a full-screen canvas is 1280*800*2 = 2,048,000 bytes = 64,000 cache lines,
 * every blit, regardless of the rect. Legato blits one damage rect per frame, so a 12x12
 * status-LED repaint paid the same toll as a full-screen one: measured 7.45 ms per frame
 * on this 800 MHz ARM926, which put ~8% of the CPU into a pulsing 12px dot.
 *
 * Cleaning the rect's rows instead is both correct and enough — the 2D engine only reads
 * the source rect and only writes the destination rect. Full-width rects (the common
 * Legato case, since its scratch buffer is sized to the damage) collapse to one
 * contiguous range, so nothing is lost on large blits.
 *
 * Ordering and clean+invalidate semantics are deliberately left exactly as stock: only
 * the address range changes. See the journal, 2026-08-07 (night). */
static void gfx2d_cache_rect(const gfxPixelBuffer* buf, const gfxRect* rect)
{
    uint32_t bpp = gfxColorInfoTable[buf->mode].size;
    uint8_t* base;
    int32_t  x0, y0, x1, y1, row;

    /* Clamped to the buffer, because the stock whole-buffer clean was immune to a rect
     * reaching past the end and a per-row one is not. Every in-tree caller passes a
     * clipped rect today; this keeps the patch safe for the ones that don't. */
    x0 = (rect->x > 0) ? rect->x : 0;
    y0 = (rect->y > 0) ? rect->y : 0;
    x1 = rect->x + rect->width;
    y1 = rect->y + rect->height;
    if (x1 > buf->size.width)  { x1 = buf->size.width;  }
    if (y1 > buf->size.height) { y1 = buf->size.height; }

    if (x1 <= x0 || y1 <= y0)
    {
        return;
    }

    /* Rows are contiguous when the rect spans the full buffer width — one range. */
    if (x0 == 0 && x1 == buf->size.width)
    {
        base = (uint8_t*)gfxPixelBufferOffsetGet_Unsafe(buf, 0, (uint32_t)y0);
        dcache_CleanInvalidateByAddr(base,
                                    (int32_t)((uint32_t)(x1 - x0) *
                                              (uint32_t)(y1 - y0) * bpp));
        return;
    }

    for (row = y0; row < y1; row++)
    {
        base = (uint8_t*)gfxPixelBufferOffsetGet_Unsafe(buf, (uint32_t)x0,
                                                        (uint32_t)row);
        dcache_CleanInvalidateByAddr(base, (int32_t)((uint32_t)(x1 - x0) * bpp));
    }
}

gfxResult DRV_GFX2D_Blit(const gfxPixelBuffer* source,
                           const gfxRect* srcRect,
                           const gfxPixelBuffer* dest,
                        const gfxRect* destRect)
{
    GFX2D_BUFFER    dest_buffer;
    GFX2D_RECTANGLE dest_rect;
    GFX2D_BUFFER    src_buffer;
    GFX2D_RECTANGLE src_rect;

    if(source->orientation != GFX_ORIENT_0 ||
       dest->orientation != GFX_ORIENT_0)
		return GFX_FAILURE;

    dest_buffer.width = dest->size.width;
    dest_buffer.height = dest->size.height;
    dest_buffer.format = gfx2dFormats[dest->mode];
    dest_buffer.dir = GFX2D_XY00;
    dest_buffer.addr = (uint32_t)dest->pixels;

    dest_rect.x = (uint32_t)destRect->x;
    dest_rect.y = (uint32_t)destRect->y;
    dest_rect.width = (uint32_t)destRect->width;
    dest_rect.height = (uint32_t)destRect->height;

    src_buffer.width = source->size.width;
    src_buffer.height = source->size.height;
    src_buffer.format = gfx2dFormats[source->mode];
    src_buffer.dir = GFX2D_XY00;
    src_buffer.addr = (uint32_t)source->pixels;

    src_rect.x = (uint32_t)srcRect->x;
    src_rect.y = (uint32_t)srcRect->y;
    src_rect.width = (uint32_t)srcRect->width;
    src_rect.height = (uint32_t)srcRect->height;

	/* MARVIN re-apply patch #14 — was:
	 *   dcache_CleanInvalidateByAddr(source->pixels, source->buffer_length);
	 *   dcache_CleanInvalidateByAddr(dest->pixels, dest->buffer_length);
	 * i.e. the whole buffer per blit. See gfx2d_cache_rect above. */
	gfx2d_cache_rect(source, srcRect);
	gfx2d_cache_rect(dest, destRect);

    if ( blendState == GFX_BLEND_NONE )
    {
        PLIB_GFX2D_Copy(&dest_buffer, &dest_rect, &src_buffer, &src_rect);
    }
    else
    {
        return GFX_FAILURE;
    }

    while(PLIB_GFX2D_GetGlobalStatusBusy() == true);

    return GFX_SUCCESS;
}

void  DRV_GFX2D_Blend(
    GFX2D_BUFFER *dest,
    GFX2D_RECTANGLE *dest_rect,
    GFX2D_BUFFER *src1,
    GFX2D_RECTANGLE *src1_rect,
    GFX2D_BUFFER *src2,
    GFX2D_RECTANGLE *src2_rect,
    GFX2D_BLEND blend)
{
    PLIB_GFX2D_Blend(dest, dest_rect, src1, src1_rect, src2, src2_rect, blend);

    while(PLIB_GFX2D_GetGlobalStatusBusy() == true);
}

void  DRV_GFX2D_Rop(
   GFX2D_BUFFER *dest,
   GFX2D_RECTANGLE *dest_rect,
   GFX2D_BUFFER *src1,
   GFX2D_RECTANGLE *src1_rect,
   GFX2D_BUFFER *src2,
   GFX2D_RECTANGLE *src2_rect,
   GFX2D_BUFFER *pmask,
   GFX2D_ROP rop)
{
    PLIB_GFX2D_Rop(dest, dest_rect, src1, src1_rect, src2, src2_rect, pmask, rop);

    while(PLIB_GFX2D_GetGlobalStatusBusy() == true);
}

gfxResult DRV_GFX2D_SetBlend(
                const gfxBlend blend)
{
    blendState = blend;

    return GFX_SUCCESS;
}

gfxResult DRV_GFX2D_SetGlobalAlpha(const gfxAlpha srcGlobalAlpha,
                                   const gfxAlpha dstGlobalAlpha,
                                   uint32_t srcGlobalAlphaValue,
                                   uint32_t dstGlobalAlphaValue)
{
    return GFX_SUCCESS;
}

gfxResult DRV_GFX2D_SetPalette(
                        uint32_t index_count,
                        gfxBuffer color_table,
                        gfxBool color_convert)
{
    return GFX_SUCCESS;
}

gfxResult DRV_GFX2D_SetTransparency(
                        gfxTransparency transparency,
                        gfxColor color,
                        uint32_t foreground_rop,
                        uint32_t background_rop)
{
    return GFX_SUCCESS;
}
/*******************************************************************************
 End of File
*/
