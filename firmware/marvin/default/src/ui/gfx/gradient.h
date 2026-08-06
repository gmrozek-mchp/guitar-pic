#ifndef UI_GFX_GRADIENT_H
#define UI_GFX_GRADIENT_H

#include <stdint.h>

#include "gfx/legato/legato.h"   /* leRect, leColorMode */

#ifdef __cplusplus
extern "C" {
#endif

/* Horizontal gradient fill with ordered dithering.
 *
 * A ramp interpolated in 24-bit RGB and then merely truncated to the panel's RGB565
 * shows obvious banding: 5/6/5 bits give 32 levels per channel, so a neutral black →
 * white ramp across 718 px has ~22 px of identical pixels per step. Ordered (Bayer 8×8)
 * dithering trades that for a fine, static stipple the eye integrates into a smooth
 * ramp — the same reason it is used for gradients in 16-bit framebuffers generally.
 *
 * `rect` is the region to paint. The ramp's geometry is given separately as
 * `span_x` / `span_w` — the x and width of the FULL ramp in screen space — so painting
 * a sub-rect (a partly filled bar) yields exactly the slice of the ramp that belongs
 * there, rather than a compressed copy of the whole ramp.
 *
 * Endpoints are plain 24-bit `0xRRGGBB`, not `leColor`: interpolating after conversion
 * to the target mode would throw away the precision dithering needs. `mode` is the
 * active render mode (`leRenderer_CurrentColorMode()`); modes with 8 bits per channel
 * are filled without dithering, since they have nothing to gain.
 *
 * Cost is one PutPixel per pixel, so this is for fills that are painted rarely (a
 * static ramp, a progress bar on change) — not per frame. */
void Gradient_FillH(const leRect *rect, int32_t span_x, uint32_t span_w,
                    uint32_t rgb_from, uint32_t rgb_to, leColorMode mode);

#ifdef __cplusplus
}
#endif

#endif /* UI_GFX_GRADIENT_H */
