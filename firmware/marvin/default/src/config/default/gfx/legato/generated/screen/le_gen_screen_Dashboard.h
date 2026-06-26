#ifndef LE_GEN_SCREEN_DASHBOARD_H
#define LE_GEN_SCREEN_DASHBOARD_H

#include "gfx/legato/legato.h"

#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"

// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility
extern "C" {
#endif
// DOM-IGNORE-END

// screen member widget declarations
extern leWidget* Dashboard_PANEL_BASE;
extern leWidget* Dashboard_PANEL_BASE_TOP;
extern leWidget* Dashboard_PANEL_BASE_DIVIDER;
extern leWidget* Dashboard_PANEL_BASE_BOTTOM;
extern leWidget* Dashboard_PANEL_SYSTEM_LEFT;
extern leWidget* Dashboard_PANEL_SYSTEM_RIGHT;
extern leButtonWidget* Dashboard_BUTTON_SYSYEM_NAVIGATION;
extern leWidget* Dashboard_PANEL_SYSTEM_TITLE;
extern leLabelWidget* Dashboard_LABEL_GUITAR_PIC;
extern leLabelWidget* Dashboard_LABEL_MARVIN;
extern leButtonWidget* Dashboard_BUTTON_SYSTEM_ACTIVE;
extern leImageScaleWidget* Dashboard_IMAGE_MICROCHIP;
extern leWidget* Dashboard_PANEL_BASE_LEFT;
extern leWidget* Dashboard_PANEL_BASE_CENTER;
extern leWidget* Dashboard_PANEL_BASE_RIGHT;
extern leWidget* Dashboard_PANEL_ROBOT_CONTROLS;
extern leWidget* Dashboard_PANEL_PERFORMANCE;
extern leLabelWidget* Dashboard_LABEL_ROBOT_CONTROL;
extern leWidget* Dashboard_panel_Container_7;
extern leWidget* Dashboard_panel_Button__margin_;
extern leWidget* Dashboard_panel_Contas;
extern leWidget* Dashboard_panel_Button;
extern leWidget* Dashboard_panel_Button_0;
extern leWidget* Dashboard_panel_Icon_0;
extern leLabelWidget* Dashboard_label_START;
extern leImageWidget* Dashboard_image_Icon_0;
extern leWidget* Dashboard_panel_Icon_1;
extern leLabelWidget* Dashboard_label_CALIBRATE;
extern leImageWidget* Dashboard_image_Icon_1;
extern leWidget* Dashboard_panel_Button_1;
extern leWidget* Dashboard_panel_Icon_2;
extern leLabelWidget* Dashboard_label_EMERGENCY_STOP;
extern leImageWidget* Dashboard_image_Icon_2;
extern leWidget* Dashboard_panel_Container_8;
extern leWidget* Dashboard_panel_Container_9;
extern leWidget* Dashboard_panel_Container_10;
extern leWidget* Dashboard_panel_Container_11;
extern leWidget* Dashboard_panel_Text;
extern leWidget* Dashboard_panel_Text_0;
extern leLabelWidget* Dashboard_label_Calibration;
extern leLabelWidget* Dashboard_label_READY;
extern leWidget* Dashboard_panel_Text_1;
extern leWidget* Dashboard_panel_Text_2;
extern leLabelWidget* Dashboard_label_Motor_Status;
extern leLabelWidget* Dashboard_label_OPERATIONAL;
extern leWidget* Dashboard_panel_Text_3;
extern leWidget* Dashboard_panel_Text_4;
extern leLabelWidget* Dashboard_label_Servo_Power;
extern leLabelWidget* Dashboard_label__12_4V;
extern leLabelWidget* Dashboard_LABEL_PERFORMANCE;
extern leWidget* Dashboard_panel_Container_12;
extern leWidget* Dashboard_panel_Container_13;
extern leWidget* Dashboard_panel_Container__margin__0;
extern leWidget* Dashboard_panel_Container__margin__3;
extern leWidget* Dashboard_panel_Container__margin__5;
extern leWidget* Dashboard_panel_Container_14;
extern leWidget* Dashboard_panel_Container_15;
extern leWidget* Dashboard_panel_Icon_3;
extern leWidget* Dashboard_panel_Text_5;
extern leImageWidget* Dashboard_image_Icon_3;
extern leLabelWidget* Dashboard_label_SCORE;
extern leLabelWidget* Dashboard_label__135_416;
extern leWidget* Dashboard_panel_Container_16;
extern leWidget* Dashboard_panel_Container_17;
extern leWidget* Dashboard_panel_Container__margin__1;
extern leWidget* Dashboard_panel_Container__margin__2;
extern leWidget* Dashboard_panel_Icon_4;
extern leWidget* Dashboard_panel_Text_6;
extern leImageWidget* Dashboard_image_Icon_4;
extern leLabelWidget* Dashboard_label_ACCURACY;
extern leWidget* Dashboard_panel_Container_18;
extern leWidget* Dashboard_panel_Text_7;
extern leWidget* Dashboard_panel_Text_8;
extern leLabelWidget* Dashboard_label__94_5_;
extern leLabelWidget* Dashboard_label__1247_1320;
extern leWidget* Dashboard_panel_Container_19;
extern leWidget* Dashboard_panel_Container_20;
extern leWidget* Dashboard_panel_Container_21;
extern leWidget* Dashboard_panel_Container_22;
extern leWidget* Dashboard_panel_Container__margin__4;
extern leWidget* Dashboard_panel_Icon_5;
extern leWidget* Dashboard_panel_Text_9;
extern leImageWidget* Dashboard_image_Icon_5;
extern leLabelWidget* Dashboard_label_COMBO;
extern leWidget* Dashboard_panel_Container_23;
extern leWidget* Dashboard_panel_Text_10;
extern leWidget* Dashboard_panel_Text_11;
extern leLabelWidget* Dashboard_label__142x;
extern leLabelWidget* Dashboard_label_Best__89;
extern leWidget* Dashboard_panel_Container_24;
extern leWidget* Dashboard_panel_Container_25;
extern leWidget* Dashboard_panel_Container_28;
extern leWidget* Dashboard_panel_Container_26;
extern leWidget* Dashboard_panel_Container_27;
extern leLabelWidget* Dashboard_label_HIT;
extern leLabelWidget* Dashboard_label__1247;
extern leWidget* Dashboard_panel_Container_29;
extern leWidget* Dashboard_panel_Container_30;
extern leLabelWidget* Dashboard_label_MISS;
extern leLabelWidget* Dashboard_label__73;
extern leWidget* Dashboard_PANEL_VIDEO_STREAM;
extern leWidget* Dashboard_PANEL_GUITAR;
extern leImageWidget* Dashboard_IMAGE_STAND_BY;
extern leWidget* Dashboard_PANEL_NO_SIGNAL;
extern leImageWidget* Dashboard_IMAGE_LED_NO_SIGNAL;
extern leLabelWidget* Dashboard_LABEL_NO_SIGNAL;
extern leWidget* Dashboard_PANEL_GUITAR_ROW_0;
extern leWidget* Dashboard_PANEL_GUITAR_ROW_1;
extern leWidget* Dashboard_PANEL_GUITAR_ROW_2;
extern leLabelWidget* Dashboard_LABEL_GUITAR_MANUAL_CONTROL;
extern leButtonWidget* Dashboard_BUTTON_GUITAR_ENABLE;
extern leButtonWidget* Dashboard_BUTTON_GUITAR_FRET_GREEN;
extern leButtonWidget* Dashboard_BUTTON_GUITAR_FRET_RED;
extern leButtonWidget* Dashboard_BUTTON_GUITAR_FRET_YELLOW;
extern leButtonWidget* Dashboard_BUTTON_GUITAR_FRET_BLUE;
extern leButtonWidget* Dashboard_BUTTON_GUITAR_FRET_ORANGE;
extern leButtonWidget* Dashboard_BUTTON_GUITAR_STRUM_UP;
extern leButtonWidget* Dashboard_BUTTON_GUITAR_MINUS;
extern leButtonWidget* Dashboard_BUTTON_GUITAR_PLUS;
extern leButtonWidget* Dashboard_BUTTON_GUITAR_STRUM_DOWN;
extern leWidget* Dashboard_panel_SystemStatus;
extern leWidget* Dashboard_panel_NoteVisualizer;
extern leWidget* Dashboard_panel_ConfigPanel;
extern leWidget* Dashboard_panel_Heading_3_2;
extern leWidget* Dashboard_panel_Container_39;
extern leLabelWidget* Dashboard_label_SYSTEM_STATUS;
extern leWidget* Dashboard_panel_Container_40;
extern leWidget* Dashboard_panel_Container_45;
extern leWidget* Dashboard_panel_Container_50;
extern leWidget* Dashboard_panel_Container__margin__9;
extern leWidget* Dashboard_panel_Container_41;
extern leWidget* Dashboard_panel_Container__margin__6;
extern leWidget* Dashboard_panel_Container_42;
extern leWidget* Dashboard_panel_Text_15;
extern leWidget* Dashboard_panel_Icon_8;
extern leWidget* Dashboard_panel_Text_14;
extern leImageWidget* Dashboard_image_Icon_8;
extern leLabelWidget* Dashboard_label_CPU;
extern leLabelWidget* Dashboard_label__29_9_;
extern leWidget* Dashboard_panel_Container_43;
extern leWidget* Dashboard_panel_Container_44;
extern leWidget* Dashboard_panel_Container_46;
extern leWidget* Dashboard_panel_Container__margin__7;
extern leWidget* Dashboard_panel_Container_47;
extern leWidget* Dashboard_panel_Text_17;
extern leWidget* Dashboard_panel_Icon_9;
extern leWidget* Dashboard_panel_Text_16;
extern leImageWidget* Dashboard_image_Icon_9;
extern leLabelWidget* Dashboard_label_MEMORY;
extern leLabelWidget* Dashboard_label__62_3_;
extern leWidget* Dashboard_panel_Container_48;
extern leWidget* Dashboard_panel_Container_49;
extern leWidget* Dashboard_panel_Container_51;
extern leWidget* Dashboard_panel_Container__margin__8;
extern leWidget* Dashboard_panel_Container_52;
extern leWidget* Dashboard_panel_Text_19;
extern leWidget* Dashboard_panel_Icon_10;
extern leWidget* Dashboard_panel_Text_18;
extern leImageWidget* Dashboard_image_Icon_10;
extern leLabelWidget* Dashboard_label_TEMP;
extern leLabelWidget* Dashboard_label__46_3_C;
extern leWidget* Dashboard_panel_Container_53;
extern leWidget* Dashboard_panel_Container_54;
extern leWidget* Dashboard_panel_Container_55;
extern leWidget* Dashboard_panel_Container_56;
extern leWidget* Dashboard_panel_Container_58;
extern leWidget* Dashboard_panel_Inline_content;
extern leWidget* Dashboard_panel_Container_57;
extern leLabelWidget* Dashboard_label_UPTIME;
extern leLabelWidget* Dashboard_label__3h_24m;
extern leWidget* Dashboard_panel_Inline_content_0;
extern leWidget* Dashboard_panel_Container_59;
extern leLabelWidget* Dashboard_label_LATENCY;
extern leLabelWidget* Dashboard_label__12ms;
extern leWidget* Dashboard_panel_Heading_3_3;
extern leWidget* Dashboard_panel_Container__margin__10;
extern leLabelWidget* Dashboard_label_NOTE_TRACKING;
extern leWidget* Dashboard_panel_Container_60;
extern leWidget* Dashboard_panel_Container_96;
extern leWidget* Dashboard_panel_Container_105;
extern leWidget* Dashboard_panel_Container_109;
extern leWidget* Dashboard_panel_Container_111;
extern leWidget* Dashboard_panel_Container_132;
extern leWidget* Dashboard_panel_Container_138;
extern leWidget* Dashboard_panel_Container_141;
extern leWidget* Dashboard_panel_Container_144;
extern leWidget* Dashboard_panel_Container_149;
extern leWidget* Dashboard_panel_Container_169;
extern leWidget* Dashboard_panel_Container_170;
extern leWidget* Dashboard_panel_Container_97;
extern leWidget* Dashboard_panel_Container_98;
extern leWidget* Dashboard_panel_Container_106;
extern leWidget* Dashboard_panel_Container_110;
extern leWidget* Dashboard_panel_Container_112;
extern leWidget* Dashboard_panel_Container_133;
extern leWidget* Dashboard_panel_Container_139;
extern leWidget* Dashboard_panel_Container_140;
extern leWidget* Dashboard_panel_Container_142;
extern leWidget* Dashboard_panel_Container_143;
extern leWidget* Dashboard_panel_Container_145;
extern leWidget* Dashboard_panel_Container_146;
extern leWidget* Dashboard_panel_Container_150;
extern leWidget* Dashboard_panel_Container_151;
extern leWidget* Dashboard_panel_Container_171;
extern leWidget* Dashboard_panel_Container_173;
extern leWidget* Dashboard_panel_Container_175;
extern leWidget* Dashboard_panel_Container_177;
extern leWidget* Dashboard_panel_Container_179;
extern leWidget* Dashboard_panel_Container_172;
extern leWidget* Dashboard_panel_Container_174;
extern leWidget* Dashboard_panel_Container_176;
extern leWidget* Dashboard_panel_Container_178;
extern leWidget* Dashboard_panel_Container_180;
extern leWidget* Dashboard_panel_Heading_3_4;
extern leWidget* Dashboard_panel_Container_181;
extern leLabelWidget* Dashboard_label_CONFIGURATION;
extern leWidget* Dashboard_panel_Container_182;
extern leWidget* Dashboard_panel_Container_186;
extern leWidget* Dashboard_panel_Container__margin__11;
extern leWidget* Dashboard_panel_Container_183;
extern leWidget* Dashboard_panel_Text_21;
extern leWidget* Dashboard_panel_Label;
extern leWidget* Dashboard_panel_Text_20;
extern leLabelWidget* Dashboard_label_RESPONSE_DELAY;
extern leLabelWidget* Dashboard_label__45ms;
extern leWidget* Dashboard_panel_Container_184;
extern leWidget* Dashboard_panel_Container_185;
extern leWidget* Dashboard_panel_Container_187;
extern leWidget* Dashboard_panel_Text_23;
extern leWidget* Dashboard_panel_Label_0;
extern leWidget* Dashboard_panel_Text_22;
extern leLabelWidget* Dashboard_label_SENSITIVITY;
extern leLabelWidget* Dashboard_label__75_;
extern leWidget* Dashboard_panel_Container_188;
extern leWidget* Dashboard_panel_Container_189;
extern leWidget* Dashboard_panel_Container_190;
extern leWidget* Dashboard_panel_Container_191;
extern leWidget* Dashboard_panel_Container_192;
extern leWidget* Dashboard_panel_Container_193;
extern leWidget* Dashboard_panel_Label_1;
extern leWidget* Dashboard_panel_Switch;
extern leLabelWidget* Dashboard_label_AUTO_CALIBRATE;
extern leWidget* Dashboard_panel_Text_24;
extern leWidget* Dashboard_panel_Label_2;
extern leWidget* Dashboard_panel_Switch_0;
extern leLabelWidget* Dashboard_label_DEBUG_MODE;
extern leWidget* Dashboard_panel_Text_25;
extern leWidget* Dashboard_panel_Label_3;
extern leWidget* Dashboard_panel_Switch_1;
extern leLabelWidget* Dashboard_label_AUTO_RECOVERY;
extern leWidget* Dashboard_panel_Text_26;

// event handlers
// !!THESE MUST BE IMPLEMENTED IN THE APPLICATION CODE!!
void event_Dashboard_BUTTON_SYSYEM_NAVIGATION_OnPressed(leButtonWidget* btn);
void event_Dashboard_BUTTON_GUITAR_STRUM_UP_OnPressed(leButtonWidget* btn);
void event_Dashboard_BUTTON_GUITAR_STRUM_UP_OnReleased(leButtonWidget* btn);
void event_Dashboard_BUTTON_GUITAR_STRUM_DOWN_OnPressed(leButtonWidget* btn);
void event_Dashboard_BUTTON_GUITAR_STRUM_DOWN_OnReleased(leButtonWidget* btn);

// screen lifecycle functions
leResult screenInit_Dashboard(void); // call to initialize this screen
leResult screenShow_Dashboard(void); // call to show this screen
void screenHide_Dashboard(void); // call to hide this screen
void screenDestroy_Dashboard(void); // call to destroy this screen
void screenUpdate_Dashboard(void); // call to update this screen

leWidget* screenGetRoot_Dashboard(uint32_t lyrIdx); // gets a root widget for this screen

// Screen Events:
void Dashboard_OnShow(void); // called when this screen is shown

//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

#endif // LE_GEN_SCREEN_DASHBOARD_H
