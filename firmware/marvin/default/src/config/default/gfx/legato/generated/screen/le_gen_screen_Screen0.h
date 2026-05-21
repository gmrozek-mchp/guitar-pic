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
extern leWidget* Screen0_BackgroundPanel;
extern leImageWidget* Screen0_ImageWidget_0;
extern leButtonWidget* Screen0_Button_Manual_Green;
extern leButtonWidget* Screen0_Button_Manual_Red;
extern leButtonWidget* Screen0_Button_Manual_Yellow;
extern leButtonWidget* Screen0_Button_Manual_Blue;
extern leButtonWidget* Screen0_Button_Manual_Orange;
extern leButtonWidget* Screen0_Button_Manual_StrumDown;
extern leButtonWidget* Screen0_Button_Manual_StrumUp;
extern leButtonWidget* Screen0_Button_Manual_Enable;

// event handlers
// !!THESE MUST BE IMPLEMENTED IN THE APPLICATION CODE!!
void event_Screen0_Button_Manual_Green_OnPressed(leButtonWidget* btn);
void event_Screen0_Button_Manual_Green_OnReleased(leButtonWidget* btn);
void event_Screen0_Button_Manual_Red_OnPressed(leButtonWidget* btn);
void event_Screen0_Button_Manual_Red_OnReleased(leButtonWidget* btn);
void event_Screen0_Button_Manual_Yellow_OnPressed(leButtonWidget* btn);
void event_Screen0_Button_Manual_Yellow_OnReleased(leButtonWidget* btn);
void event_Screen0_Button_Manual_Blue_OnPressed(leButtonWidget* btn);
void event_Screen0_Button_Manual_Blue_OnReleased(leButtonWidget* btn);
void event_Screen0_Button_Manual_Orange_OnPressed(leButtonWidget* btn);
void event_Screen0_Button_Manual_Orange_OnReleased(leButtonWidget* btn);
void event_Screen0_Button_Manual_StrumDown_OnPressed(leButtonWidget* btn);
void event_Screen0_Button_Manual_StrumDown_OnReleased(leButtonWidget* btn);
void event_Screen0_Button_Manual_StrumUp_OnPressed(leButtonWidget* btn);
void event_Screen0_Button_Manual_StrumUp_OnReleased(leButtonWidget* btn);
void event_Screen0_Button_Manual_Enable_OnReleased(leButtonWidget* btn);

// screen lifecycle functions
// DO NOT CALL THESE DIRECTLY
leResult screenInit_Screen0(void); // called when Legato is initialized
leResult screenShow_Screen0(void); // called when screen is shown
void screenHide_Screen0(void); // called when screen is hidden
void screenDestroy_Screen0(void); // called when Legato is destroyed
void screenUpdate_Screen0(void); // called when Legato is updating

leWidget* screenGetRoot_Screen0(uint32_t lyrIdx); // gets a root widget for this screen

//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

#endif // LE_GEN_SCREEN_SCREEN0_H
