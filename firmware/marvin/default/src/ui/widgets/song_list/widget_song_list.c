#include "ui/widgets/song_list/widget_song_list.h"

#include <string.h>

#include "gfx/legato/legato.h"
#include "gfx/legato/widget/legato_widget.h"
#include "gfx/legato/string/legato_string_renderer.h"

#include "FreeRTOS.h"
#include "task.h"   /* xTaskGetTickCount — real elapsed time for inertia */

/* ---- layout / feel constants ------------------------------------------- */

#define SL_PAD_X        20      /* left/right inset for content */
#define SL_BADGE_COL    50      /* width reserved for the tier badge column */
#define SL_LINE_GAP     2       /* px between title and artist */
#define SL_TAP_SLOP     10      /* drag under this many px counts as a tap */
#define SL_DECAY_PER_MS 0.004f  /* velocity *= (1 - SL_DECAY_PER_MS*dt_ms) per update */
#define SL_MIN_VEL      0.02f   /* px/ms; inertia below this stops */
#define SL_MAX_VEL      4.0f    /* px/ms; cap on a release fling */
#define SL_MAX_DT_MS    100u    /* clamp a long scheduling stall */
#define SL_DEFAULT_ROWH 64

/* Colors are authored in RGB_888 and converted to the active layer mode at
 * paint time. */
#define SL_BG           0x0E0E0Eu
#define SL_SEP          0x232323u
#define SL_SEL_BG       0xF2F2F2u
#define SL_TITLE        0xF0F0F0u
#define SL_ARTIST       0x8A8A8Au
#define SL_RIGHT        0x9A9A9Au
#define SL_SEL_TITLE    0x121212u
#define SL_SEL_ARTIST   0x606060u
#define SL_SEL_RIGHT    0x404040u

/* ---- widget type -------------------------------------------------------- */

typedef struct leSongListWidget
{
    leWidget widget;            /* base — must be first */

    int                count;
    songlist_row_fn    rowFn;
    void              *rowCtx;
    songlist_select_fn selFn;
    void              *selCtx;

    const leFont *titleFont;
    const leFont *metaFont;
    const leFont *badgeFont;

    int   rowHeight;
    float scrollY;              /* px scrolled from the top */
    float velocity;             /* px/ms of scrollY, applied while not touching */
    int   selected;             /* -1 = none */

    /* touch tracking */
    bool     tracking;
    int32_t  touchId;
    int32_t  firstY;
    int32_t  lastY;
    uint32_t lastMoveMs;        /* time of the last touch-move (drag velocity) */
    uint32_t lastTickMs;        /* time of the last inertia step */

    bool     debugFill;         /* diagnostic: paint a bare solid rect, no rows */
} leSongListWidget;

static leWidgetVTable songListVTable;
static bool           vtableReady = false;

/* ---- small helpers ------------------------------------------------------ */

static uint32_t now_ms(void)
{
    return (uint32_t)xTaskGetTickCount() * (uint32_t)portTICK_PERIOD_MS;
}

static leColor conv(leColor rgb888)
{
    return leColorConvert(LE_COLOR_MODE_RGB_888, leRenderer_CurrentColorMode(), rgb888);
}

static int font_h(const leFont *f)
{
    return (f != NULL) ? (int)((const leRasterFont *)f)->height : 0;
}

static int max_scroll(const leSongListWidget *w)
{
    int content = w->count * w->rowHeight;
    int vh = (int)w->widget.rect.height;
    return (content > vh) ? (content - vh) : 0;
}

static void clamp_scroll(leSongListWidget *w)
{
    int mx = max_scroll(w);
    if (w->scrollY < 0.0f)        { w->scrollY = 0.0f; }
    else if (w->scrollY > (float)mx) { w->scrollY = (float)mx; }
}

static void draw_str(const leSongListWidget *w, const char *s, const leFont *font,
                     int x, int y, leHAlignment align, leColor color)
{
    leCStringRenderRequest req;
    if (s == NULL || s[0] == '\0' || font == NULL) { return; }

    req.str   = s;
    req.font  = font;
    req.x     = x;
    req.y     = y;
    req.align = align;
    req.color = color;
    req.alpha = 255;
    req.lookupTable = (w->widget.scheme != NULL)
        ? leUtils_GetSchemeLookupTable(w->widget.scheme, color,
              leScheme_GetRenderColor(w->widget.scheme, LE_SCHM_BASE))
        : NULL;

    leStringRenderer_DrawCString(&req);
}

/* ---- paint -------------------------------------------------------------- */

static void sl_paint(leWidget *wgt)
{
    leSongListWidget *w = (leSongListWidget *)wgt;
    leRect area;
    int rowH, vh, sy, first, firstTop, i;
    int titleH, metaH;

    if (wgt->status.drawState == LE_WIDGET_DRAW_STATE_DONE) { return; }

    wgt->fn->rectToScreen(wgt, &area);

    if (w->debugFill)
    {
        /* Diagnostic: a flat opaque fill, nothing else. If dashboard text still
         * bleeds through this, the compositing order is the problem, not our
         * row rendering. Bright magenta so any bleed is unmistakable. */
        leRenderer_RectFill(&area, conv(0xFF00FFu), 255);
        wgt->status.drawState = LE_WIDGET_DRAW_STATE_DONE;
        wgt->drawFunc = NULL;
        return;
    }

    leRenderer_RectFill(&area, conv(SL_BG), 255);

    rowH = w->rowHeight;
    vh   = (int)area.height;
    if (rowH <= 0)
    {
        wgt->status.drawState = LE_WIDGET_DRAW_STATE_DONE;
        wgt->drawFunc = NULL;
        return;
    }

    titleH = font_h(w->titleFont);
    metaH  = font_h(w->metaFont);

    sy       = (int)(w->scrollY + 0.5f);
    first    = sy / rowH;
    if (first < 0) { first = 0; }
    firstTop = area.y - (sy - first * rowH);

    for (i = first; i < w->count; i++)
    {
        int rowTop = firstTop + (i - first) * rowH;
        songlist_row_t row;
        leColor titleC, artistC, rightC;
        int textX, block, startY;

        if (rowTop >= area.y + vh) { break; }

        memset(&row, 0, sizeof(row));
        if (w->rowFn == NULL || !w->rowFn(w->rowCtx, i, &row)) { continue; }
        row.selected = (i == w->selected);

        if (row.selected)
        {
            leRect hl = { area.x + 6, rowTop + 4, area.width - 12, rowH - 8 };
            leRenderer_RectFill(&hl, conv(SL_SEL_BG), 255);
            titleC = conv(SL_SEL_TITLE); artistC = conv(SL_SEL_ARTIST); rightC = conv(SL_SEL_RIGHT);
        }
        else
        {
            titleC = conv(SL_TITLE); artistC = conv(SL_ARTIST); rightC = conv(SL_RIGHT);

            leRect sep = { area.x + SL_PAD_X, rowTop + rowH - 1,
                           area.width - 2 * SL_PAD_X, 1 };
            leRenderer_RectFill(&sep, conv(SL_SEP), 255);
        }

        if (row.badge != NULL && row.badge[0] != '\0')
        {
            draw_str(w, row.badge, w->badgeFont,
                     area.x + SL_PAD_X, rowTop + (rowH - font_h(w->badgeFont)) / 2,
                     LE_HALIGN_LEFT, conv(row.badgeColor));
        }

        textX  = area.x + SL_PAD_X + SL_BADGE_COL;
        block  = titleH + SL_LINE_GAP + metaH;
        startY = rowTop + (rowH - block) / 2;

        draw_str(w, row.title,  w->titleFont, textX, startY, LE_HALIGN_LEFT, titleC);
        draw_str(w, row.artist, w->metaFont,  textX, startY + titleH + SL_LINE_GAP,
                 LE_HALIGN_LEFT, artistC);
        draw_str(w, row.right,  w->metaFont,
                 area.x + area.width - SL_PAD_X, rowTop + (rowH - metaH) / 2,
                 LE_HALIGN_RIGHT, rightC);
    }

    wgt->status.drawState = LE_WIDGET_DRAW_STATE_DONE;
    wgt->drawFunc = NULL;
}

/* ---- touch + inertia ---------------------------------------------------- */

static void sl_touchDown(leWidget *wgt, leWidgetEvent_TouchDown *evt)
{
    leSongListWidget *w = (leSongListWidget *)wgt;

    w->tracking   = true;
    w->touchId    = evt->touchID;
    w->firstY     = evt->y;
    w->lastY      = evt->y;
    w->velocity   = 0.0f;
    w->lastMoveMs = now_ms();

    leWidgetEvent_Accept((leWidgetEvent *)evt, wgt);
}

static void sl_touchMove(leWidget *wgt, leWidgetEvent_TouchMove *evt)
{
    leSongListWidget *w = (leSongListWidget *)wgt;
    uint32_t now, dtm;
    int dy;

    if (!w->tracking || (int32_t)evt->touchID != w->touchId) { return; }

    now = now_ms();
    dtm = now - w->lastMoveMs;
    dy  = evt->y - w->lastY;           /* finger down (dy>0) reveals earlier rows */

    w->scrollY -= (float)dy;           /* content follows the finger 1:1 */
    if (dtm > 0u) { w->velocity = (float)(-dy) / (float)dtm; }   /* px/ms of scrollY */
    w->lastY      = evt->y;
    w->lastMoveMs = now;

    clamp_scroll(w);
    wgt->fn->invalidate(wgt);
    leWidgetEvent_Accept((leWidgetEvent *)evt, wgt);
}

static void sl_touchUp(leWidget *wgt, leWidgetEvent_TouchUp *evt)
{
    leSongListWidget *w = (leSongListWidget *)wgt;

    if (w->tracking && (int32_t)evt->touchID == w->touchId)
    {
        int moved = evt->y - w->firstY;
        if (moved < 0) { moved = -moved; }

        if (moved < SL_TAP_SLOP)
        
        {
            /* tap: select the row under the touch */
            leRect area;
            int contentY, idx;

            wgt->fn->rectToScreen(wgt, &area);
            contentY = (evt->y - area.y) + (int)(w->scrollY + 0.5f);
            idx = (w->rowHeight > 0) ? contentY / w->rowHeight : -1;

            if (idx >= 0 && idx < w->count && idx != w->selected)
            {
                w->selected = idx;
                if (w->selFn != NULL) { w->selFn(w->selCtx, idx); }
                wgt->fn->invalidate(wgt);
            }
            w->velocity = 0.0f;        /* no fling on a tap */
        }
        else
        {
            /* cap the release fling speed */
            if (w->velocity >  SL_MAX_VEL) { w->velocity =  SL_MAX_VEL; }
            if (w->velocity < -SL_MAX_VEL) { w->velocity = -SL_MAX_VEL; }
        }

        w->tracking   = false;
        w->lastTickMs = now_ms();      /* start the inertia clock */
    }

    leWidgetEvent_Accept((leWidgetEvent *)evt, wgt);
}

static void sl_update(leWidget *wgt, uint32_t dt)
{
    leSongListWidget *w = (leSongListWidget *)wgt;
    uint32_t now, dtm;
    float decay;
    int mx;

    /* Legato calls leUpdate(0), so the framework `dt` is always 0; we measure
     * real elapsed time so motion is frame-rate independent (the update cadence
     * is render-bound and irregular under task scheduling). */
    (void)dt;

    if (w->tracking || w->velocity == 0.0f) { return; }

    now = now_ms();
    dtm = now - w->lastTickMs;
    w->lastTickMs = now;
    if (dtm == 0u) { return; }
    if (dtm > SL_MAX_DT_MS) { dtm = SL_MAX_DT_MS; }

    w->scrollY += w->velocity * (float)dtm;

    decay = 1.0f - SL_DECAY_PER_MS * (float)dtm;
    if (decay < 0.0f) { decay = 0.0f; }
    w->velocity *= decay;
    if (w->velocity < SL_MIN_VEL && w->velocity > -SL_MIN_VEL) { w->velocity = 0.0f; }

    mx = max_scroll(w);
    if (w->scrollY < 0.0f || w->scrollY > (float)mx) { w->velocity = 0.0f; }
    clamp_scroll(w);
    wgt->fn->invalidate(wgt);
}

/* ---- construction ------------------------------------------------------- */

/* Build our vtable once by copying whatever base vtable leWidget_Constructor
 * installed (this project compiles with LE_DYNAMIC_VTABLES == 0, i.e. static
 * const vtables), then overriding only the slots we implement. Copying the live
 * base vtable avoids enumerating ~70 base slots and works in either vtable mode. */
static void ensure_vtable(const leWidget *constructed)
{
    if (vtableReady) { return; }
    songListVTable = *constructed->fn;
    songListVTable._paint         = sl_paint;
    songListVTable.update         = sl_update;
    songListVTable.touchDownEvent = sl_touchDown;
    songListVTable.touchMoveEvent = sl_touchMove;
    songListVTable.touchUpEvent   = sl_touchUp;
    vtableReady = true;
}

leWidget *SongList_New(void)
{
    leSongListWidget *w = (leSongListWidget *)LE_MALLOC(sizeof(leSongListWidget));
    if (w == NULL) { return NULL; }

    leWidget_Constructor(&w->widget);   /* installs the base vtable into w->widget.fn */
    ensure_vtable(&w->widget);
    w->widget.fn = &songListVTable;
    /* We fully paint our own opaque background in sl_paint(); declare FILL so
     * Legato knows the widget is opaque (_leWidget_IsOpaque) and repaints us over
     * anything behind. We override _paint entirely, so this adds no base draw. */
    w->widget.style.backgroundType = LE_WIDGET_BACKGROUND_FILL;
    if (w->widget.scheme == NULL) { w->widget.scheme = leGetDefaultScheme(); }

    w->count     = 0;
    w->rowFn     = NULL;  w->rowCtx = NULL;
    w->selFn     = NULL;  w->selCtx = NULL;
    w->titleFont = NULL;  w->metaFont = NULL;  w->badgeFont = NULL;
    w->rowHeight   = SL_DEFAULT_ROWH;
    w->scrollY     = 0.0f;
    w->velocity    = 0.0f;
    w->selected    = -1;
    w->tracking    = false;
    w->touchId     = 0;
    w->firstY      = 0;
    w->lastY       = 0;
    w->lastMoveMs  = 0;
    w->lastTickMs  = 0;
    w->debugFill   = false;

    return &w->widget;
}

/* ---- public setters/getters -------------------------------------------- */

void SongList_SetModel(leWidget *wgt, int count, songlist_row_fn rows, void *ctx)
{
    leSongListWidget *w = (leSongListWidget *)wgt;
    if (w == NULL) { return; }
    w->count  = (count > 0) ? count : 0;
    w->rowFn  = rows;
    w->rowCtx = ctx;
    w->scrollY = 0.0f;
    w->velocity = 0.0f;
    clamp_scroll(w);
    wgt->fn->invalidate(wgt);
}

void SongList_SetSelectHandler(leWidget *wgt, songlist_select_fn fn, void *ctx)
{
    leSongListWidget *w = (leSongListWidget *)wgt;
    if (w == NULL) { return; }
    w->selFn  = fn;
    w->selCtx = ctx;
}

void SongList_SetFonts(leWidget *wgt, const leFont *title, const leFont *meta, const leFont *badge)
{
    leSongListWidget *w = (leSongListWidget *)wgt;
    if (w == NULL) { return; }
    w->titleFont = title;
    w->metaFont  = meta;
    w->badgeFont = badge;
    wgt->fn->invalidate(wgt);
}

void SongList_SetRowHeight(leWidget *wgt, int px)
{
    leSongListWidget *w = (leSongListWidget *)wgt;
    if (w == NULL || px <= 0) { return; }
    w->rowHeight = px;
    clamp_scroll(w);
    wgt->fn->invalidate(wgt);
}

int SongList_Selected(const leWidget *wgt)
{
    const leSongListWidget *w = (const leSongListWidget *)wgt;
    return (w != NULL) ? w->selected : -1;
}

uint32_t SongList_DrawCount(const leWidget *wgt)
{
    return (wgt != NULL) ? wgt->drawCount : 0u;
}

void SongList_SetDebugFill(leWidget *wgt, bool on)
{
    leSongListWidget *w = (leSongListWidget *)wgt;
    if (w == NULL) { return; }
    w->debugFill = on;
    wgt->fn->invalidate(wgt);   /* repaint immediately, no interaction needed */
}

void SongList_SetSelected(leWidget *wgt, int index)
{
    leSongListWidget *w = (leSongListWidget *)wgt;
    if (w == NULL || index < 0 || index >= w->count) { return; }

    w->selected = index;

    /* scroll the selected row fully into view */
    {
        int rowTop = index * w->rowHeight;
        int rowBot = rowTop + w->rowHeight;
        int vh     = (int)w->widget.rect.height;
        if (rowTop < (int)w->scrollY)            { w->scrollY = (float)rowTop; }
        else if (rowBot > (int)w->scrollY + vh)  { w->scrollY = (float)(rowBot - vh); }
        clamp_scroll(w);
    }
    wgt->fn->invalidate(wgt);
}
