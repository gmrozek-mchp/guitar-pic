#ifndef LE_GEN_SCREEN_SONGSELECT_H
#define LE_GEN_SCREEN_SONGSELECT_H

#include "gfx/legato/legato.h"

#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"

// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility
extern "C" {
#endif
// DOM-IGNORE-END

// screen member widget declarations
extern leWidget* SongSelect_PANEL_SONG_SELECT;
extern leWidget* SongSelect_PANEL_SONG_SELECT_TOP;
extern leWidget* SongSelect_PANEL_SONG_SELECT_BOTTOM;
extern leLabelWidget* SongSelect_LABEL_SELECT_SONG;
extern leButtonWidget* SongSelect_BUTTON_SONG_SELECT_CLOSE;
extern leWidget* SongSelect_PANEL_SONG_SELECT_LEFT;
extern leWidget* SongSelect_PANEL_SONG_SELECT_CENTER;
extern leWidget* SongSelect_PANEL_SONG_SELECT_RIGHT;
extern leWidget* SongSelect_PANEL_SETLIST;
extern leLabelWidget* SongSelect_LABEL_SETLIST;
extern leImageWidget* SongSelect_IMAGE_ALBUM_ART;
extern leWidget* SongSelect_PANEL_ALBUM_ART_OVERLAY;
extern leLabelWidget* SongSelect_LABEL_SONG_SELECT_SONG_LEVEL;
extern leLabelWidget* SongSelect_LABEL_SONG_SELECT_SONG_TITLE;
extern leLabelWidget* SongSelect_LABEL_SONG_SELECT_SONG_ARTIST;
extern leWidget* SongSelect_PANEL_SONG_SELECT_SONG_INFO;
extern leLabelWidget* SongSelect_LABEL_SONG_SELECT_ALBUM;
extern leLabelWidget* SongSelect_LABEL_SONG_SELECT_SongAlbum;
extern leLabelWidget* SongSelect_LABEL_SONG_SELECT_YEAR;
extern leLabelWidget* SongSelect_LABEL_SONG_SELECT_SongYear;
extern leLabelWidget* SongSelect_LABEL_SONG_SELECT_GENRE;
extern leLabelWidget* SongSelect_LABEL_SONG_SELECT_SongGenre;
extern leLabelWidget* SongSelect_LABEL_SONG_SELECT_DURATION;
extern leLabelWidget* SongSelect_LABEL_SONG_SELECT_SongDuration;
extern leWidget* SongSelect_PANEL_SONG_SELECT_DIFFICULTY;
extern leWidget* SongSelect_PANEL_SONG_SELECT_MODE;
extern leButtonWidget* SongSelect_BUTTON_SONG_SELECT_SELECT;
extern leLabelWidget* SongSelect_LABEL_SONG_SELECT_DIFFICULTY;
extern leButtonWidget* SongSelect_BUTTON_SONG_SELECT_EASY;
extern leButtonWidget* SongSelect_BUTTON_SONG_SELECT_MEDIUM;
extern leButtonWidget* SongSelect_BUTTON_SONG_SELECT_HARD;
extern leButtonWidget* SongSelect_BUTTON_SONG_SELECT_EXPERT;
extern leLabelWidget* SongSelect_LABEL_SONG_SELECT_MODE;
extern leButtonWidget* SongSelect_BUTTON_SONG_SELECT_1P_ROBOT;
extern leButtonWidget* SongSelect_BUTTON_SONG_SELECT_1P_HUMAN;
extern leButtonWidget* SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN;

// screen lifecycle functions
leResult screenInit_SongSelect(void); // call to initialize this screen
leResult screenShow_SongSelect(void); // call to show this screen
void screenHide_SongSelect(void); // call to hide this screen
void screenDestroy_SongSelect(void); // call to destroy this screen
void screenUpdate_SongSelect(void); // call to update this screen

leWidget* screenGetRoot_SongSelect(uint32_t lyrIdx); // gets a root widget for this screen

// Screen Events:
void SongSelect_OnShow(void); // called when this screen is shown

//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

#endif // LE_GEN_SCREEN_SONGSELECT_H
