#include "ui/gfx/ui_surface.h"

typedef struct
{
    const void     *buffer;
    uint16_t        w;
    uint16_t        h;
    GFXC_COLOR_FORMAT  mode;
    bool            valid;
} ui_surface_t;

static ui_surface_t s_surface[UI_SURFACE_MAX_CANVAS];

GFXC_RESULT UiSurface_Set(unsigned int canvas, uint32_t w, uint32_t h,
                          GFXC_COLOR_FORMAT mode, void *buffer)
{
    if (canvas < UI_SURFACE_MAX_CANVAS)
    {
        s_surface[canvas].buffer = buffer;
        s_surface[canvas].w      = (uint16_t)w;
        s_surface[canvas].h      = (uint16_t)h;
        s_surface[canvas].mode   = mode;
        s_surface[canvas].valid  = (buffer != NULL);
    }

    return gfxcSetPixelBuffer(canvas, w, h, mode, buffer);
}

bool UiSurface_Get(unsigned int canvas, const void **buffer,
                   uint16_t *w, uint16_t *h, GFXC_COLOR_FORMAT *mode)
{
    if (canvas >= UI_SURFACE_MAX_CANVAS || !s_surface[canvas].valid)
    {
        return false;
    }

    if (buffer != NULL) { *buffer = s_surface[canvas].buffer; }
    if (w      != NULL) { *w      = s_surface[canvas].w; }
    if (h      != NULL) { *h      = s_surface[canvas].h; }
    if (mode   != NULL) { *mode   = s_surface[canvas].mode; }

    return true;
}
