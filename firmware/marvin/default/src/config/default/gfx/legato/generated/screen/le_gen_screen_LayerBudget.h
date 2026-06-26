#ifndef LE_GEN_SCREEN_LAYERBUDGET_H
#define LE_GEN_SCREEN_LAYERBUDGET_H

#include "gfx/legato/legato.h"

#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"

// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility
extern "C" {
#endif
// DOM-IGNORE-END

// screen lifecycle functions
leResult screenInit_LayerBudget(void); // call to initialize this screen
leResult screenShow_LayerBudget(void); // call to show this screen
void screenHide_LayerBudget(void); // call to hide this screen
void screenDestroy_LayerBudget(void); // call to destroy this screen
void screenUpdate_LayerBudget(void); // call to update this screen

leWidget* screenGetRoot_LayerBudget(uint32_t lyrIdx); // gets a root widget for this screen

//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

#endif // LE_GEN_SCREEN_LAYERBUDGET_H
