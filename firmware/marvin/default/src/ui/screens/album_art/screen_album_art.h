#ifndef UI_SCREEN_ALBUM_ART_H
#define UI_SCREEN_ALBUM_ART_H

/* Album-art layer-screen (Marvin layer 3 / CANVAS_ALBUM_ART): a 508x208 full-color
 * RGBA8888 surface for the song-select cover strip, composited on OVR2 above the
 * RGB565 song-select dialog (OVR1) so the cover + its baked difficulty fade keep
 * 24-bit color without paying 32bpp bandwidth for the whole dialog — and so switching
 * songs repaints only this 0.42 MB surface.
 *
 * This module owns only the canvas surface and its on-screen window; the widgets on
 * the layer (the cover, its corner overlay, and the tier/title/artist lines) are built
 * and fed by screen_song_select.c, which owns the selection they show. The window's
 * position comes from that module's layout (ScreenSongSelect_ArtOrigin). */

void ScreenAlbumArt_InitSurface(void);   /* pre-scheduler: assign the canvas buffer */
void ScreenAlbumArt_Setup(void);          /* place the canvas window over the dialog art rect */

#endif /* UI_SCREEN_ALBUM_ART_H */
