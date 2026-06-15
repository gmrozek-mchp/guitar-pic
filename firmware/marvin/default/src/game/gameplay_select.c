#include "game/gameplay_select.h"
#include "game/gameplay_metadata.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define GP_BPP            3
#define GP_SEL_MAX_CELLS  16   /* >= max items on any modelled menu (7) */

/* Split [0, extent) into `n` spans; edge(i) = round(i*extent/n). */
static int gp_edge(int i, int extent, int n)
{
    return (int)lround((double)i * (double)extent / (double)n);
}

const gp_menu_layout_t *gp_menu_for_screen(uint8_t screen)
{
    for (int i = 0; i < GP_N_MENUS; i++)
    {
        if (gp_menus[i].screen == screen) { return &gp_menus[i]; }
    }
    return NULL;
}

/* Mean BGR over [x0,x1)×[y0,y1), clamped to the frame. */
static void gp_rect_mean_bgr(const uint8_t *frame, int w, int h,
                             int x0, int y0, int x1, int y1, float out[3])
{
    if (x0 < 0) { x0 = 0; }  if (x1 > w) { x1 = w; }
    if (y0 < 0) { y0 = 0; }  if (y1 > h) { y1 = h; }
    uint32_t sb = 0u, sg = 0u, sr = 0u, n = 0u;
    for (int y = y0; y < y1; y++)
    {
        const uint8_t *p = frame + ((uint32_t)y * (uint32_t)w + (uint32_t)x0) * GP_BPP;
        for (int x = x0; x < x1; x++)
        {
            sb += p[0]; sg += p[1]; sr += p[2];
            p += GP_BPP;
            n++;
        }
    }
    if (n == 0u) { out[0] = out[1] = out[2] = 0.0f; return; }
    out[0] = (float)sb / (float)n;
    out[1] = (float)sg / (float)n;
    out[2] = (float)sr / (float)n;
}

int gp_read_selection(const uint8_t *frame, int width, int height,
                      const gp_menu_layout_t *menu)
{
    if (width != GP_CANON_W || height != GP_CANON_H) { return -1; }
    int count = menu->count;
    if (count > GP_SEL_MAX_CELLS) { count = GP_SEL_MAX_CELLS; }

    float col[GP_SEL_MAX_CELLS][3];
    int bx0 = menu->band[0], by0 = menu->band[1], bx1 = menu->band[2], by1 = menu->band[3];
    for (int c = 0; c < count; c++)
    {
        int x0, y0, x1, y1;
        if (menu->axis == GP_AXIS_H)
        {
            y0 = by0; y1 = by1;
            x0 = bx0 + gp_edge(c, bx1 - bx0, count);
            x1 = bx0 + gp_edge(c + 1, bx1 - bx0, count);
        }
        else
        {
            x0 = bx0; x1 = bx1;
            y0 = by0 + gp_edge(c, by1 - by0, count);
            y1 = by0 + gp_edge(c + 1, by1 - by0, count);
        }
        gp_rect_mean_bgr(frame, width, height, x0, y0, x1, y1, col[c]);
    }

    /* Normalize per frame: subtract per-channel mean across cells, divide by the
     * scalar spread (matches highlight.cell_colors in the prototype). */
    float mean[3] = {0.0f, 0.0f, 0.0f};
    for (int c = 0; c < count; c++)
        for (int ch = 0; ch < 3; ch++) { mean[ch] += col[c][ch]; }
    for (int ch = 0; ch < 3; ch++) { mean[ch] /= (float)count; }

    double var = 0.0;
    for (int c = 0; c < count; c++)
        for (int ch = 0; ch < 3; ch++)
        {
            double d = (double)col[c][ch] - (double)mean[ch];
            var += d * d;
        }
    float std = (float)sqrt(var / (double)(count * 3));
    if (std < 1e-6f) { std = 1.0f; }

    int best = 0;
    float best_dev = -1.0f;
    for (int c = 0; c < count; c++)
    {
        const float *base = &menu->baseline[c * 3];
        float dev = 0.0f;
        for (int ch = 0; ch < 3; ch++)
        {
            float v = (col[c][ch] - mean[ch]) / std;
            float d = v - base[ch];
            dev += d * d;
        }
        if (dev > best_dev) { best_dev = dev; best = c; }
    }
    return best;
}

/* ── song_select ─────────────────────────────────────────────────────────── */

/* Per-frame-normalized low-res luma grid over an ROI, offset by (dx,dy). */
static void gp_song_grid(const uint8_t *frame, int w, int h,
                         const int roi[4], int dx, int dy, float *out)
{
    int x0 = roi[0] + dx, y0 = roi[1] + dy;
    int pw = roi[2] - roi[0], ph = roi[3] - roi[1];
    for (int r = 0; r < GP_SONG_ROWS; r++)
    {
        int cy0 = y0 + gp_edge(r, ph, GP_SONG_ROWS);
        int cy1 = y0 + gp_edge(r + 1, ph, GP_SONG_ROWS);
        if (cy0 < 0) { cy0 = 0; }  if (cy1 > h) { cy1 = h; }
        for (int c = 0; c < GP_SONG_COLS; c++)
        {
            int cx0 = x0 + gp_edge(c, pw, GP_SONG_COLS);
            int cx1 = x0 + gp_edge(c + 1, pw, GP_SONG_COLS);
            if (cx0 < 0) { cx0 = 0; }  if (cx1 > w) { cx1 = w; }
            double s = 0.0; uint32_t n = 0u;
            for (int y = cy0; y < cy1; y++)
            {
                const uint8_t *p = frame + ((uint32_t)y * (uint32_t)w + (uint32_t)cx0) * GP_BPP;
                for (int x = cx0; x < cx1; x++)
                {
                    s += (double)p[0] * 29.0 + (double)p[1] * 150.0 + (double)p[2] * 77.0;
                    p += GP_BPP;
                    n++;
                }
            }
            out[r * GP_SONG_COLS + c] = (n != 0u) ? (float)(s / 256.0 / (double)n) : 0.0f;
        }
    }
    double sum = 0.0;
    for (int i = 0; i < GP_SONG_LEN; i++) { sum += out[i]; }
    float mean = (float)(sum / (double)GP_SONG_LEN);
    double var = 0.0;
    for (int i = 0; i < GP_SONG_LEN; i++)
    {
        double d = (double)out[i] - (double)mean;
        var += d * d;
    }
    float std = (float)sqrt(var / (double)GP_SONG_LEN);
    if (std < 1e-6f) { std = 1.0f; }
    for (int i = 0; i < GP_SONG_LEN; i++) { out[i] = (out[i] - mean) / std; }
}

/* Offset-search grids, both ROIs. Static — single observer-task caller. */
static float s_slot_grids[GP_SONG_NDX * GP_SONG_NDY][GP_SONG_LEN];
static float s_first_grids[GP_SONG_NDX * GP_SONG_NDY][GP_SONG_LEN];

int gp_read_song(const uint8_t *frame, int width, int height, gp_song_t *out)
{
    if (width != GP_CANON_W || height != GP_CANON_H)
    {
        out->setlist = 0; out->index = 0; out->tmpl = -1;
        return -1;
    }
    static const int slot_roi[4]  = GP_SONG_SLOT_ROI;
    static const int first_roi[4] = GP_SONG_FIRST_ROI;

    int noff = 0;
    for (int ix = 0; ix < GP_SONG_NDX; ix++)
        for (int iy = 0; iy < GP_SONG_NDY; iy++)
        {
            gp_song_grid(frame, width, height, slot_roi,  gp_song_dx[ix], gp_song_dy[iy], s_slot_grids[noff]);
            gp_song_grid(frame, width, height, first_roi, gp_song_dx[ix], gp_song_dy[iy], s_first_grids[noff]);
            noff++;
        }

    float best_d = 1e30f;
    int best_t = -1;
    for (int t = 0; t < GP_N_SONGS; t++)
    {
        const float *vec = gp_song_templates[t].vec;
        const float (*grids)[GP_SONG_LEN] =
            (gp_song_templates[t].roi_kind == GP_SONG_ROI_FIRST) ? s_first_grids : s_slot_grids;
        float tbest = 1e30f;
        for (int o = 0; o < noff; o++)
        {
            float d = 0.0f;
            for (int i = 0; i < GP_SONG_LEN; i++) { d += fabsf(grids[o][i] - vec[i]); }
            if (d < tbest) { tbest = d; }
        }
        if (tbest < best_d) { best_d = tbest; best_t = t; }
    }

    out->tmpl = (int16_t)best_t;
    out->setlist = (best_t >= 0) ? gp_song_templates[best_t].setlist : 0;
    out->index = (best_t >= 0) ? gp_song_templates[best_t].index : 0;
    return 0;
}
