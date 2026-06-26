#ifndef MARVIN_UI_WIDGET_SONG_LIST_DEMO_H
#define MARVIN_UI_WIDGET_SONG_LIST_DEMO_H

#include <stdbool.h>
#include <stdint.h>

/* Temporary bring-up scaffolding for the SongList widget: builds one instance
 * with a catalog-backed provider and attaches it to the (already-shown) Dashboard
 * screen so scroll/inertia/tap-select can be exercised on hardware before the
 * real SELECT SONG screen is authored in MGS. Driven by the `songlist` console
 * command. Remove once the song-select screen exists. */

void SongList_DemoAttach(void);

/* Remove the list and restore the dashboard. */
void SongList_DemoDetach(void);

/* Toggle the dashboard panel (for a clean background once done diagnosing). */
void SongList_DemoSetDashboard(bool show);

/* Toggle the widget's bare-solid-fill diagnostic. Returns the new state. */
bool SongList_DemoToggleFill(void);

/* Diagnostic: the widget's z-index among its parent's children (last == topmost)
 * and the sibling count. Returns -1 if not attached. */
int SongList_DemoZOrder(int *out_count);

/* Read the widget's renderer draw-count + current selection (diagnostic for the
 * overlay repaint-on-sibling-damage question). Returns false if not attached. */
bool SongList_DemoStats(uint32_t *draw_count, int *selected);

#endif
