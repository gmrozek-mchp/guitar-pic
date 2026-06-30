#ifndef UI_SCREEN_ALBUM_ART_H
#define UI_SCREEN_ALBUM_ART_H

/* Album-art layer-screen (Marvin layer 3 / CANVAS_ALBUM_ART): a 508x208 full-color
 * RGB888 surface for the song-select cover strip, composited on OVR2 above the
 * RGB565 song-select dialog (OVR1) so the cover + its baked difficulty fade keep
 * 24-bit color without paying 24bpp bandwidth for the whole dialog. MGS owns the
 * widgets on this layer (the image + the TIER/title/artist labels); this module
 * owns only the canvas surface and its on-screen window. The cover image is set
 * per selection from screen_song_select.c (Marvin_IMAGE_ALBUM_ART). */

void ScreenAlbumArt_InitSurface(void);   /* pre-scheduler: assign the canvas buffer */
void ScreenAlbumArt_Setup(void);          /* place the canvas window over the dialog art rect */

#endif /* UI_SCREEN_ALBUM_ART_H */
