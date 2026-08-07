#include "ui/qr_art.h"

#include <stdint.h>
#include <string.h>

#include "definitions.h"
#include "log.h"

#include "ui/gfx/qr_raster.h"

#include "gfx/legato/common/legato_color.h"
#include "gfx/legato/core/legato_stream.h"
#include "gfx/legato/image/legato_image.h"

/* One slot per distinct URL. Eight is the worst case — one per System Info card — so a
 * new card can never exhaust the cache, and the strcmp dedupe below is a saving rather
 * than a correctness requirement (today five slots hold eight cards). */
#define QR_ART_MAX  8u

#define URL_CAP     96u

#define REGION_RAM  __attribute__((section(".region_ram"), aligned (32)))

/* Cached DDR, write-once per slot: the CPU rasterizes into it, the clean at the end of
 * encode_one pushes it out, and nothing writes it again — so no line can go dirty
 * behind the 2D engine that DMA-reads it. Same contract as the node photos. */
static uint16_t REGION_RAM s_px[QR_ART_MAX][QR_RASTER_W * QR_RASTER_H];

static leImage s_img[QR_ART_MAX];
static char    s_url[QR_ART_MAX][URL_CAP];
static int     s_n;

void QrArt_Initialize(void)
{
    s_n = 0;
    (void)memset(s_url, 0, sizeof s_url);
}

const leImage *QrArt_Get(const char *url)
{
    if ((url == NULL) || (url[0] == '\0')) { return NULL; }

    if (strlen(url) >= URL_CAP)
    {
        LOG_WARN("QR: url %u bytes exceeds cache key (%u)\r\n",
                 (unsigned)strlen(url), (unsigned)URL_CAP);
        return NULL;
    }

    for (int i = 0; i < s_n; i++)
    {
        if (strcmp(s_url[i], url) == 0) { return &s_img[i]; }
    }

    if (s_n >= (int)QR_ART_MAX)
    {
        LOG_WARN("QR: cache full (%u), no tile for '%s'\r\n", (unsigned)QR_ART_MAX, url);
        return NULL;
    }

    uint16_t *px = s_px[s_n];

    if (!QrRaster_Render(url, px, QR_RASTER_W))
    {
        /* Almost always a URL too long for the pinned QR version. */
        LOG_WARN("QR: cannot encode '%s' (%u bytes)\r\n", url, (unsigned)strlen(url));
        return NULL;
    }

    /* RAW RGB565 over the slot. Source and the BASE canvas are both 565, so the
     * standard draw path uses the 2D engine and clips the tile within the full-screen
     * canvas — no LE_IMAGE_DIRECT_BLIT (see game_art.c's small tier). */
    (void)leImage_Create(&s_img[s_n], (uint16_t)QR_RASTER_W, (uint16_t)QR_RASTER_H,
                         LE_COLOR_MODE_RGB_565, px, LE_STREAM_LOCATION_ID_INTERNAL);

    dcache_CleanByAddr(px, (int32_t)QR_RASTER_BYTES);

    (void)strncpy(s_url[s_n], url, URL_CAP - 1u);
    s_n++;

    return &s_img[s_n - 1];
}
