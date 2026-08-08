#ifndef MARVIN_UI_WIDGET_LOG_LIST_H
#define MARVIN_UI_WIDGET_LOG_LIST_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/legato/legato.h"   /* leWidget, leColor, leFont */

#include "log.h"                 /* log_level_t — the level drives the row's colour */
#include "ui/gfx/text_lut.h"     /* text_path_t — runtime A/B of the three text paths */

/* Scrollable, touch-driven log table (a custom leWidget subclass) for the ACTIVITY LOG
 * screen. Four monospaced columns — time, level, source, message — over a zinc-900 fill.
 *
 * Decoupled from the capture ring: the owner registers a row provider, so the widget
 * never includes log_ring.h and the screen owns the entry -> row mapping (uptime
 * formatting, the tag -> display-name table).
 *
 * Row 0 is the top row. The log screen feeds it newest-first, so scroll offset 0 always
 * shows the newest line and no auto-scroll is needed.
 *
 * Interaction is a 1:1 drag — the content follows the finger — and nothing else. There is
 * deliberately no release fling: `update()` is never called in this build (Legato's 100 Hz
 * widget walk is compiled out, see the MARVIN_ANY_UPDATING_WIDGET patch in
 * legato_state.c), so inertia driven from that hook would be dead code, and driving it
 * from a task would mean repainting a 1248px-wide list at animation rates.
 *
 * Painted virtualized — only visible rows are drawn, via the Legato renderer, with no
 * per-row child widgets. That keeps the whole list at one widget: the screen would
 * otherwise add ~400 labels to every pass that walks the widget tree.
 *
 * Statically allocated — one instance, since the log screen is the only user. */

typedef struct
{
    const char *time;       /* "00:04:17.905" */
    log_level_t level;
    const char *source;     /* display name; NULL / "" draws the unknown placeholder */
    const char *message;
} loglist_row_t;

/* Fill *out for row `index`; return false if index is out of range or unreadable. Called
 * during paint for visible rows only — keep it cheap and non-blocking. */
typedef bool (*loglist_row_fn)(void *ctx, int index, loglist_row_t *out);

/* The singleton. NULL if already handed out. */
leWidget *LogList_New(void);

void LogList_SetModel(leWidget *w, int count, loglist_row_fn rows, void *ctx);

/* Update just the row count, as lines arrive.
 *
 * Holds the reader's place: because row 0 is the newest, `n` new lines push everything
 * the operator is looking at down by n rows, so a list that is scrolled away from the top
 * has its offset advanced to match. At the top it stays at the top and the new lines
 * simply appear. */
void LogList_SetCount(leWidget *w, int count);

void LogList_SetFont(leWidget *w, const leFont *text);
void LogList_SetRowHeight(leWidget *w, int px);
void LogList_SetEmptyText(leWidget *w, const char *text);   /* not copied */

/* Column left edges and widths, widget-relative px. The screen uses these to line its
 * own column headings up with the rows, so the two cannot drift apart. */
typedef enum { LOGLIST_COL_TIME, LOGLIST_COL_LEVEL, LOGLIST_COL_SOURCE,
               LOGLIST_COL_MESSAGE, LOGLIST_COL_COUNT } loglist_col_t;

void LogList_ColumnRect(const leWidget *w, loglist_col_t col, int *x, int *width);

/* True while a drag is in progress — the screen skips its refresh repaint so a scroll is
 * not fighting a telemetry update for the render lock. */
bool LogList_Dragging(const leWidget *w);

/* Rows that fit in the widget's height. For the screen's footer and for the probe. */
int LogList_VisibleRows(const leWidget *w);

/* Swap the glyph-rendering path at runtime, so `log probe` can measure all three against
 * each other on hardware instead of trusting a prediction. Applies to every cell. */
void        LogList_SetTextPath(leWidget *w, text_path_t path);
text_path_t LogList_TextPath(void);

#endif /* MARVIN_UI_WIDGET_LOG_LIST_H */
