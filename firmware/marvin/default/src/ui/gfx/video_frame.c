#include "ui/gfx/video_frame.h"

#include <math.h>

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/* Signed distance to the rounded-rect boundary (negative inside), iq's rounded-box
 * SDF, for pixel-centre (x+0.5, y+0.5) in a WxH rect with corner radius R. */
static float rrect_sdf(int x, int y, float w, float h, float r)
{
    float px = (float)x + 0.5f - w * 0.5f;
    float py = (float)y + 0.5f - h * 0.5f;
    float qx = fabsf(px) - (w * 0.5f - r);
    float qy = fabsf(py) - (h * 0.5f - r);
    float ox = qx > 0.0f ? qx : 0.0f;
    float oy = qy > 0.0f ? qy : 0.0f;
    float outside = sqrtf(ox * ox + oy * oy);
    float inside  = fminf(fmaxf(qx, qy), 0.0f);
    return outside + inside - r;
}

/* For each pixel: outer coverage Co (inside the outer rounded-rect boundary) and
 * inner coverage Ci (inside the boundary inset by the stroke). alpha = 1-Ci (opaque
 * over the cut+stroke, transparent over the video); the opaque colour is the stroke
 * scaled by its share of the opaque part (the rest is the black cut). */
void VideoFrame_Fill(uint16_t *buf, uint32_t w, uint32_t h,
                     float radius, float stroke, uint32_t c4)
{
    const float fw = (float)w;
    const float fh = (float)h;

    for (uint32_t y = 0u; y < h; y++)
    {
        for (uint32_t x = 0u; x < w; x++)
        {
            float d  = rrect_sdf((int)x, (int)y, fw, fh, radius);
            float Co = clamp01(0.5f - d);
            float Ci = clamp01(0.5f - d - stroke);
            float a  = 1.0f - Ci;

            uint16_t px;
            if (a <= 0.0f)
            {
                px = 0x0000u;                       /* transparent — video shows */
            }
            else
            {
                float stroke_frac = (Co - Ci) / a;  /* opaque part that is stroke */
                uint32_t sc = (uint32_t)((float)c4 * stroke_frac + 0.5f);
                uint32_t a4 = (uint32_t)(a * 15.0f + 0.5f);
                if (sc > 15u) { sc = 15u; }
                if (a4 > 15u) { a4 = 15u; }
                px = (uint16_t)((a4 << 12) | (sc << 8) | (sc << 4) | sc);
            }
            buf[y * w + x] = px;
        }
    }
}
