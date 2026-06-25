#ifndef LE_GEN_SCREEN_SCREEN0_H
#define LE_GEN_SCREEN_SCREEN0_H

#include "gfx/legato/legato.h"

#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"

// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility
extern "C" {
#endif
// DOM-IGNORE-END

// screen member widget declarations
extern leImageWidget* Screen0_ImageWidget_0;

// screen lifecycle functions
leResult screenInit_Screen0(void); // call to initialize this screen
leResult screenShow_Screen0(void); // call to show this screen
void screenHide_Screen0(void); // call to hide this screen
void screenDestroy_Screen0(void); // call to destroy this screen
void screenUpdate_Screen0(void); // call to update this screen

leWidget* screenGetRoot_Screen0(uint32_t lyrIdx); // gets a root widget for this screen

//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

#endif // LE_GEN_SCREEN_SCREEN0_H
