#include "game/game_art.h"

#include <ctype.h>     /* tolower */
#include <stdio.h>     /* snprintf */
#include <stdlib.h>
#include <string.h>

#include "definitions.h"
#include "log.h"

#include "storage/storage.h"
#include "game/gameplay_metadata.h"   /* GP_SETLIST_*, GP_N_SONGS */

#include "gfx/legato/common/legato_color.h"   /* LE_COLOR_MODE_RGB_888 */
#include "gfx/legato/common/legato_rect.h"
#include "gfx/legato/core/legato_stream.h"    /* LE_STREAM_LOCATION_ID_INTERNAL */

/* Slot geometry - must match the offline asset tool (process_gh3_cover_art.py). */
#define ART_SMALL_W 144u
#define ART_SMALL_H 144u
#define ART_LARGE_W 508u
#define ART_LARGE_H 208u

/* Each tier's slot format matches the hardware layer it is displayed on, so the
 * blit is a native copy with no runtime color conversion:
 *   small  RGB565  (2 B/px) — the dashboard now-playing thumbnail lives on the
 *          RGB565 BASE layer; 565 has no alpha, so the cover is inherently opaque.
 *   large  RGBA8888 (4 B/px) — the song-select strip has its own full-color OVR2
 *          layer, and its baked difficulty gradient needs the extra depth.
 * Both modes are 2D-engine-blittable (RGB_888 isn't). */
#define ART_SMALL_BPP  2u   /* RGB565   */
#define ART_LARGE_BPP  4u   /* RGBA8888 */
#define ART_SMALL_SLOT (ART_SMALL_W * ART_SMALL_H * ART_SMALL_BPP)
#define ART_LARGE_SLOT (ART_LARGE_W * ART_LARGE_H * ART_LARGE_BPP)

/* Largest compressed cover we'll read. Oversize files are skipped, not truncated. */
#define ART_SCRATCH_BYTES (512u * 1024u)

#define ART_DIR_REL   "games/gh3-wii/art"
#define ART_PATH_MAX  96

#define REGION_RAM    __attribute__((section(".region_ram"), aligned (32)))

typedef struct
{
    uint8_t setlist;
    uint8_t index;
    bool    valid;
} art_key_t;

/* Pixel pools + the shared decode scratch live in cached DDR (.region_ram): they
 * are decoder source/output staging that Legato later blits into the (non-cached)
 * canvas surfaces, never scanned out by the LCDC directly. */
static uint8_t  REGION_RAM s_small_px[GP_N_SONGS][ART_SMALL_SLOT];
static uint8_t  REGION_RAM s_large_px[GP_N_SONGS][ART_LARGE_SLOT];
static uint8_t  REGION_RAM s_scratch[ART_SCRATCH_BYTES];

static leImage   s_small_img[GP_N_SONGS];
static leImage   s_large_img[GP_N_SONGS];
static art_key_t s_small_key[GP_N_SONGS];
static art_key_t s_large_key[GP_N_SONGS];

static int  s_small_n = 0;
static int  s_large_n = 0;
static bool s_loaded  = false;

static game_art_progress_fn s_progress_cb;

/* ---- filename / header parsing ----------------------------------------- */

/* Case-insensitive equality of `s` (an n-char span, no NUL) with a lowercase
 * literal. FatFs may hand back 8.3 short names in upper case. */
static bool ieq(const char *s, size_t n, const char *lower)
{
    size_t i;
    for (i = 0; i < n; i++)
    {
        if (lower[i] == '\0' || tolower((unsigned char)s[i]) != lower[i]) { return false; }
    }
    return lower[i] == '\0';
}

/* Case-insensitive match of a NUL-terminated extension against a lowercase literal. */
static bool ext_is(const char *ext, const char *lower)
{
    size_t i;
    for (i = 0; lower[i] != '\0'; i++)
    {
        if (tolower((unsigned char)ext[i]) != lower[i]) { return false; }
    }
    return ext[i] == '\0';
}

/* "main"/"bonus" (any case) -> GP_SETLIST_*, or -1. */
static int setlist_id(const char *s, size_t n)
{
    if (ieq(s, n, "main"))  { return GP_SETLIST_MAIN; }
    if (ieq(s, n, "bonus")) { return GP_SETLIST_BONUS; }
    return -1;
}

/* Parse "<setlist>-<NN>.<ext>" -> setlist, index, image format. */
static bool parse_name(const char *fname, uint8_t *setlist, uint8_t *index,
                       leImageFormat *fmt)
{
    const char *dash = strchr(fname, '-');
    const char *dot  = strrchr(fname, '.');
    if (dash == NULL || dot == NULL || dot < dash) { return false; }

    int sl = setlist_id(fname, (size_t)(dash - fname));
    if (sl < 0) { return false; }

    const char *ext = dot + 1;
    if (ext_is(ext, "png"))                            { *fmt = LE_IMAGE_FORMAT_PNG; }
    else if (ext_is(ext, "jpg") || ext_is(ext, "jpeg")) { *fmt = LE_IMAGE_FORMAT_JPEG; }
    else { return false; }

    *setlist = (uint8_t)sl;
    *index   = (uint8_t)atoi(dash + 1);
    return true;
}

/* True image dimensions from the compressed header (the destination raster is
 * sized from these, so they must be exact). */
static bool jpeg_dims(const uint8_t *b, uint32_t n, uint16_t *w, uint16_t *h)
{
    if (n < 4u || b[0] != 0xFFu || b[1] != 0xD8u) { return false; }

    uint32_t i = 2u;
    while (i + 1u < n)
    {
        if (b[i] != 0xFFu) { i++; continue; }

        uint8_t m = b[i + 1u];
        i += 2u;

        /* Standalone markers (no length field). */
        if (m == 0xD8u || m == 0xD9u || (m >= 0xD0u && m <= 0xD7u) || m == 0x01u) { continue; }
        if (i + 1u >= n) { break; }

        uint16_t seg = (uint16_t)((b[i] << 8) | b[i + 1u]);

        /* SOF markers carry frame dimensions; exclude DHT(C4)/JPG(C8)/DAC(CC). */
        if (m >= 0xC0u && m <= 0xCFu && m != 0xC4u && m != 0xC8u && m != 0xCCu)
        {
            if (i + 7u > n) { break; }
            *h = (uint16_t)((b[i + 3u] << 8) | b[i + 4u]);
            *w = (uint16_t)((b[i + 5u] << 8) | b[i + 6u]);
            return true;
        }

        i += seg;   /* length includes its own 2 bytes */
    }
    return false;
}

static bool png_dims(const uint8_t *b, uint32_t n, uint16_t *w, uint16_t *h)
{
    if (n < 24u) { return false; }
    if (b[0] != 0x89u || b[1] != 0x50u || b[2] != 0x4Eu || b[3] != 0x47u) { return false; }

    /* IHDR width/height are 4-byte big-endian at offsets 16 and 20; our sizes fit
     * 16 bits, so take the low two bytes of each. */
    *w = (uint16_t)((b[18] << 8) | b[19]);
    *h = (uint16_t)((b[22] << 8) | b[23]);
    return true;
}

/* ---- decode ------------------------------------------------------------- */

/* DIAGNOSTIC: fill slots with a known RGBA8888 pattern instead of decoding from
 * the card, to isolate the display path (GFX2D blit -> canvas -> XLCDC) from the
 * PNG/JPEG decode. Set to 0 for real artwork. Pattern: R ramps left->right, G ramps
 * top->bottom, B=0x40, opaque, with a 1px white border. Correct display = smooth 2D
 * gradient framed edge-to-edge; stride bugs shear the R ramp / break the border;
 * a height truncation stops the G ramp early and drops the bottom border; a
 * byte-order bug swaps which axis is red vs green/blue. */
#define ART_TEST_PATTERN 0

#if ART_TEST_PATTERN
static void fill_test_pattern(uint8_t *slot, uint16_t w, uint16_t h)
{
    uint32_t *px = (uint32_t *)(void *)slot;
    for (uint16_t y = 0u; y < h; y++)
    {
        for (uint16_t x = 0u; x < w; x++)
        {
            uint8_t r = (uint8_t)((uint32_t)x * 255u / (w - 1u));
            uint8_t g = (uint8_t)((uint32_t)y * 255u / (h - 1u));
            uint32_t c = ((uint32_t)r << 24) | ((uint32_t)g << 16) | (0x40u << 8) | 0xFFu;
            if (x == 0u || y == 0u || x == (w - 1u) || y == (h - 1u)) { c = 0xFFFFFFFFu; }
            px[(uint32_t)y * w + x] = c;
        }
    }
}
#endif

/* Read one cover file into scratch, verify it matches the tier slot dimensions,
 * and decode it into the slot as `mode` (RGB565 or RGBA8888) via the Legato
 * JPEG/PNG decoder. Fills *out_img (RAW, `mode`, pointing at slot_px) on success. */
static bool decode_one(const char *path, leImageFormat fmt, leColorMode mode,
                       uint8_t *slot_px, uint16_t w_exp, uint16_t h_exp,
                       leImage *out_img)
{
    uint8_t bpp = (mode == LE_COLOR_MODE_RGBA_8888) ? 4u : 2u;
#if ART_TEST_PATTERN
    (void)path; (void)fmt;
    fill_test_pattern(slot_px, w_exp, h_exp);
    (void)leImage_Create(out_img, w_exp, h_exp, mode, slot_px,
                         LE_STREAM_LOCATION_ID_INTERNAL);
    out_img->flags |= LE_IMAGE_DIRECT_BLIT;
    dcache_CleanByAddr(slot_px, (int32_t)((uint32_t)w_exp * h_exp * bpp));
    return true;
#else
    SYS_FS_HANDLE h = SYS_FS_FileOpen(path, SYS_FS_FILE_OPEN_READ);
    if (h == SYS_FS_HANDLE_INVALID)
    {
        LOG_WARN("ART: open '%s' failed (fs err %d)\r\n", path, (int)SYS_FS_Error());
        return false;
    }

    int32_t size = SYS_FS_FileSize(h);
    if (size <= 0 || (uint32_t)size > ART_SCRATCH_BYTES)
    {
        LOG_WARN("ART: '%s' size %ld out of range\r\n", path, (long)size);
        (void)SYS_FS_FileClose(h);
        return false;
    }
    size_t got = SYS_FS_FileRead(h, s_scratch, (size_t)size);
    (void)SYS_FS_FileClose(h);
    if (got != (size_t)size) { LOG_WARN("ART: '%s' short read\r\n", path); return false; }

    uint16_t iw = 0u, ih = 0u;
    bool ok = (fmt == LE_IMAGE_FORMAT_PNG)
                  ? png_dims(s_scratch, (uint32_t)size, &iw, &ih)
                  : jpeg_dims(s_scratch, (uint32_t)size, &iw, &ih);
    if (!ok) { LOG_WARN("ART: '%s' header dims unreadable\r\n", path); return false; }
    if (iw != w_exp || ih != h_exp)
    {
        LOG_WARN("ART: '%s' is %ux%u, expected %ux%u\r\n",
                 path, (unsigned)iw, (unsigned)ih, (unsigned)w_exp, (unsigned)h_exp);
        return false;
    }

    /* Source: compressed bytes in scratch, described as a JPEG/PNG image.
     * leImage_Create stamps header.size with the *decoded* size; the PNG decoder
     * passes header.size to lodepng as the *compressed* input length, so override
     * it with the real file size (harmless for the marker-driven JPEG path). */
    leImage src;
    (void)leImage_Create(&src, iw, ih, LE_COLOR_MODE_RGB_888, s_scratch,
                         LE_STREAM_LOCATION_ID_INTERNAL);
    src.format       = fmt;
    src.header.size  = (uint32_t)size;

    /* Destination: the fixed slot as a RAW image in the tier's layer mode.
     * RGBA8888 (large): the 2D engine declines an alpha-mode source (SRC_OVER, not
     * a plain copy), so set LE_IMAGE_DIRECT_BLIT to route the on-paint draw through
     * the RAW decoder's _directBlit (per-row memcpy into the matching-mode OVR2
     * layer) instead of a per-pixel software blit (visible hesitation on select).
     * RGB565 (small): source and the BASE layer are both 565, so the standard draw
     * path uses the 2D engine (a native 565→565 copy) and correctly positions/clips
     * the thumbnail within the full-screen dashboard canvas — no DIRECT_BLIT. */
    (void)leImage_Create(out_img, iw, ih, mode, slot_px,
                         LE_STREAM_LOCATION_ID_INTERNAL);
    if (mode == LE_COLOR_MODE_RGBA_8888) { out_img->flags |= LE_IMAGE_DIRECT_BLIT; }

    leRect full = { 0, 0, (int32_t)iw, (int32_t)ih };
    /* Decode straight into the slot. leImage_Render's return is unreliable
     * (always LE_FAILURE even on success), so we trust the validated dims +
     * known format, mirroring how leProcessImage drives the decoder. */
    (void)leImage_Render(&src, &full, 0, 0, LE_TRUE, LE_TRUE, out_img);

    /* Force opaque alpha (RGBA8888 only). The covers are fully opaque (any fade is
     * baked into RGB), but a JPEG/PNG-without-alpha decode into RGBA8888 can leave
     * the alpha byte 0, which renders the image fully transparent. RGBA_8888 packs
     * 0xRRGGBBAA, so alpha is the low byte. RGB565 has no alpha channel. */
    if (mode == LE_COLOR_MODE_RGBA_8888)
    {
        uint32_t *px  = (uint32_t *)(void *)slot_px;
        size_t    npx = (size_t)iw * ih;
        for (size_t i = 0; i < npx; i++) { px[i] |= 0x000000FFu; }
    }

    /* The decode wrote the slot via the CPU (write-back cache); flush it to DDR so
     * a 2D-engine read (leGPU_BlitBuffer DMA-reads the slot) sees current pixels.
     * 32-byte aligned, exact-multiple size → clean line boundaries. */
    dcache_CleanByAddr(slot_px, (int32_t)((uint32_t)iw * ih * bpp));
    return true;
#endif /* ART_TEST_PATTERN */
}

/* Scan scratch (reused across the two sequential load_tier calls): the dir walk
 * collects entries here, then the dir is closed before any file is opened. */
typedef struct { uint8_t setlist; uint8_t index; leImageFormat fmt; } art_scan_t;
static art_scan_t s_scan[GP_N_SONGS];

/* Load one tier subdir's covers into its pool. Two phases because FatFs is built
 * with FF_FS_MAX_FILES=1 — only one open object at a time — so we must close the
 * directory handle before opening any file (otherwise every SYS_FS_FileOpen
 * returns FR_TOO_MANY_OPEN_FILES). Returns the number decoded. */
static int load_tier(const char *subdir, uint8_t *pool, size_t slot_bytes,
                     uint16_t w, uint16_t h, leColorMode mode,
                     leImage *imgs, art_key_t *keys)
{
    char dirpath[ART_PATH_MAX];
    (void)snprintf(dirpath, sizeof dirpath, "%s/%s/%s",
                   Storage_MountPoint(), ART_DIR_REL, subdir);

    /* Phase 1 — collect "<setlist>-<NN>.<ext>" entries, then close the dir. */
    SYS_FS_HANDLE dh = SYS_FS_DirOpen(dirpath);
    if (dh == SYS_FS_HANDLE_INVALID)
    {
        LOG_WARN("ART: opendir '%s' failed (fs err %d)\r\n", dirpath, (int)SYS_FS_Error());
        return 0;
    }

    int n = 0, seen = 0;
    SYS_FS_FSTAT st;
    while (n < GP_N_SONGS)
    {
        memset(&st, 0, sizeof st);
        st.lfname = NULL;
        st.lfsize = 0u;

        if (SYS_FS_DirRead(dh, &st) != SYS_FS_RES_SUCCESS) { break; }
        if (st.fname[0] == '\0') { break; }                 /* end of directory */
        if ((st.fattrib & SYS_FS_ATTR_DIR) != 0u) { continue; }
        if (st.fname[0] == '.') { continue; }               /* skip ._ AppleDouble & dotfiles */

        seen++;
        if (parse_name(st.fname, &s_scan[n].setlist, &s_scan[n].index, &s_scan[n].fmt))
        {
            n++;
        }
    }
    (void)SYS_FS_DirClose(dh);

    /* Phase 2 — decode each collected cover (dir closed → one file open at a time).
     * Reconstruct the path from the key so we never touch a stray filename. */
    int count = 0;
    if (s_progress_cb != NULL) { s_progress_cb(subdir, 0u, (uint32_t)n); }

    for (int i = 0; i < n; i++)
    {
        const char *setname = (s_scan[i].setlist == GP_SETLIST_BONUS) ? "bonus" : "main";
        const char *ext     = (s_scan[i].fmt == LE_IMAGE_FORMAT_PNG) ? "png" : "jpg";
        char filepath[ART_PATH_MAX];
        (void)snprintf(filepath, sizeof filepath, "%s/%s-%02u.%s",
                       dirpath, setname, (unsigned)s_scan[i].index, ext);

        uint8_t *slot = pool + (size_t)count * slot_bytes;
        if (decode_one(filepath, s_scan[i].fmt, mode, slot, w, h, &imgs[count]))
        {
            keys[count].setlist = s_scan[i].setlist;
            keys[count].index   = s_scan[i].index;
            keys[count].valid   = true;
            count++;
        }
        if (s_progress_cb != NULL) { s_progress_cb(subdir, (uint32_t)(i + 1), (uint32_t)n); }
    }

    LOG_INFO("ART: %s: %d file(s) seen, %d loaded\r\n", subdir, seen, count);
    return count;
}

/* ---- public ------------------------------------------------------------- */

void GameArt_Initialize(void)
{
    s_small_n = 0;
    s_large_n = 0;
    s_loaded  = false;
}

void GameArt_SetProgressCallback(game_art_progress_fn fn)
{
    s_progress_cb = fn;
}

int GameArt_LoadAll(void)
{
    if (s_loaded) { return s_small_n + s_large_n; }
    s_loaded = true;   /* one attempt; lookups won't trigger a re-load */

    if (!Storage_Mount()) { return 0; }

    s_small_n = load_tier("small", (uint8_t *)s_small_px, ART_SMALL_SLOT,
                          ART_SMALL_W, ART_SMALL_H, LE_COLOR_MODE_RGB_565,
                          s_small_img, s_small_key);
    s_large_n = load_tier("large", (uint8_t *)s_large_px, ART_LARGE_SLOT,
                          ART_LARGE_W, ART_LARGE_H, LE_COLOR_MODE_RGBA_8888,
                          s_large_img, s_large_key);

    LOG_INFO("ART: loaded %d small, %d large\r\n", s_small_n, s_large_n);
    return s_small_n + s_large_n;
}

bool GameArt_IsLoaded(void) { return s_loaded; }
int  GameArt_CountSmall(void) { return s_small_n; }
int  GameArt_CountLarge(void) { return s_large_n; }

static const leImage *lookup(const art_key_t *keys, const leImage *imgs, int n,
                             uint8_t setlist, uint8_t index)
{
    for (int i = 0; i < n; i++)
    {
        if (keys[i].valid && keys[i].setlist == setlist && keys[i].index == index)
        {
            return &imgs[i];
        }
    }
    return NULL;
}

const leImage *GameArt_Small(uint8_t setlist, uint8_t index)
{
    return lookup(s_small_key, s_small_img, s_small_n, setlist, index);
}

const leImage *GameArt_Large(uint8_t setlist, uint8_t index)
{
    return lookup(s_large_key, s_large_img, s_large_n, setlist, index);
}
