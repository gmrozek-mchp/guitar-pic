#include "ui/gfx/qr_raster.h"

#include "qrcodegen.h"

#define QR_WHITE  0xFFFFu
#define QR_BLACK  0x0000u

/* Both qrcodegen buffers must be sized for the largest version the call may produce,
 * which here is the only version it may produce. 138 bytes each. */
#define QR_BUF_LEN  qrcodegen_BUFFER_LEN_FOR_VERSION(QR_RASTER_VERSION)

bool QrRaster_Render(const char *text, uint16_t *px, unsigned stride_px)
{
    uint8_t qr[QR_BUF_LEN];
    uint8_t tmp[QR_BUF_LEN];

    if ((text == NULL) || (text[0] == '\0') || (px == NULL) ||
        (stride_px < QR_RASTER_W))
    {
        return false;
    }

    /* min == max version: see the header. ECC M is the floor, boostEcl the ceiling. */
    if (!qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_MEDIUM,
                              (int)QR_RASTER_VERSION, (int)QR_RASTER_VERSION,
                              qrcodegen_Mask_AUTO, true))
    {
        return false;
    }

    int n = qrcodegen_getSize(qr);
    if (n != (int)QR_RASTER_MODULES) { return false; }

    for (unsigned y = 0u; y < QR_RASTER_H; y++)
    {
        uint16_t *row = &px[(size_t)y * stride_px];
        for (unsigned x = 0u; x < QR_RASTER_W; x++) { row[x] = QR_WHITE; }
    }

    for (int my = 0; my < n; my++)
    {
        for (int mx = 0; mx < n; mx++)
        {
            if (!qrcodegen_getModule(qr, mx, my)) { continue; }

            unsigned x0 = ((unsigned)mx + QR_RASTER_QUIET) * QR_RASTER_SCALE;
            unsigned y0 = ((unsigned)my + QR_RASTER_QUIET) * QR_RASTER_SCALE;

            for (unsigned dy = 0u; dy < QR_RASTER_SCALE; dy++)
            {
                uint16_t *row = &px[(size_t)(y0 + dy) * stride_px + x0];
                for (unsigned dx = 0u; dx < QR_RASTER_SCALE; dx++) { row[dx] = QR_BLACK; }
            }
        }
    }

    return true;
}
