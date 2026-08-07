/* Host driver for the firmware's QR rasterizer: renders one string with
 * QrRaster_Render and writes the tile as a binary PPM for verify.py to decode.
 *
 * Links ui/gfx/qr_raster.c and third_party/qrcodegen/qrcodegen.c unchanged — the point
 * of the test is that the code under test is the code that runs on the panel. */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "ui/gfx/qr_raster.h"

static uint16_t s_px[QR_RASTER_W * QR_RASTER_H];

/* RGB565 -> RGB888 with the low bits replicated, matching how the LCDC expands a
 * 565 layer. Exact for 0x0000 and 0xFFFF, which is all this tile should contain. */
static void expand(uint16_t v, unsigned char rgb[3])
{
    unsigned r = (v >> 11) & 0x1Fu;
    unsigned g = (v >> 5)  & 0x3Fu;
    unsigned b =  v        & 0x1Fu;

    rgb[0] = (unsigned char)((r << 3) | (r >> 2));
    rgb[1] = (unsigned char)((g << 2) | (g >> 4));
    rgb[2] = (unsigned char)((b << 3) | (b >> 2));
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        (void)fprintf(stderr, "usage: qr_dump <text> <out.ppm>\n");
        return 2;
    }

    if (!QrRaster_Render(argv[1], s_px, QR_RASTER_W))
    {
        (void)fprintf(stderr, "QrRaster_Render failed for '%s' (%u bytes)\n",
                      argv[1], (unsigned)strlen(argv[1]));
        return 1;
    }

    FILE *f = fopen(argv[2], "wb");
    if (f == NULL) { (void)fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }

    (void)fprintf(f, "P6\n%u %u\n255\n", (unsigned)QR_RASTER_W, (unsigned)QR_RASTER_H);

    for (unsigned i = 0u; i < (unsigned)(QR_RASTER_W * QR_RASTER_H); i++)
    {
        unsigned char rgb[3];
        expand(s_px[i], rgb);
        (void)fwrite(rgb, 1u, 3u, f);
    }

    (void)fclose(f);
    return 0;
}
