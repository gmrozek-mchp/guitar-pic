#ifndef LE_GEN_SCREEN_MARVIN_H
#define LE_GEN_SCREEN_MARVIN_H

#include "gfx/legato/legato.h"

#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"

// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility
extern "C" {
#endif
// DOM-IGNORE-END

// screen member widget declarations
extern leWidget* Marvin_PANEL_DASHBOARD;
extern leWidget* Marvin_PANEL_NAVIGATION;
extern leWidget* Marvin_PANEL_SONG_SELECT;
extern leWidget* Marvin_PANEL_SONG_SELECT_TOP;
extern leWidget* Marvin_PANEL_SONG_SELECT_BOTTOM;
extern leLabelWidget* Marvin_LABEL_SELECT_SONG;
extern leButtonWidget* Marvin_BUTTON_SONG_SELECT_CLOSE;
extern leWidget* Marvin_PANEL_SONG_SELECT_LEFT;
extern leWidget* Marvin_PANEL_SONG_SELECT_CENTER;
extern leWidget* Marvin_PANEL_SONG_SELECT_RIGHT;
extern leWidget* Marvin_PANEL_SETLIST;
extern leLabelWidget* Marvin_LABEL_SETLIST;
extern leWidget* Marvin_PANEL_SONG_SELECT_SONG_INFO;
extern leLabelWidget* Marvin_LABEL_SONG_SELECT_ALBUM;
extern leLabelWidget* Marvin_LABEL_SONG_SELECT_SongAlbum;
extern leLabelWidget* Marvin_LABEL_SONG_SELECT_YEAR;
extern leLabelWidget* Marvin_LABEL_SONG_SELECT_SongYear;
extern leLabelWidget* Marvin_LABEL_SONG_SELECT_GENRE;
extern leLabelWidget* Marvin_LABEL_SONG_SELECT_SongGenre;
extern leLabelWidget* Marvin_LABEL_SONG_SELECT_DURATION;
extern leLabelWidget* Marvin_LABEL_SONG_SELECT_SongDuration;
extern leWidget* Marvin_PANEL_SONG_SELECT_DIFFICULTY;
extern leWidget* Marvin_PANEL_SONG_SELECT_MODE;
extern leButtonWidget* Marvin_BUTTON_SONG_SELECT_SELECT;
extern leLabelWidget* Marvin_LABEL_SONG_SELECT_DIFFICULTY;
extern leButtonWidget* Marvin_BUTTON_SONG_SELECT_EASY;
extern leButtonWidget* Marvin_BUTTON_SONG_SELECT_MEDIUM;
extern leButtonWidget* Marvin_BUTTON_SONG_SELECT_HARD;
extern leButtonWidget* Marvin_BUTTON_SONG_SELECT_EXPERT;
extern leLabelWidget* Marvin_LABEL_SONG_SELECT_MODE;
extern leButtonWidget* Marvin_BUTTON_SONG_SELECT_1P_ROBOT;
extern leButtonWidget* Marvin_BUTTON_SONG_SELECT_1P_HUMAN;
extern leButtonWidget* Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN;
extern leWidget* Marvin_PANEL_SONG_SELECT_ALBUM_ART;
extern leImageWidget* Marvin_IMAGE_ALBUM_ART;
extern leWidget* Marvin_PANEL_ALBUM_ART_OVERLAY;
extern leLabelWidget* Marvin_LABEL_SONG_SELECT_SongTier;
extern leLabelWidget* Marvin_LABEL_SONG_SELECT_SongTitle;
extern leLabelWidget* Marvin_LABEL_SONG_SELECT_Artist;
extern leWidget* Marvin_PANEL_WIIMOTES;
extern leWidget* Marvin_PANEL_KEYBOARD;
extern leWidget* Marvin_PANEL_BUS;

// screen lifecycle functions
leResult screenInit_Marvin(void); // call to initialize this screen
leResult screenShow_Marvin(void); // call to show this screen
void screenHide_Marvin(void); // call to hide this screen
void screenDestroy_Marvin(void); // call to destroy this screen
void screenUpdate_Marvin(void); // call to update this screen

leWidget* screenGetRoot_Marvin(uint32_t lyrIdx); // gets a root widget for this screen

//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

#endif // LE_GEN_SCREEN_MARVIN_H
