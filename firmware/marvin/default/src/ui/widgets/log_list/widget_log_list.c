#include "ui/widgets/log_list/widget_log_list.h"

#include <string.h>

#include "gfx/legato/legato.h"
#include "gfx/legato/widget/legato_widget.h"
#include "gfx/legato/string/legato_stringutils.h"   /* measure a string to fit a column */

#include "util/legato_utf8.h"
#include "ui/gfx/text_lut.h"   /* the three text paths this widget A/Bs */

/* ---- layout ------------------------------------------------------------- */

#define LL_DEFAULT_ROWH  36     /* mockup's p-3 around a 14px line */
#define LL_PAD_X         16     /* card px-4 */

/* Columns are defined in CHARACTERS and resolved to pixels from the font's measured
 * advance, so changing the row font reflows the table — and the screen's own column
 * headings with it, since they are placed from LogList_ColumnRect. Hard-coding pixel
 * columns meant a font change silently mis-sized every one of them.
 *
 * The three fixed columns are sized to their longest possible content: "00:04:17.905",
 * "ERROR", and the longest source display name ("CV Detector"). The message column takes
 * whatever is left, which is what absorbs a larger font. */
#define LL_CH_TIME     12
#define LL_CH_LEVEL     5
#define LL_CH_SOURCE   11
#define LL_CH_GAP       2

/* Colours authored in RGB_888 and converted to the active layer mode at paint time.
 * Tailwind zinc over the card's zinc-900, and the mockup's level palette. */
#define LL_BG         0x18181Bu   /* zinc-900  */
#define LL_SEP        0x1F1F23u   /* zinc-800 at 50% over zinc-900 */
#define LL_TIME       0x71717Bu   /* zinc-500  */
#define LL_SOURCE     0xA1A1AAu   /* zinc-400  */
#define LL_SOURCE_UNK 0x52525Cu   /* zinc-600 — dimmer, so a missing tag reads as absent */
#define LL_MESSAGE    0xD4D4D8u   /* zinc-300  */

#define LL_ERROR      0xF87171u   /* red-400    */
#define LL_WARN       0xFACC15u   /* yellow-400 */
#define LL_INFO       0x60A5FAu   /* blue-400   */
#define LL_DEBUG      0x71717Bu   /* zinc-500   */

#define LL_UNKNOWN    "Unknown"

/* Longest row text handled in one piece. The capture ring's lines are 143 chars, and the
 * message column shows fewer than that, so this only has to cover the widest column. */
#define LL_TEXT_MAX   160

/* ---- widget type -------------------------------------------------------- */

typedef struct leLogListWidget
{
    leWidget widget;            /* base — must be first */

    int             count;
    loglist_row_fn  rowFn;
    void           *rowCtx;

    const leFont *font;
    const char   *emptyText;

    int rowHeight;
    int scrollY;                /* px scrolled from the top; integer — there is no fling */

    bool    tracking;
    int32_t touchId;
    int32_t lastY;
} leLogListWidget;

static leWidgetVTable logListVTable;
static bool           vtableReady = false;

/* One instance — the log screen is the only user, and static storage keeps the widget out
 * of the Legato pool (the bus screen's approach, and the project's static-allocation
 * rule). */
static leLogListWidget s_inst;
static bool            s_taken = false;

/* Resolved column geometry and the advance it was resolved from. Module scope because
 * there is one instance, like s_path below. */
static struct { int x, w; } s_col[LOGLIST_COL_COUNT];
static int                  s_cw;

/* Which text path the cells use. Shared by all instances (there is one) so the `log text`
 * command can A/B it at runtime — this screen is the most text-heavy in the UI, so it is
 * where the three paths are worth comparing. Defaults to the lookup path; whether that is
 * actually the fastest here is a measurement, not a claim (see ui/gfx/text_lut.h). */
static text_path_t s_path = TEXT_PATH_LUT;

/* ---- helpers ------------------------------------------------------------ */

static leColor conv(leColor rgb888)
{
    return leColorConvert(LE_COLOR_MODE_RGB_888, leRenderer_CurrentColorMode(), rgb888);
}

static int font_h(const leFont *f)
{
    return (f != NULL) ? (int)((const leRasterFont *)f)->height : 0;
}

static int max_scroll(const leLogListWidget *w)
{
    int content = w->count * w->rowHeight;
    int vh      = (int)w->widget.rect.height;
    return (content > vh) ? (content - vh) : 0;
}

static void clamp_scroll(leLogListWidget *w)
{
    int mx = max_scroll(w);
    if (w->scrollY < 0)  { w->scrollY = 0;  }
    else if (w->scrollY > mx) { w->scrollY = mx; }
}

static leColor level_color(log_level_t lvl)
{
    switch (lvl)
    {
        case LOG_LEVEL_ERROR: return LL_ERROR;
        case LOG_LEVEL_WARN:  return LL_WARN;
        case LOG_LEVEL_INFO:  return LL_INFO;
        default:              return LL_DEBUG;
    }
}

static const char *level_text(log_level_t lvl)
{
    switch (lvl)
    {
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_INFO:  return "INFO";
        default:              return "DEBUG";
    }
}

/* Width of one character in the (monospaced) row font, in px. Measured rather than
 * assumed so a font swap cannot silently mis-size the columns. */
static int char_w(const leFont *f)
{
    leChar  probe = (leChar)'0';
    leRect  r     = leRect_Zero;

    if (f == NULL) { return 0; }
    if (leStringUtils_GetRect(&probe, 1u, f, &r) != LE_SUCCESS) { return 0; }
    return (int)r.width;
}

/* Draw `s` left-aligned at (x,y), clipped to `maxw` px by truncating with "...".
 *
 * Truncation is by character count because the font is monospaced; ASCII dots rather than
 * U+2026 because the design's fonts carry ASCII+Latin-1 and no ellipsis glyph. `cw` is the
 * font's character width, measured once per paint by the caller — measuring it per cell
 * would be ~70 string measurements a repaint for one unchanging number. */
static void draw_cell(const leLogListWidget *w, const char *s, int x, int y,
                      int maxw, int cw, leColor rgb888)
{
    leChar buf[LL_TEXT_MAX];
    uint32_t len;

    if (s == NULL || s[0] == '\0' || w->font == NULL || maxw <= 0) { return; }

    /* Decode UTF-8 to code points; the C-string renderer would draw each byte of a
     * multibyte sequence as its own glyph. */
    len = utf8_to_lechar(s, buf, sizeof buf / sizeof buf[0]);
    if (len == 0u) { return; }

    if (cw > 0)
    {
        uint32_t fits = (uint32_t)(maxw / cw);
        if (fits == 0u) { return; }
        if (len > fits)
        {
            len = fits;
            if (len >= 3u)
            {
                buf[len - 3u] = (leChar)'.';
                buf[len - 2u] = (leChar)'.';
                buf[len - 1u] = (leChar)'.';
            }
        }
    }

    /* Colour is passed in RGB_888 so the lookup path can build its ramp from it; every path
     * converts to the render mode itself. LL_BG is the background because ll_paint fills the
     * whole rect with it before any cell is drawn, which is what makes the ramp valid. */
    TextLut_DrawLine(w->font, x, y, buf, len, rgb888, LL_BG, s_path);
}

/* Resolve the column pixels from the font's advance and the widget's width. Called whenever
 * either could have changed (font or size), so ColumnRect and the paint always agree.
 *
 * The message column takes the remainder, so a bigger font costs message characters rather
 * than pushing a column off the right edge. */
static void layout_columns(const leLogListWidget *w)
{
    s_cw = char_w(w->font);

    for (int c = 0; c < (int)LOGLIST_COL_COUNT; c++) { s_col[c].x = 0; s_col[c].w = 0; }
    if (s_cw <= 0) { return; }

    const int chars[3] = { LL_CH_TIME, LL_CH_LEVEL, LL_CH_SOURCE };
    int x = LL_PAD_X;

    for (int c = 0; c < 3; c++)
    {
        s_col[c].x = x;
        s_col[c].w = chars[c] * s_cw;
        x += (chars[c] + LL_CH_GAP) * s_cw;
    }

    int avail = (int)w->widget.rect.width - x - LL_PAD_X;
    s_col[LOGLIST_COL_MESSAGE].x = x;
    s_col[LOGLIST_COL_MESSAGE].w = (avail > 0) ? avail : 0;
}

/* ---- paint -------------------------------------------------------------- */

static void ll_paint(leWidget *wgt)
{
    leLogListWidget *w = (leLogListWidget *)wgt;
    leRect area;
    int rowH, vh, first, firstTop, textH, cw, i;

    if (wgt->status.drawState == LE_WIDGET_DRAW_STATE_DONE) { return; }

    wgt->fn->rectToScreen(wgt, &area);
    leRenderer_RectFill(&area, conv(LL_BG), 255);

    rowH = w->rowHeight;
    if (rowH <= 0 || w->font == NULL)
    {
        wgt->status.drawState = LE_WIDGET_DRAW_STATE_DONE;
        wgt->drawFunc = NULL;
        return;
    }

    cw = char_w(w->font);

    if (w->count <= 0)
    {
        const char *msg = (w->emptyText != NULL) ? w->emptyText : "No log entries";
        draw_cell(w, msg, area.x + LL_PAD_X,
                  area.y + ((int)area.height - font_h(w->font)) / 2,
                  (int)area.width - 2 * LL_PAD_X, cw, LL_SOURCE_UNK);

        wgt->status.drawState = LE_WIDGET_DRAW_STATE_DONE;
        wgt->drawFunc = NULL;
        return;
    }

    vh       = (int)area.height;
    first    = w->scrollY / rowH;
    if (first < 0) { first = 0; }
    firstTop = area.y - (w->scrollY - first * rowH);
    textH    = font_h(w->font);

    for (i = first; i < w->count; i++)
    {
        int rowTop = firstTop + (i - first) * rowH;
        loglist_row_t row;
        int textY;

        if (rowTop >= area.y + vh) { break; }

        memset(&row, 0, sizeof row);
        if (w->rowFn == NULL || !w->rowFn(w->rowCtx, i, &row)) { continue; }

        leRect sep = { area.x, rowTop + rowH - 1, area.width, 1 };
        leRenderer_RectFill(&sep, conv(LL_SEP), 255);

        textY = rowTop + (rowH - textH) / 2;

        draw_cell(w, row.time, area.x + s_col[LOGLIST_COL_TIME].x, textY,
                  s_col[LOGLIST_COL_TIME].w, cw, LL_TIME);

        draw_cell(w, level_text(row.level), area.x + s_col[LOGLIST_COL_LEVEL].x, textY,
                  s_col[LOGLIST_COL_LEVEL].w, cw, level_color(row.level));

        bool known = (row.source != NULL) && (row.source[0] != '\0');
        draw_cell(w, known ? row.source : LL_UNKNOWN,
                  area.x + s_col[LOGLIST_COL_SOURCE].x, textY,
                  s_col[LOGLIST_COL_SOURCE].w, cw,
                  known ? LL_SOURCE : LL_SOURCE_UNK);

        draw_cell(w, row.message, area.x + s_col[LOGLIST_COL_MESSAGE].x, textY,
                  s_col[LOGLIST_COL_MESSAGE].w, cw, LL_MESSAGE);
    }

    wgt->status.drawState = LE_WIDGET_DRAW_STATE_DONE;
    wgt->drawFunc = NULL;
}

/* ---- touch -------------------------------------------------------------- */

static void ll_touchDown(leWidget *wgt, leWidgetEvent_TouchDown *evt)
{
    leLogListWidget *w = (leLogListWidget *)wgt;

    w->tracking = true;
    w->touchId  = evt->touchID;
    w->lastY    = evt->y;

    leWidgetEvent_Accept((leWidgetEvent *)evt, wgt);
}

static void ll_touchMove(leWidget *wgt, leWidgetEvent_TouchMove *evt)
{
    leLogListWidget *w = (leLogListWidget *)wgt;
    int dy, before;

    if (!w->tracking || (int32_t)evt->touchID != w->touchId) { return; }

    dy     = evt->y - w->lastY;        /* finger down (dy>0) reveals earlier rows */
    before = w->scrollY;

    w->scrollY -= dy;                  /* content follows the finger 1:1 */
    w->lastY    = evt->y;
    clamp_scroll(w);

    /* Repainting the list is the most expensive thing this screen does, so a move that
     * changed nothing — a horizontal drag, or a pull past either end — must not queue a
     * frame for it. */
    if (w->scrollY != before) { wgt->fn->invalidate(wgt); }

    leWidgetEvent_Accept((leWidgetEvent *)evt, wgt);
}

static void ll_touchUp(leWidget *wgt, leWidgetEvent_TouchUp *evt)
{
    leLogListWidget *w = (leLogListWidget *)wgt;

    if (w->tracking && (int32_t)evt->touchID == w->touchId) { w->tracking = false; }

    leWidgetEvent_Accept((leWidgetEvent *)evt, wgt);
}

/* ---- construction ------------------------------------------------------- */

/* Copy whatever base vtable leWidget_Constructor installed, then override only the slots
 * implemented here — the song_list approach, which avoids enumerating ~70 base slots and
 * works in either LE_DYNAMIC_VTABLES mode. `update` is deliberately not overridden: it is
 * never called in this build (see the header). */
static void ensure_vtable(const leWidget *constructed)
{
    if (vtableReady) { return; }
    logListVTable = *constructed->fn;
    logListVTable._paint         = ll_paint;
    logListVTable.touchDownEvent = ll_touchDown;
    logListVTable.touchMoveEvent = ll_touchMove;
    logListVTable.touchUpEvent   = ll_touchUp;
    vtableReady = true;
}

leWidget *LogList_New(void)
{
    if (s_taken) { return NULL; }
    s_taken = true;

    leLogListWidget *w = &s_inst;

    /* In-place construction, exactly as leX_New would after LE_MALLOC. */
    leWidget_Constructor(&w->widget);
    ensure_vtable(&w->widget);
    w->widget.fn = &logListVTable;

    /* ll_paint fills the whole rect, so declare FILL and let _leWidget_IsOpaque skip
     * repainting the card behind us. */
    w->widget.style.backgroundType = LE_WIDGET_BACKGROUND_FILL;
    if (w->widget.scheme == NULL) { w->widget.scheme = leGetDefaultScheme(); }

    w->count     = 0;
    w->rowFn     = NULL;
    w->rowCtx    = NULL;
    w->font      = NULL;
    w->emptyText = NULL;
    w->rowHeight = LL_DEFAULT_ROWH;
    w->scrollY   = 0;
    w->tracking  = false;
    w->touchId   = 0;
    w->lastY     = 0;

    return &w->widget;
}

/* ---- setters / getters -------------------------------------------------- */

void LogList_SetModel(leWidget *wgt, int count, loglist_row_fn rows, void *ctx)
{
    leLogListWidget *w = (leLogListWidget *)wgt;
    if (w == NULL) { return; }

    w->count   = (count > 0) ? count : 0;
    w->rowFn   = rows;
    w->rowCtx  = ctx;
    w->scrollY = 0;
    clamp_scroll(w);
    wgt->fn->invalidate(wgt);
}

void LogList_SetCount(leWidget *wgt, int count)
{
    leLogListWidget *w = (leLogListWidget *)wgt;
    if (w == NULL) { return; }

    int n = (count > 0) ? count : 0;
    if (n == w->count) { return; }

    /* Away from the top, hold the operator's place: n - count new rows were inserted
     * above everything on screen, so the offset moves with them. At the top, stay there
     * and let the new lines appear. */
    if (w->scrollY > 0 && n > w->count) { w->scrollY += (n - w->count) * w->rowHeight; }

    w->count = n;
    clamp_scroll(w);
    wgt->fn->invalidate(wgt);
}

void LogList_SetFont(leWidget *wgt, const leFont *text)
{
    leLogListWidget *w = (leLogListWidget *)wgt;
    if (w == NULL) { return; }
    w->font = text;
    layout_columns(w);   /* column pixels follow the font's advance */
    wgt->fn->invalidate(wgt);
}

void LogList_SetRowHeight(leWidget *wgt, int px)
{
    leLogListWidget *w = (leLogListWidget *)wgt;
    if (w == NULL || px <= 0) { return; }
    w->rowHeight = px;
    clamp_scroll(w);
    wgt->fn->invalidate(wgt);
}

void LogList_SetEmptyText(leWidget *wgt, const char *text)
{
    leLogListWidget *w = (leLogListWidget *)wgt;
    if (w == NULL) { return; }
    w->emptyText = text;   /* not copied — pass a literal or other stable pointer */
    wgt->fn->invalidate(wgt);
}

void LogList_ColumnRect(const leWidget *wgt, loglist_col_t col, int *x, int *width)
{
    const leLogListWidget *w = (const leLogListWidget *)wgt;
    if (w == NULL || col >= LOGLIST_COL_COUNT) { return; }

    if (x     != NULL) { *x     = s_col[col].x; }
    if (width != NULL) { *width = s_col[col].w; }
}

bool LogList_Dragging(const leWidget *wgt)
{
    const leLogListWidget *w = (const leLogListWidget *)wgt;
    return (w != NULL) && w->tracking;
}

void LogList_SetTextPath(leWidget *wgt, text_path_t path)
{
    s_path = path;
    if (wgt != NULL) { wgt->fn->invalidate(wgt); }
}

text_path_t LogList_TextPath(void)
{
    return s_path;
}

int LogList_VisibleRows(const leWidget *wgt)
{
    const leLogListWidget *w = (const leLogListWidget *)wgt;
    if (w == NULL || w->rowHeight <= 0) { return 0; }
    return (int)w->widget.rect.height / w->rowHeight;
}
