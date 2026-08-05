#ifndef UI_GFX_VIDEO_FRAME_H
#define UI_GFX_VIDEO_FRAME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fill an ARGB_4444 surface with a rounded anti-aliased frame to composite over the
 * live video (HEO). The surface is mostly transparent so the video shows through;
 * the four corners are cut to opaque black (matching the backdrop, so the video's
 * square corners read as rounded) and a `stroke`-px edge frames it. Per-pixel alpha
 * (4-bit → 16 levels) gives the corner and edge anti-aliasing.
 *
 * The stroke is drawn INSIDE the w x h rect (border-box), so it eats the outer
 * `stroke` pixels of the video window it is bound over — bind the overlay at the
 * same origin and size as the video window.
 *
 * `c4` is the stroke grey in 4-bit-per-channel form (0..15); 4 renders #444444,
 * which is the zinc-700 the mockups frame the video with.
 *
 * `buf` must hold w * h uint16_t and live in non-cached memory (the LCDC DMA reads
 * it). Fill once at setup — the compositor only binds/unbinds the layer to it. */
void VideoFrame_Fill(uint16_t *buf, uint32_t w, uint32_t h,
                     float radius, float stroke, uint32_t c4);

#ifdef __cplusplus
}
#endif

#endif /* UI_GFX_VIDEO_FRAME_H */
