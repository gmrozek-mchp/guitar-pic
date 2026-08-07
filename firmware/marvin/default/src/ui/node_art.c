#include "ui/node_art.h"

#include <stdio.h>     /* snprintf */
#include <string.h>

#include "definitions.h"
#include "log.h"

#include "storage/storage.h"

#include "gfx/legato/common/legato_color.h"
#include "gfx/legato/common/legato_rect.h"
#include "gfx/legato/core/legato_stream.h"
#include "gfx/legato/image/legato_image.h"

/* One photo per node, keyed by T1S PLCA id. The filenames are the node names the
 * rest of the system uses, so a photo drop is self-explanatory on the card. */
typedef struct { uint8_t id; const char *file; } node_photo_t;

static const node_photo_t PHOTO[] = {
    { 0u,    "marvin"     },
    { 1u,    "fauxmote"   },
    { 3u,    "guitar"     },
    { 4u,    "fretboard"  },
    { 5u,    "beatbox"    },
    { 6u,    "lemmy"      },
    { 7u,    "lightshow"  },
    /* The whole-rig shot for the project card, which is not a node — 0xFF is its stand-in
     * key (ID_PROJECT in screen_system.c), outside the T1S id space. */
    { 0xFFu, "guitar-pic" },
};
#define PHOTO_N  (sizeof PHOTO / sizeof PHOTO[0])

/* RGBA8888 to match the OVR1 layer the slot is scanned out on. The alpha byte is
 * forced opaque after decode and never used — the layer is a plain opaque rectangle
 * — but RGBA_8888 is the mode the XLCDC and Legato's decoder agree on at 32bpp. */
#define SLOT_BPP    4u
#define SLOT_BYTES  (NODE_ART_W * NODE_ART_H * SLOT_BPP)

/* Largest compressed photo we'll read. Oversize files are skipped, not truncated.
 * PNG of a full-colour photo this size runs a few hundred KB. */
#define SCRATCH_BYTES  (512u * 1024u)

#define DIR_REL   "system/nodes"
#define PATH_MAX_  96

#define REGION_RAM  __attribute__((section(".region_ram"), aligned (32)))

/* Pixel pool + decode scratch live in cached DDR even though the LCDC DMA-reads a
 * slot directly. Safe because a slot is write-once: the CPU decodes into it, the
 * clean at the end of decode_one pushes it to DDR, and nothing writes it again, so no
 * line can go dirty behind the display controller's back. */
static uint8_t REGION_RAM s_px[PHOTO_N][SLOT_BYTES];
static uint8_t REGION_RAM s_scratch[SCRATCH_BYTES];

static bool    s_valid[PHOTO_N];
static int     s_n;
static bool    s_loaded;

static node_art_progress_fn s_progress_cb;

/* True dimensions from the PNG IHDR. The destination raster is sized from the slot,
 * so a mismatch has to be rejected rather than scaled. IHDR width/height are 4-byte
 * big-endian at offsets 16 and 20; our sizes fit 16 bits, so take the low two bytes. */
static bool png_dims(const uint8_t *b, uint32_t n, uint16_t *w, uint16_t *h)
{
    if (n < 24u) { return false; }
    if (b[0] != 0x89u || b[1] != 0x50u || b[2] != 0x4Eu || b[3] != 0x47u) { return false; }

    *w = (uint16_t)((b[18] << 8) | b[19]);
    *h = (uint16_t)((b[22] << 8) | b[23]);
    return true;
}

/* Read one photo into scratch, verify it is exactly slot-sized, and decode it into
 * the slot as RGBA8888.
 *
 * PNG, not JPEG: Legato's JPEG decoder gates its block writes on the renderer clip
 * rect, which is stale during an offscreen boot decode, so a runtime JPEG lands as
 * noise. The PNG decoder is a plain colour-converting buffer copy with no clip
 * dependency. The album-art tiers are PNG for the same reason. */
static bool decode_one(const char *path, uint8_t *slot_px)
{
    SYS_FS_HANDLE fh = SYS_FS_FileOpen(path, SYS_FS_FILE_OPEN_READ);
    if (fh == SYS_FS_HANDLE_INVALID) { return false; }   /* absent is normal */

    int32_t size = SYS_FS_FileSize(fh);
    if (size <= 0 || (uint32_t)size > SCRATCH_BYTES)
    {
        LOG_WARN("NODEART: '%s' size %ld out of range\r\n", path, (long)size);
        (void)SYS_FS_FileClose(fh);
        return false;
    }
    size_t got = SYS_FS_FileRead(fh, s_scratch, (size_t)size);
    (void)SYS_FS_FileClose(fh);
    if (got != (size_t)size)
    {
        LOG_WARN("NODEART: '%s' short read\r\n", path);
        return false;
    }

    uint16_t iw = 0u, ih = 0u;
    if (!png_dims(s_scratch, (uint32_t)size, &iw, &ih))
    {
        LOG_WARN("NODEART: '%s' header dims unreadable\r\n", path);
        return false;
    }
    if (iw != NODE_ART_W || ih != NODE_ART_H)
    {
        LOG_WARN("NODEART: '%s' is %ux%u, expected %ux%u\r\n",
                 path, (unsigned)iw, (unsigned)ih,
                 (unsigned)NODE_ART_W, (unsigned)NODE_ART_H);
        return false;
    }

    /* Source: the compressed bytes in scratch, described as a PNG. leImage_Create
     * stamps header.size with the *decoded* size, but the PNG decoder passes it to
     * lodepng as the *compressed* input length — so override it with the file size. */
    leImage src;
    (void)leImage_Create(&src, iw, ih, LE_COLOR_MODE_RGB_888, s_scratch,
                         LE_STREAM_LOCATION_ID_INTERNAL);
    src.format      = LE_IMAGE_FORMAT_PNG;
    src.header.size = (uint32_t)size;

    /* Destination: the slot as a RAW RGBA8888 image. Only ever a decode target — the
     * LCDC scans the pixels, so no LE_IMAGE_DIRECT_BLIT and no draw path here. */
    leImage dst;
    (void)leImage_Create(&dst, iw, ih, LE_COLOR_MODE_RGBA_8888, slot_px,
                         LE_STREAM_LOCATION_ID_INTERNAL);

    leRect full = { 0, 0, (int32_t)iw, (int32_t)ih };
    /* leImage_Render's return is unreliable (LE_FAILURE even on success), so we trust
     * the validated dims + known format, mirroring how leProcessImage drives it. */
    (void)leImage_Render(&src, &full, 0, 0, LE_TRUE, LE_TRUE, &dst);

    /* Force opaque alpha: a PNG-without-alpha decode into RGBA8888 leaves the alpha
     * byte 0, which the LCDC reads as a fully transparent layer. RGBA_8888 packs
     * 0xRRGGBBAA, so alpha is the low byte (same fix as game_art.c's large tier). */
    {
        uint32_t *px  = (uint32_t *)(void *)slot_px;
        size_t    npx = (size_t)iw * ih;
        for (size_t i = 0; i < npx; i++) { px[i] |= 0x000000FFu; }
    }

    /* The decode wrote the slot via the CPU (write-back cache); flush it so the LCDC's
     * DMA read sees current pixels. 32-byte aligned, exact-multiple size → clean line
     * boundaries. */
    dcache_CleanByAddr(slot_px, (int32_t)((uint32_t)iw * ih * SLOT_BPP));
    return true;
}

void NodeArt_Initialize(void)
{
    s_n      = 0;
    s_loaded = false;
    memset(s_valid, 0, sizeof s_valid);
}

void NodeArt_SetProgressCallback(node_art_progress_fn fn)
{
    s_progress_cb = fn;
}

int NodeArt_LoadAll(void)
{
    if (s_loaded) { return s_n; }
    s_loaded = true;   /* one attempt; lookups won't trigger a re-load */

    if (!Storage_Mount()) { return 0; }

    /* One file open at a time: FatFs is built with FF_FS_MAX_FILES=1. Each photo is
     * opened, read and closed inside decode_one, so no directory walk is needed —
     * the names are known, which also means a stray filename is never touched. */
    if (s_progress_cb != NULL) { s_progress_cb(0u, PHOTO_N); }

    for (unsigned i = 0u; i < PHOTO_N; i++)
    {
        char path[PATH_MAX_];
        (void)snprintf(path, sizeof path, "%s/%s/%s.png",
                       Storage_MountPoint(), DIR_REL, PHOTO[i].file);

        if (decode_one(path, s_px[i]))
        {
            s_valid[i] = true;
            s_n++;
        }
        if (s_progress_cb != NULL) { s_progress_cb(i + 1u, PHOTO_N); }
    }

    LOG_INFO("NODEART: %d of %u board photo(s) loaded\r\n", s_n, (unsigned)PHOTO_N);
    return s_n;
}

bool NodeArt_IsLoaded(void) { return s_loaded; }
int  NodeArt_Count(void)    { return s_n; }

const void *NodeArt_Pixels(uint8_t node_id)
{
    for (unsigned i = 0u; i < PHOTO_N; i++)
    {
        if (PHOTO[i].id == node_id)
        {
            return s_valid[i] ? (const void *)s_px[i] : NULL;
        }
    }
    return NULL;
}
