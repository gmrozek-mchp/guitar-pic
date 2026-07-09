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
extern leWidget* Marvin_PANEL_DASHBOARD_TITLEBAR;
extern leWidget* Marvin_PANEL_DASHBOARD_BOTTOM;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_TITLEBAR_NAVIGATION;
extern leImageWidget* Marvin_IMAGE_DASHBOARD_TITLEBAR_GUITAR;
extern leImageWidget* Marvin_IMAGE_DASHBOARD_TITLEBAR_PIC;
extern leImageWidget* Marvin_IMAGE_DASHBOARD_TITLEBAR_MICROCHIP;
extern leWidget* Marvin_PANEL_DASHBOARD_ROBOT;
extern leWidget* Marvin_PANEL_DASHBOARD_GAMEPLAY;
extern leWidget* Marvin_PANEL_DASHBOARD_HUMAN;
extern leImageWidget* Marvin_IMAGE_DASHBOARD_ROBOT_PLAYER;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_Name;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer;
extern leWidget* Marvin_PANEL_DASHBOARD_ROBOT_STATE;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_SCORE;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_1X;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_2X;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_3X;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_4X;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_Score;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_STREAK;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_Streak;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_Accuracy;
extern leProgressBarWidget* Marvin_PROGRESSBAR_DASHBOARD_ROBOT_Accuracy;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_StarPower;
extern leProgressBarWidget* Marvin_PROGRESSBAR_DASHBOARD_ROBOT_StarPower;
extern leWidget* Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_1;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR;
extern leWidget* Marvin_panel_Container_36_0;
extern leWidget* Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_2;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV;
extern leWidget* Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_3;
extern leWidget* Marvin_PANEL_DASHBOARD_ROBOT_BORDER;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_ACTUATOR_GUITAR_1;
extern leWidget* Marvin_PANEL_DASHBOARD_ROBOT_STATE_LED;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_RobotState;
extern leLabelWidget* Marvin_label_IDLE_0_0;
extern leWidget* Marvin_PANEL_DASHBOARD_VIDEO;
extern leWidget* Marvin_PANEL_DASHBOARD_SONG;
extern leWidget* Marvin_PANEL_DASHBOARD_TEST_BAR_WHITE;
extern leWidget* Marvin_PANEL_DASHBOARD_TEST_BAR_YELLOW;
extern leWidget* Marvin_PANEL_DASHBOARD_TEST_BAR_CYAN;
extern leWidget* Marvin_PANEL_DASHBOARD_TEST_BAR_GREEN;
extern leWidget* Marvin_PANEL_DASHBOARD_TEST_BAR_MAGENTA;
extern leWidget* Marvin_PANEL_DASHBOARD_TEST_BAR_RED;
extern leWidget* Marvin_PANEL_DASHBOARD_TEST_BAR_BLUE;
extern leGradientWidget* Marvin_GRADIENT_DASHBOARD_TEST_BAR_GRAY;
extern leWidget* Marvin_PANEL_DASHBOARD_NO_SIGNAL;
extern leWidget* Marvin_PANEL_DASHBOARD_TEST_PATTERN_BORDER;
extern leWidget* Marvin_PANEL_DASHBOARD_NO_SIGNAL_LED;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_NO_SIGNAL;
extern leWidget* Marvin_PANEL_DASHBOARD_SONG_INFO;
extern leWidget* Marvin_PANEL_DASHBOARD_SONG_DIVIDER;
extern leWidget* Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY;
extern leImageWidget* Marvin_PANEL_DASHBOARD_SONG_AlbumArt;
extern leWidget* Marvin_PANEL_DASHBOARD_SONG_ALBUM_ART_BORDER;
extern leWidget* Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_Status;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_SongTitle;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_SongArtist;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_SongAlbum;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_GENRE;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_SongGenre;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_DURATION;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_SongDuration;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_TIER;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_SongTier;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_START_TIME;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_STOP_TIME;
extern leProgressBarWidget* Marvin_PROGRESSBAR_DASHBOARD_SONG_PLAYTIME;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY;
extern leWidget* Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_Difficulty;
extern leWidget* Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_DIVIDER;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_GAMEPLAY_START;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty;
extern leImageWidget* Marvin_IMAGE_DASHBOARD_HUMAN_PLAYER;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_Name;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer;
extern leWidget* Marvin_PANEL_DASHBOARD_HUMAN_STATE;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_SCORE;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_HUMAN_1X;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_HUMAN_2X;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_HUMAN_3X;
extern leButtonWidget* Marvin_BUTTON_DASHBOARD_HUMAN_4X;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_Score;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_STREAK;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_Streak;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_Accuracy;
extern leProgressBarWidget* Marvin_PROGRESSBAR_DASHBOARD_HUMAN_Accuracy;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_StarPower;
extern leProgressBarWidget* Marvin_PROGRESSBAR_DASHBOARD_HUMAN_StarPower;
extern leWidget* Marvin_PANEL_DASHBOARD_HUMAN_DIVIDER_1;
extern leWidget* Marvin_panel_Container_98_0;
extern leWidget* Marvin_PANEL_DASHBOARD_HUMAN_BORDER;
extern leWidget* Marvin_PANEL_DASHBOARD_HUMAN_STATE_LED;
extern leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_HumanState;
extern leWidget* Marvin_panel_Paragraph_13_0;
extern leWidget* Marvin_panel_Container_99_0;
extern leLabelWidget* Marvin_label_CONTROLLER_0;
extern leWidget* Marvin_panel_Container_100_0;
extern leWidget* Marvin_panel_Container_101_0;
extern leWidget* Marvin_panel_Container_102_0;
extern leWidget* Marvin_panel_Text_32_0;
extern leWidget* Marvin_panel_Text_33_0;
extern leLabelWidget* Marvin_label_Wii_guitar_0;
extern leLabelWidget* Marvin_label_Connected_1;
extern leWidget* Marvin_panel_Text_34_0;
extern leWidget* Marvin_panel_Text_35_0;
extern leLabelWidget* Marvin_label_Wii_remote_0;
extern leLabelWidget* Marvin_label_Connected_0_0;
extern leWidget* Marvin_panel_Text_36_0;
extern leWidget* Marvin_panel_Text_37_0;
extern leLabelWidget* Marvin_label_Battery_0;
extern leLabelWidget* Marvin_label__68__0;
extern leWidget* Marvin_PANEL_NAVIGATION;
extern leWidget* Marvin_PANEL_NAVIGATION_TOP;
extern leWidget* Marvin_PANEL_NAVIGATION_MIDDLE;
extern leWidget* Marvin_PANEL_NAVIGATION_BOTTOM;
extern leLabelWidget* Marvin_LABEL_NAVIGATION;
extern leLabelWidget* Marvin_LABEL_NAV_SUB_HEADING;
extern leButtonWidget* Marvin_BUTTON_NAV_DASHBOARD;
extern leButtonWidget* Marvin_BUTTON_NAV_WIIMOTES;
extern leButtonWidget* Marvin_BUTTON_NAV_LOGS;
extern leButtonWidget* Marvin_BUTTON_NAV_PERFORMANCE;
extern leButtonWidget* Marvin_BUTTON_NAV_SYSTEM_INFO;
extern leButtonWidget* Marvin_BUTTON_NAV_DIAGNOSTICS;
extern leButtonWidget* Marvin_BUTTON_NAV_SETTINGS;
extern leWidget* Marvin_panel_Container_196;
extern leWidget* Marvin_panel_Container_197;
extern leWidget* Marvin_panel_Container_198;
extern leWidget* Marvin_panel_Container_199;
extern leWidget* Marvin_panel_Container_200;
extern leLabelWidget* Marvin_label_STATUS;
extern leLabelWidget* Marvin_label_Connected;
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
extern leWidget* Marvin_PANEL_WIIMOTES_TITLEBAR;
extern leWidget* Marvin_PANEL_WIIMOTES_BOTTOM;
extern leButtonWidget* Marvin_BUTTON_WIIMOTES_TITLEBAR_NAVIGATION;
extern leImageWidget* Marvin_IMAGE_WIIMOTES_TITLEBAR_GUITAR;
extern leImageWidget* Marvin_IMAGE_WIIMOTES_TITLEBAR_PIC;
extern leImageWidget* Marvin_IMAGE_WIIMOTES_TITLEBAR_MICROCHIP;
extern leWidget* Marvin_PANEL_WIIMOTES_ROBOT;
extern leWidget* Marvin_PANEL_WIIMOTES_HUMAN;
extern leLabelWidget* Marvin_LABEL_WIIMOTES_ROBOT_RobotLemmy;
extern leWidget* Marvin_panel_Button_37;
extern leWidget* Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION;
extern leWidget* Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE;
extern leWidget* Marvin_panel_Text_1_1;
extern leLabelWidget* Marvin_label_ENABLED_0;
extern leLabelWidget* Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION;
extern leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_FRET_GREEN;
extern leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_FRET_RED;
extern leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_FRET_YELLOW;
extern leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_FRET_BLUE;
extern leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_FRET_ORANGE;
extern leWidget* Marvin_panel_Button_5_0;
extern leWidget* Marvin_panel_Button_6_0;
extern leWidget* Marvin_panel_Container_15_1;
extern leWidget* Marvin_panel_Button_7_0;
extern leWidget* Marvin_panel_Button_8_0;
extern leWidget* Marvin_panel_Icon_0_0;
extern leLabelWidget* Marvin_label__UP_1;
extern leImageWidget* Marvin_image_Icon_0_0;
extern leWidget* Marvin_panel_Icon_1_0;
extern leLabelWidget* Marvin_label__DN_1;
extern leImageWidget* Marvin_image_Icon_1_0;
extern leLabelWidget* Marvin_label_WHAMMY_1;
extern leWidget* Marvin_panel_WhammySlider_1;
extern leWidget* Marvin_panel_Container_16_1;
extern leWidget* Marvin_panel_Container_17_1;
extern leWidget* Marvin_panel_Container_18_1;
extern leWidget* Marvin_panel_Container_19_1;
extern leLabelWidget* Marvin_label___11;
extern leLabelWidget* Marvin_label___0_1;
extern leLabelWidget* Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE;
extern leWidget* Marvin_panel_Button_9_0;
extern leWidget* Marvin_panel_Button_10_0;
extern leWidget* Marvin_panel_Button_11_0;
extern leWidget* Marvin_panel_Button_12_0;
extern leWidget* Marvin_panel_Button_13_0;
extern leWidget* Marvin_panel_Button_14_0;
extern leWidget* Marvin_panel_Button_15_0;
extern leWidget* Marvin_panel_Button_16_0;
extern leWidget* Marvin_panel_Button_17_0;
extern leWidget* Marvin_panel_TiltControl_1;
extern leLabelWidget* Marvin_label___1_1;
extern leLabelWidget* Marvin_label___2_1;
extern leLabelWidget* Marvin_label___3_0;
extern leLabelWidget* Marvin_label___4_0;
extern leLabelWidget* Marvin_label_HOME_1;
extern leLabelWidget* Marvin_label_A_1;
extern leLabelWidget* Marvin_label_B_1;
extern leLabelWidget* Marvin_label__1_1;
extern leLabelWidget* Marvin_label__2_1;
extern leImageWidget* Marvin_image_TiltControl_1;
extern leLabelWidget* Marvin_label_TILT_1;

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
