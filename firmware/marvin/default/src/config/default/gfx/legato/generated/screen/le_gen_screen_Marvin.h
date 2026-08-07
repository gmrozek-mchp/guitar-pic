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
extern leWidget* Marvin_PANEL_SONG_SELECT_ALBUM_ART;
extern leWidget* Marvin_PANEL_WIIMOTES;
extern leWidget* Marvin_PANEL_KEYBOARD;
extern leWidget* Marvin_PANEL_BUS;
extern leWidget* Marvin_PANEL_SYSTEM;
extern leWidget* Marvin_PANEL_SYSTEM_DETAIL;

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
