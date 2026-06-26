#ifndef LE_GEN_SCREEN_SPLASH_H
#define LE_GEN_SCREEN_SPLASH_H

#include "gfx/legato/legato.h"

#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"

// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility
extern "C" {
#endif
// DOM-IGNORE-END

// screen member widget declarations
extern leWidget* Splash_Panel_0;

// screen lifecycle functions
leResult screenInit_Splash(void); // call to initialize this screen
leResult screenShow_Splash(void); // call to show this screen
void screenHide_Splash(void); // call to hide this screen
void screenDestroy_Splash(void); // call to destroy this screen
void screenUpdate_Splash(void); // call to update this screen

leWidget* screenGetRoot_Splash(uint32_t lyrIdx); // gets a root widget for this screen

//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

#endif // LE_GEN_SCREEN_SPLASH_H
