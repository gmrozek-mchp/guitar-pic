# Bug: Legato PNG image decoder `_render()` produces garbage (image-to-image / `leProcessImage` path)

**Component:** MPLAB Harmony Graphics (Legato) — `gfx` **v3.17.0**
**File:** `gfx/legato/image/png/legato_imagedecoder_png.c`
**Function:** `_render()` (the PNG decoder's `leImageDecoder.render` callback)
**Affected public APIs:** `leImage_Render()`, `leImage_Copy()` (render-into-image path), and therefore `leProcessImage()` (which calls `leImage_Render`)
**Config at repro:** `LE_STREAMING_ENABLED = 0`, `LE_PNG_USE_SCRATCH = 0`, `LE_ENABLE_PNG_DECODER = 1`
**Target:** SAM9X75 (Cortex-A5), but the defect is platform-independent (pure logic in the decoder).
**Severity:** High — any attempt to decode a PNG into a destination buffer (rather than draw it directly to the active layer) yields garbage.

---

## Summary

The PNG decoder's `_render()` (used to decode a PNG `leImage` **into another `leImage`/buffer**, e.g. for `leProcessImage()` to pre-decode an asset into a GPU-friendly raster) does **not** use the freshly-decoded pixels. It decodes correctly into `decodedData`, then:

1. **ignores** the `lodepng_decode_memory()` return value (no error check),
2. **omits** the lodepng→Legato channel byte-swap that the sibling `_draw()` performs, and
3. **copies from `src->buffer`** — i.e. the **compressed PNG input** — instead of from a buffer wrapping `decodedData`.

The net effect is that the destination receives the compressed byte stream (and adjacent memory) interpreted as pixels → **noise**. The decoded data is computed and then discarded (and leaked when `LE_PNG_USE_SCRATCH == 0`, since it is only freed at the end after never being used).

The on-paint path, `_draw()`, in the **same file** is correct, which is why `leImageWidget` PNGs display fine and the defect is easy to miss — it only affects the decode-to-buffer path.

---

## Offending code (`_render`, abridged to the relevant tail)

```c
    lodepng_decode_memory(&decodedData,                 /* (1) return value ignored      */
                          (unsigned int*)&width,
                          (unsigned int*)&height,
                          encodedData,
                          src->header.size,
                          src->buffer.mode == LE_COLOR_MODE_RGBA_8888 ? LCT_RGBA : LCT_RGB,
                          8);

    /* ... (streaming free of encodedData) ... */

    lePixelBufferCopy(&dst->buffer, x, y, &src->buffer, srcRect);   /* (3) copies COMPRESSED src->buffer,
                                                                       and (2) no channel byte-swap        */
#if defined LE_PNG_USE_SCRATCH && LE_PNG_USE_SCRATCH == 0
    LE_FREE(decodedData);                               /* decodedData decoded but never used (leak)       */
#endif
    return LE_SUCCESS;
```

`src->buffer.pixels` points at the **compressed PNG bytes** (`src->header.address`), not the decoded raster. `decodedData` — the actual decoded output from `lodepng_decode_memory` — is never referenced by the copy.

---

## Root cause — compare with the correct `_draw()` (same file)

`_draw()` does it right:

```c
    pngError = lodepng_decode_memory(&decodedData, &width, &height,
                                     encodedData, img->header.size,
                                     img->buffer.mode == LE_COLOR_MODE_RGBA_8888 ? LCT_RGBA : LCT_RGB, 8);
    if(pngError != 0)
        return LE_FAILURE;                                   /* (1) checks the error            */

    leImage_Create(&decodedImage, width, height, img->buffer.mode,
                   decodedData, LE_STREAM_LOCATION_ID_INTERNAL); /* wrap the DECODED buffer       */

    if(decodedImage.buffer.mode == LE_COLOR_MODE_RGBA_8888) {   /* (2) byte-swap lodepng output  */
        for(itr = 0; itr < decodedImage.buffer.pixel_count; ++itr) {
            clr = ((uint32_t*)decodedImage.buffer.pixels)[itr];
            rSwap=(clr>>24)&0xFF; gSwap=(clr>>16)&0xFF; bSwap=(clr>>8)&0xFF; aSwap=(clr>>0)&0xFF;
            ((uint32_t*)decodedImage.buffer.pixels)[itr] =
                (aSwap<<24)|(bSwap<<16)|(gSwap<<8)|(rSwap<<0);
        }
    } else if(decodedImage.buffer.mode == LE_COLOR_MODE_RGB_888) {
        /* per-pixel leColorSwap(...) */
    }

    leImage_Draw(&decodedImage, srcRect, x, y, a);             /* (3) draws the DECODED buffer    */
    LE_FREE(decodedData);
```

`_render()` is missing all three of these steps. lodepng emits channels in `[R,G,B,A]` byte order; Legato's `LE_COLOR_MODE_RGBA_8888` expects the reversed in-memory order, which is exactly what the `_draw()` swap loop produces. `_render()` skips it (and never even reaches the right source buffer).

---

## Proposed fix

The decode + byte-swap was duplicated in `_draw()` and `_render()`, and the `_render()` copy drifted. The root-cause fix is to **factor the shared decode into one helper** so the two paths can't diverge again — `_draw()` then *draws* the result, `_render()` *copies* it into the destination:

```c
/* Decode `src` (a PNG leImage) into the file-scope `decodedImage` as a RAW leImage
 * over freshly-malloc'd pixels, byte-swapped from lodepng's [R,G,B,A] output to
 * Legato's channel order. Returns LE_SUCCESS and hands the buffer back via *outData
 * (caller frees under the LE_PNG_USE_SCRATCH==0 policy); LE_FAILURE on error. */
static leResult _pngDecodeToImage(const leImage* src, uint8_t** outData)
{
    uint8_t *encodedData = NULL, *decodedData = NULL, *ptr;
    uint32_t width = 0, height = 0, itr, clr;
    uint8_t  rSwap, gSwap, bSwap, aSwap;
    int32_t  pngError;
#if LE_STREAMING_ENABLED == 1
    leStream stream;
#endif
    *outData = NULL;

#if LE_STREAMING_ENABLED == 1
    if(src->header.location != LE_STREAM_LOCATION_ID_INTERNAL) {
        encodedData = LE_MALLOC(src->header.size);
        if(encodedData == NULL) return LE_FAILURE;
        leStream_Init(&stream, &src->header, 0, NULL, NULL);
        if(leStream_Open(&stream) == LE_FAILURE) { LE_FREE(encodedData); return LE_FAILURE; }
        stream.flags |= SF_BLOCKING;
        if(leStream_Read(&stream, (size_t)src->header.address, src->header.size,
                         encodedData, NULL) == LE_FAILURE) { LE_FREE(encodedData); return LE_FAILURE; }
    } else
#endif
    { encodedData = src->header.address; }

    pngError = lodepng_decode_memory(&decodedData, (unsigned*)&width, (unsigned*)&height,
                 encodedData, src->header.size,
                 src->buffer.mode == LE_COLOR_MODE_RGBA_8888 ? LCT_RGBA : LCT_RGB, 8);

#if LE_STREAMING_ENABLED == 1
    if(src->header.location != LE_STREAM_LOCATION_ID_INTERNAL) LE_FREE(encodedData);
#endif

    if(pngError != 0 || decodedData == NULL) return LE_FAILURE;   /* (1) error checked */

    leImage_Create(&decodedImage, width, height, src->buffer.mode,
                   decodedData, LE_STREAM_LOCATION_ID_INTERNAL);

    if(decodedImage.buffer.mode == LE_COLOR_MODE_RGBA_8888) {      /* (2) byte-swap     */
        for(itr = 0; itr < decodedImage.buffer.pixel_count; ++itr) {
            clr = ((uint32_t*)decodedImage.buffer.pixels)[itr];
            rSwap=(clr>>24)&0xFF; gSwap=(clr>>16)&0xFF; bSwap=(clr>>8)&0xFF; aSwap=(clr>>0)&0xFF;
            ((uint32_t*)decodedImage.buffer.pixels)[itr] =
                ((uint32_t)aSwap<<24)|((uint32_t)bSwap<<16)|((uint32_t)gSwap<<8)|((uint32_t)rSwap<<0);
        }
    } else if(decodedImage.buffer.mode == LE_COLOR_MODE_RGB_888) {
        for(itr = 0; itr < decodedImage.buffer.pixel_count; ++itr) {
            ptr = &((uint8_t*)decodedImage.buffer.pixels)[itr*3];
            clr = 0; memcpy(&clr, ptr, 3); clr = leColorSwap(clr, decodedImage.buffer.mode); memcpy(ptr, &clr, 3);
        }
    }
    *outData = decodedData;
    return LE_SUCCESS;
}
```

`_draw()` and `_render()` become thin and share the decode — the only difference is the output op:

```c
static leResult _draw(const leImage* img, const leRect* srcRect, int32_t x, int32_t y, uint32_t a)
{
    /* ... srcRect bounds check (unchanged) ... */
    uint8_t* decodedData;
    if(_pngDecodeToImage(img, &decodedData) != LE_SUCCESS) return LE_FAILURE;
    leImage_Draw(&decodedImage, srcRect, x, y, a);                 /* draw to active frame buffer */
#if defined LE_PNG_USE_SCRATCH && LE_PNG_USE_SCRATCH == 0
    LE_FREE(decodedData);
#endif
    return LE_SUCCESS;
}

static leResult _render(const leImage* src, const leRect* srcRect, int32_t x, int32_t y,
                        leBool ignoreMask, leBool ignoreAlpha, leImage* dst)
{
    /* ... srcRect bounds check (unchanged) ... */
    uint8_t* decodedData;
    (void)ignoreMask; (void)ignoreAlpha;
    if(_pngDecodeToImage(src, &decodedData) != LE_SUCCESS) return LE_FAILURE;
    lePixelBufferCopy(&dst->buffer, x, y, &decodedImage.buffer, srcRect);  /* copy DECODED pixels into dst */
#if defined LE_PNG_USE_SCRATCH && LE_PNG_USE_SCRATCH == 0
    LE_FREE(decodedData);
#endif
    return LE_SUCCESS;
}
```

A minimal point-fix (without the refactor) is equivalent: in `_render()`, check `pngError`, byte-swap the decoded buffer as `_draw()` does, and change the copy source from `&src->buffer` to a buffer wrapping `decodedData`. The shared-helper form is preferred because the duplication is what allowed the divergence in the first place.

(Verified on hardware, gfx v3.17.0: with the shared-helper refactor, PNG `leImage_Render` / `leProcessImage` produce correct full-color output.)

---

## Reproduction

1. Enable the PNG decoder (`LE_ENABLE_PNG_DECODER = 1`), `LE_STREAMING_ENABLED = 0`.
2. Build a PNG source `leImage`: `format = LE_IMAGE_FORMAT_PNG`, `header.address` = pointer to the compressed PNG bytes in RAM, `header.size` = compressed byte count, `buffer.size` = the image WxH, `buffer.mode = LE_COLOR_MODE_RGBA_8888`.
3. Create a destination RAW `leImage` of the same WxH/`RGBA_8888` over an application buffer.
4. Call `leImage_Render(&pngSrc, &fullRect, 0, 0, LE_TRUE, LE_TRUE, &dst)` (or `leProcessImage(&pngSrc, (size_t)dstBuf, LE_COLOR_MODE_RGBA_8888)`).
5. **Observed:** `dst` contains noise (compressed data interpreted as pixels). **Expected:** the decoded image.
   - The same image via `leImage_Draw()` onto a layer renders correctly, confirming the decode itself works and the fault is isolated to `_render()`.

---

## Impact

- Any workflow that **pre-decodes a PNG into a buffer** (GPU-format staging via `leProcessImage`, image-to-image composition via `leImage_Render`/`leImage_Copy`) is broken for PNG.
- JPEG is unaffected — the JPEG decoder's `_render()` (`blitToImage` → `lePixelBufferSet(&dst->buffer, …)`) writes decoded pixels into `dst` correctly.

---

## Secondary observation (not the bug above; a memory-sizing note)

`lodepng_decode_memory()` allocates the full decoded raster **plus** the inflated/filtered scanline buffer from the Legato variable heap, peaking at roughly `W*H*(srcBytesPerPixel + dstBytesPerPixel)`. For a 508×208 image decoded to RGBA (≈ 740 KB peak), the default `LE_VARIABLEHEAP_SIZE = 524288` (512 KB) is insufficient — `lodepng_decode_memory` returns `83` (`LODEPNG_OUT_OF_MEMORY`) intermittently (heap fragmentation across repeated decode/free cycles tips some allocations over). Raising `LE_VARIABLEHEAP_SIZE` (we use 2 MB) resolves it. Consider documenting the per-decode transient-heap requirement, or decoding in a lower-peak manner.
