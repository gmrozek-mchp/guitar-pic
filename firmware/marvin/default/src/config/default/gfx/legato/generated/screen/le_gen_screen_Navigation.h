#ifndef LE_GEN_SCREEN_NAVIGATION_H
#define LE_GEN_SCREEN_NAVIGATION_H

#include "gfx/legato/legato.h"

#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"

// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility
extern "C" {
#endif
// DOM-IGNORE-END

// screen member widget declarations
extern leWidget* Navigation_PANEL_NAVIGATION;
extern leWidget* Navigation_PANEL_NAVIGATION_TOP;
extern leWidget* Navigation_PANEL_NAVIGATION_MIDDLE;
extern leWidget* Navigation_PANEL_NAVIGATION_BOTTOM;
extern leLabelWidget* Navigation_LABEL_NAVIGATION;
extern leLabelWidget* Navigation_LABEL_NAV_SUB_HEADING;
extern leButtonWidget* Navigation_BUTTON_NAV_DASHBOARD;
extern leButtonWidget* Navigation_BUTTON_NAV_LOGS;
extern leButtonWidget* Navigation_BUTTON_NAV_PERFORMANCE;
extern leButtonWidget* Navigation_BUTTON_NAV_SYSTEM_INFO;
extern leButtonWidget* Navigation_BUTTON_NAV_DIAGNOSTICS;
extern leButtonWidget* Navigation_BUTTON_NAV_SETTINGS;
extern leWidget* Navigation_panel_Container_196;
extern leWidget* Navigation_panel_Container_197;
extern leWidget* Navigation_panel_Container_198;
extern leWidget* Navigation_panel_Container_199;
extern leWidget* Navigation_panel_Container_200;
extern leLabelWidget* Navigation_label_STATUS;
extern leLabelWidget* Navigation_label_Connected;

// screen lifecycle functions
leResult screenInit_Navigation(void); // call to initialize this screen
leResult screenShow_Navigation(void); // call to show this screen
void screenHide_Navigation(void); // call to hide this screen
void screenDestroy_Navigation(void); // call to destroy this screen
void screenUpdate_Navigation(void); // call to update this screen

leWidget* screenGetRoot_Navigation(uint32_t lyrIdx); // gets a root widget for this screen

// Screen Events:
void Navigation_OnShow(void); // called when this screen is shown

//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

#endif // LE_GEN_SCREEN_NAVIGATION_H
