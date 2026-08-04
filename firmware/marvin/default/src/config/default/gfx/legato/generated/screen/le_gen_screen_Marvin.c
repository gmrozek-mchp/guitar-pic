#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"

// screen member widget declarations
static leWidget* root0;
static leWidget* root1;
static leWidget* root2;
static leWidget* root3;
static leWidget* root4;
static leWidget* root5;
static leWidget* root6;

leWidget* Marvin_PANEL_DASHBOARD;
leWidget* Marvin_PANEL_DASHBOARD_BOTTOM;
leWidget* Marvin_PANEL_DASHBOARD_ROBOT;
leWidget* Marvin_PANEL_DASHBOARD_GAMEPLAY;
leWidget* Marvin_PANEL_DASHBOARD_HUMAN;
leImageWidget* Marvin_IMAGE_DASHBOARD_ROBOT_PLAYER;
leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_Name;
leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer;
leWidget* Marvin_PANEL_DASHBOARD_ROBOT_STATE;
leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_SCORE;
leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_1X;
leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_2X;
leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_3X;
leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_4X;
leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_Score;
leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_STREAK;
leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_Streak;
leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY;
leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_Accuracy;
leProgressBarWidget* Marvin_PROGRESSBAR_DASHBOARD_ROBOT_Accuracy;
leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER;
leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_StarPower;
leProgressBarWidget* Marvin_PROGRESSBAR_DASHBOARD_ROBOT_StarPower;
leWidget* Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_1;
leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY;
leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN;
leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED;
leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW;
leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE;
leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE;
leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR;
leWidget* Marvin_panel_Container_36_0;
leWidget* Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_2;
leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR;
leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN;
leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV;
leWidget* Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_3;
leWidget* Marvin_PANEL_DASHBOARD_ROBOT_BORDER;
leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS;
leButtonWidget* Marvin_BUTTON_DASHBOARD_ROBOT_ACTUATOR_GUITAR_1;
leWidget* Marvin_PANEL_DASHBOARD_ROBOT_STATE_LED;
leLabelWidget* Marvin_LABEL_DASHBOARD_ROBOT_RobotState;
leLabelWidget* Marvin_label_IDLE_0_0;
leWidget* Marvin_PANEL_DASHBOARD_VIDEO;
leWidget* Marvin_PANEL_DASHBOARD_SONG;
leWidget* Marvin_PANEL_DASHBOARD_TEST_BAR_WHITE;
leWidget* Marvin_PANEL_DASHBOARD_TEST_BAR_YELLOW;
leWidget* Marvin_PANEL_DASHBOARD_TEST_BAR_CYAN;
leWidget* Marvin_PANEL_DASHBOARD_TEST_BAR_GREEN;
leWidget* Marvin_PANEL_DASHBOARD_TEST_BAR_MAGENTA;
leWidget* Marvin_PANEL_DASHBOARD_TEST_BAR_RED;
leWidget* Marvin_PANEL_DASHBOARD_TEST_BAR_BLUE;
leGradientWidget* Marvin_GRADIENT_DASHBOARD_TEST_BAR_GRAY;
leWidget* Marvin_PANEL_DASHBOARD_NO_SIGNAL;
leWidget* Marvin_PANEL_DASHBOARD_TEST_PATTERN_BORDER;
leWidget* Marvin_PANEL_DASHBOARD_NO_SIGNAL_LED;
leLabelWidget* Marvin_LABEL_DASHBOARD_NO_SIGNAL;
leWidget* Marvin_PANEL_DASHBOARD_SONG_INFO;
leWidget* Marvin_PANEL_DASHBOARD_SONG_DIVIDER;
leWidget* Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY;
leImageWidget* Marvin_PANEL_DASHBOARD_SONG_AlbumArt;
leWidget* Marvin_PANEL_DASHBOARD_SONG_ALBUM_ART_BORDER;
leWidget* Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT;
leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_Status;
leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_SongTitle;
leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_SongArtist;
leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_SongAlbum;
leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_GENRE;
leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_SongGenre;
leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_DURATION;
leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_SongDuration;
leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_TIER;
leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_SongTier;
leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_START_TIME;
leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_STOP_TIME;
leProgressBarWidget* Marvin_PROGRESSBAR_DASHBOARD_SONG_PLAYTIME;
leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE;
leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode;
leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY;
leWidget* Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_Difficulty;
leWidget* Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_DIVIDER;
leButtonWidget* Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG;
leButtonWidget* Marvin_BUTTON_DASHBOARD_GAMEPLAY_START;
leLabelWidget* Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty;
leImageWidget* Marvin_IMAGE_DASHBOARD_HUMAN_PLAYER;
leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_Name;
leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer;
leWidget* Marvin_PANEL_DASHBOARD_HUMAN_STATE;
leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_SCORE;
leButtonWidget* Marvin_BUTTON_DASHBOARD_HUMAN_1X;
leButtonWidget* Marvin_BUTTON_DASHBOARD_HUMAN_2X;
leButtonWidget* Marvin_BUTTON_DASHBOARD_HUMAN_3X;
leButtonWidget* Marvin_BUTTON_DASHBOARD_HUMAN_4X;
leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_Score;
leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_STREAK;
leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_Streak;
leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY;
leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_Accuracy;
leProgressBarWidget* Marvin_PROGRESSBAR_DASHBOARD_HUMAN_Accuracy;
leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER;
leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_StarPower;
leProgressBarWidget* Marvin_PROGRESSBAR_DASHBOARD_HUMAN_StarPower;
leWidget* Marvin_PANEL_DASHBOARD_HUMAN_DIVIDER_1;
leWidget* Marvin_panel_Container_98_0;
leWidget* Marvin_PANEL_DASHBOARD_HUMAN_BORDER;
leWidget* Marvin_PANEL_DASHBOARD_HUMAN_STATE_LED;
leLabelWidget* Marvin_LABEL_DASHBOARD_HUMAN_HumanState;
leWidget* Marvin_panel_Paragraph_13_0;
leWidget* Marvin_panel_Container_99_0;
leLabelWidget* Marvin_label_CONTROLLER_0;
leWidget* Marvin_panel_Container_100_0;
leWidget* Marvin_panel_Container_101_0;
leWidget* Marvin_panel_Container_102_0;
leWidget* Marvin_panel_Text_32_0;
leWidget* Marvin_panel_Text_33_0;
leLabelWidget* Marvin_label_Wii_guitar_0;
leLabelWidget* Marvin_label_Connected_1;
leWidget* Marvin_panel_Text_34_0;
leWidget* Marvin_panel_Text_35_0;
leLabelWidget* Marvin_label_Wii_remote_0;
leLabelWidget* Marvin_label_Connected_0_0;
leWidget* Marvin_panel_Text_36_0;
leWidget* Marvin_panel_Text_37_0;
leLabelWidget* Marvin_label_Battery_0;
leLabelWidget* Marvin_label__68__0;
leWidget* Marvin_PANEL_NAVIGATION;
leWidget* Marvin_PANEL_NAVIGATION_TOP;
leWidget* Marvin_PANEL_NAVIGATION_MIDDLE;
leWidget* Marvin_PANEL_NAVIGATION_BOTTOM;
leLabelWidget* Marvin_LABEL_NAVIGATION;
leLabelWidget* Marvin_LABEL_NAV_SUB_HEADING;
leButtonWidget* Marvin_BUTTON_NAV_DASHBOARD;
leButtonWidget* Marvin_BUTTON_NAV_WIIMOTES;
leButtonWidget* Marvin_BUTTON_NAV_LOGS;
leButtonWidget* Marvin_BUTTON_NAV_PERFORMANCE;
leButtonWidget* Marvin_BUTTON_NAV_SYSTEM_INFO;
leButtonWidget* Marvin_BUTTON_NAV_DIAGNOSTICS;
leButtonWidget* Marvin_BUTTON_NAV_SETTINGS;
leWidget* Marvin_panel_Container_196;
leWidget* Marvin_panel_Container_197;
leWidget* Marvin_panel_Container_198;
leWidget* Marvin_panel_Container_199;
leWidget* Marvin_panel_Container_200;
leLabelWidget* Marvin_label_STATUS;
leLabelWidget* Marvin_label_Connected;
leWidget* Marvin_PANEL_SONG_SELECT;
leWidget* Marvin_PANEL_SONG_SELECT_TOP;
leWidget* Marvin_PANEL_SONG_SELECT_BOTTOM;
leLabelWidget* Marvin_LABEL_SELECT_SONG;
leButtonWidget* Marvin_BUTTON_SONG_SELECT_CLOSE;
leWidget* Marvin_PANEL_SONG_SELECT_LEFT;
leWidget* Marvin_PANEL_SONG_SELECT_CENTER;
leWidget* Marvin_PANEL_SONG_SELECT_RIGHT;
leWidget* Marvin_PANEL_SETLIST;
leLabelWidget* Marvin_LABEL_SETLIST;
leWidget* Marvin_PANEL_SONG_SELECT_SONG_INFO;
leLabelWidget* Marvin_LABEL_SONG_SELECT_ALBUM;
leLabelWidget* Marvin_LABEL_SONG_SELECT_SongAlbum;
leLabelWidget* Marvin_LABEL_SONG_SELECT_YEAR;
leLabelWidget* Marvin_LABEL_SONG_SELECT_SongYear;
leLabelWidget* Marvin_LABEL_SONG_SELECT_GENRE;
leLabelWidget* Marvin_LABEL_SONG_SELECT_SongGenre;
leLabelWidget* Marvin_LABEL_SONG_SELECT_DURATION;
leLabelWidget* Marvin_LABEL_SONG_SELECT_SongDuration;
leWidget* Marvin_PANEL_SONG_SELECT_DIFFICULTY;
leWidget* Marvin_PANEL_SONG_SELECT_MODE;
leButtonWidget* Marvin_BUTTON_SONG_SELECT_SELECT;
leLabelWidget* Marvin_LABEL_SONG_SELECT_DIFFICULTY;
leButtonWidget* Marvin_BUTTON_SONG_SELECT_EASY;
leButtonWidget* Marvin_BUTTON_SONG_SELECT_MEDIUM;
leButtonWidget* Marvin_BUTTON_SONG_SELECT_HARD;
leButtonWidget* Marvin_BUTTON_SONG_SELECT_EXPERT;
leLabelWidget* Marvin_LABEL_SONG_SELECT_MODE;
leButtonWidget* Marvin_BUTTON_SONG_SELECT_1P_ROBOT;
leButtonWidget* Marvin_BUTTON_SONG_SELECT_1P_HUMAN;
leButtonWidget* Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN;
leWidget* Marvin_PANEL_SONG_SELECT_ALBUM_ART;
leImageWidget* Marvin_IMAGE_ALBUM_ART;
leWidget* Marvin_PANEL_ALBUM_ART_OVERLAY;
leLabelWidget* Marvin_LABEL_SONG_SELECT_SongTier;
leLabelWidget* Marvin_LABEL_SONG_SELECT_SongTitle;
leLabelWidget* Marvin_LABEL_SONG_SELECT_Artist;
leWidget* Marvin_PANEL_WIIMOTES;
leWidget* Marvin_PANEL_WIIMOTES_BOTTOM;
leWidget* Marvin_PANEL_WIIMOTES_ROBOT;
leWidget* Marvin_PANEL_WIIMOTES_HUMAN;
leLabelWidget* Marvin_LABEL_WIIMOTES_ROBOT_RobotLemmy;
leWidget* Marvin_panel_Button_37;
leWidget* Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION;
leWidget* Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE;
leWidget* Marvin_panel_Text_1_1;
leLabelWidget* Marvin_label_ENABLED_0;
leLabelWidget* Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_FRET_GREEN;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_FRET_RED;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_FRET_YELLOW;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_FRET_BLUE;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_FRET_ORANGE;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN;
leWidget* Marvin_panel_Container_15_1;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_PLUS;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_MINUS;
leLabelWidget* Marvin_label_WHAMMY_1;
leWidget* Marvin_panel_WhammySlider_1;
leWidget* Marvin_panel_Container_16_1;
leWidget* Marvin_panel_Container_17_1;
leWidget* Marvin_panel_Container_18_1;
leWidget* Marvin_panel_Container_19_1;
leLabelWidget* Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_UP;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_LEFT;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_RIGHT;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_DOWN;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_HOME;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_A;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_B;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_ONE;
leButtonWidget* Marvin_BUTTON_WIIMOTES_ROBOT_TWO;
leWidget* Marvin_panel_TiltControl_1;
leImageWidget* Marvin_image_TiltControl_1;
leLabelWidget* Marvin_label_TILT_1;
leWidget* Marvin_PANEL_KEYBOARD;
leWidget* Marvin_PANEL_BUS;

static leBool initialized = LE_FALSE;
static leBool showing = LE_FALSE;

leResult screenInit_Marvin(void)
{
    if(initialized == LE_TRUE)
        return LE_FAILURE;

    initialized = LE_TRUE;

    return LE_SUCCESS;
}

leResult screenShow_Marvin(void)
{
    if(showing == LE_TRUE)
        return LE_FAILURE;

    // layer 0
    root0 = leWidget_New();
    root0->fn->setSize(root0, LE_DEFAULT_SCREEN_WIDTH, LE_DEFAULT_SCREEN_HEIGHT);
    root0->fn->setBackgroundType(root0, LE_WIDGET_BACKGROUND_NONE);
    root0->fn->setMargins(root0, 0, 0, 0, 0);
    root0->flags |= LE_WIDGET_IGNOREEVENTS;
    root0->flags |= LE_WIDGET_IGNOREPICK;

    Marvin_PANEL_DASHBOARD = leWidget_New();
    Marvin_PANEL_DASHBOARD->fn->setPosition(Marvin_PANEL_DASHBOARD, 0, 0);
    Marvin_PANEL_DASHBOARD->fn->setSize(Marvin_PANEL_DASHBOARD, 1280, 800);
    Marvin_PANEL_DASHBOARD->fn->setScheme(Marvin_PANEL_DASHBOARD, &SCHEME_BACKGROUND);
    root0->fn->addChild(root0, (leWidget*)Marvin_PANEL_DASHBOARD);

    Marvin_PANEL_DASHBOARD_BOTTOM = leWidget_New();
    Marvin_PANEL_DASHBOARD_BOTTOM->fn->setPosition(Marvin_PANEL_DASHBOARD_BOTTOM, 12, 65);
    Marvin_PANEL_DASHBOARD_BOTTOM->fn->setSize(Marvin_PANEL_DASHBOARD_BOTTOM, 1256, 728);
    Marvin_PANEL_DASHBOARD_BOTTOM->fn->setScheme(Marvin_PANEL_DASHBOARD_BOTTOM, &SCHEME_BACKGROUND);
    Marvin_PANEL_DASHBOARD_BOTTOM->fn->setBackgroundType(Marvin_PANEL_DASHBOARD_BOTTOM, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_DASHBOARD->fn->addChild(Marvin_PANEL_DASHBOARD, (leWidget*)Marvin_PANEL_DASHBOARD_BOTTOM);

    Marvin_PANEL_DASHBOARD_ROBOT = leWidget_New();
    Marvin_PANEL_DASHBOARD_ROBOT->fn->setPosition(Marvin_PANEL_DASHBOARD_ROBOT, 0, 12);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->setSize(Marvin_PANEL_DASHBOARD_ROBOT, 256, 716);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->setScheme(Marvin_PANEL_DASHBOARD_ROBOT, &SCHEME_FILL_ZINC_900);
    Marvin_PANEL_DASHBOARD_BOTTOM->fn->addChild(Marvin_PANEL_DASHBOARD_BOTTOM, (leWidget*)Marvin_PANEL_DASHBOARD_ROBOT);

    Marvin_IMAGE_DASHBOARD_ROBOT_PLAYER = leImageWidget_New();
    Marvin_IMAGE_DASHBOARD_ROBOT_PLAYER->fn->setPosition(Marvin_IMAGE_DASHBOARD_ROBOT_PLAYER, 1, 1);
    Marvin_IMAGE_DASHBOARD_ROBOT_PLAYER->fn->setSize(Marvin_IMAGE_DASHBOARD_ROBOT_PLAYER, 254, 208);
    Marvin_IMAGE_DASHBOARD_ROBOT_PLAYER->fn->setScheme(Marvin_IMAGE_DASHBOARD_ROBOT_PLAYER, &SCHEME_BACKGROUND);
    Marvin_IMAGE_DASHBOARD_ROBOT_PLAYER->fn->setBorderType(Marvin_IMAGE_DASHBOARD_ROBOT_PLAYER, LE_WIDGET_BORDER_NONE);
    Marvin_IMAGE_DASHBOARD_ROBOT_PLAYER->fn->setImage(Marvin_IMAGE_DASHBOARD_ROBOT_PLAYER, (leImage*)&LemmyOnStagePlayerImage_gradient);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_IMAGE_DASHBOARD_ROBOT_PLAYER);

    Marvin_LABEL_DASHBOARD_ROBOT_Name = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_ROBOT_Name->fn->setPosition(Marvin_LABEL_DASHBOARD_ROBOT_Name, 13, 168);
    Marvin_LABEL_DASHBOARD_ROBOT_Name->fn->setSize(Marvin_LABEL_DASHBOARD_ROBOT_Name, 160, 18);
    Marvin_LABEL_DASHBOARD_ROBOT_Name->fn->setScheme(Marvin_LABEL_DASHBOARD_ROBOT_Name, &STYLE_TEXT_ROBOT);
    Marvin_LABEL_DASHBOARD_ROBOT_Name->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_ROBOT_Name, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_ROBOT_Name->fn->setVAlignment(Marvin_LABEL_DASHBOARD_ROBOT_Name, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_ROBOT_Name->fn->setMargins(Marvin_LABEL_DASHBOARD_ROBOT_Name, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_ROBOT_Name->fn->setString(Marvin_LABEL_DASHBOARD_ROBOT_Name, (leString*)&string_PLAYER_ROBOT_Name);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_LABEL_DASHBOARD_ROBOT_Name);

    Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer->fn->setPosition(Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer, 13, 187);
    Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer->fn->setSize(Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer, 160, 16);
    Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer->fn->setScheme(Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer, &SCHEME_TEXT_ZINC_400);
    Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer->fn->setVAlignment(Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer->fn->setMargins(Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer->fn->setString(Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer, (leString*)&string_PLAYER_ROBOT_RobotPlayer);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer);

    Marvin_PANEL_DASHBOARD_ROBOT_STATE = leWidget_New();
    Marvin_PANEL_DASHBOARD_ROBOT_STATE->fn->setPosition(Marvin_PANEL_DASHBOARD_ROBOT_STATE, 186, 182);
    Marvin_PANEL_DASHBOARD_ROBOT_STATE->fn->setSize(Marvin_PANEL_DASHBOARD_ROBOT_STATE, 57, 20);
    Marvin_PANEL_DASHBOARD_ROBOT_STATE->fn->setScheme(Marvin_PANEL_DASHBOARD_ROBOT_STATE, &SCHEME_FILL_ZINC_800);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_PANEL_DASHBOARD_ROBOT_STATE);

    Marvin_PANEL_DASHBOARD_ROBOT_STATE_LED = leWidget_New();
    Marvin_PANEL_DASHBOARD_ROBOT_STATE_LED->fn->setPosition(Marvin_PANEL_DASHBOARD_ROBOT_STATE_LED, 8, 7);
    Marvin_PANEL_DASHBOARD_ROBOT_STATE_LED->fn->setSize(Marvin_PANEL_DASHBOARD_ROBOT_STATE_LED, 6, 6);
    Marvin_PANEL_DASHBOARD_ROBOT_STATE_LED->fn->setScheme(Marvin_PANEL_DASHBOARD_ROBOT_STATE_LED, &SCHEME_FILL_ZINC_600);
    Marvin_PANEL_DASHBOARD_ROBOT_STATE->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT_STATE, (leWidget*)Marvin_PANEL_DASHBOARD_ROBOT_STATE_LED);

    Marvin_LABEL_DASHBOARD_ROBOT_RobotState = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_ROBOT_RobotState->fn->setPosition(Marvin_LABEL_DASHBOARD_ROBOT_RobotState, 20, 2);
    Marvin_LABEL_DASHBOARD_ROBOT_RobotState->fn->setSize(Marvin_LABEL_DASHBOARD_ROBOT_RobotState, 29, 16);
    Marvin_LABEL_DASHBOARD_ROBOT_RobotState->fn->setScheme(Marvin_LABEL_DASHBOARD_ROBOT_RobotState, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_DASHBOARD_ROBOT_RobotState->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_ROBOT_RobotState, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_ROBOT_RobotState->fn->setVAlignment(Marvin_LABEL_DASHBOARD_ROBOT_RobotState, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_ROBOT_RobotState->fn->setMargins(Marvin_LABEL_DASHBOARD_ROBOT_RobotState, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_ROBOT_RobotState->fn->setString(Marvin_LABEL_DASHBOARD_ROBOT_RobotState, (leString*)&string_PLAYER_ROBOT_Status);
    Marvin_PANEL_DASHBOARD_ROBOT_STATE->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT_STATE, (leWidget*)Marvin_LABEL_DASHBOARD_ROBOT_RobotState);

    Marvin_LABEL_DASHBOARD_ROBOT_SCORE = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_ROBOT_SCORE->fn->setPosition(Marvin_LABEL_DASHBOARD_ROBOT_SCORE, 13, 221);
    Marvin_LABEL_DASHBOARD_ROBOT_SCORE->fn->setSize(Marvin_LABEL_DASHBOARD_ROBOT_SCORE, 120, 16);
    Marvin_LABEL_DASHBOARD_ROBOT_SCORE->fn->setScheme(Marvin_LABEL_DASHBOARD_ROBOT_SCORE, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_DASHBOARD_ROBOT_SCORE->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_ROBOT_SCORE, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_ROBOT_SCORE->fn->setVAlignment(Marvin_LABEL_DASHBOARD_ROBOT_SCORE, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_ROBOT_SCORE->fn->setMargins(Marvin_LABEL_DASHBOARD_ROBOT_SCORE, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_ROBOT_SCORE->fn->setString(Marvin_LABEL_DASHBOARD_ROBOT_SCORE, (leString*)&string_PLAYER_SCORE);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_LABEL_DASHBOARD_ROBOT_SCORE);

    Marvin_BUTTON_DASHBOARD_ROBOT_1X = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_ROBOT_1X->fn->setPosition(Marvin_BUTTON_DASHBOARD_ROBOT_1X, 142, 221);
    Marvin_BUTTON_DASHBOARD_ROBOT_1X->fn->setSize(Marvin_BUTTON_DASHBOARD_ROBOT_1X, 23, 16);
    Marvin_BUTTON_DASHBOARD_ROBOT_1X->fn->setScheme(Marvin_BUTTON_DASHBOARD_ROBOT_1X, &SCHEME_PILL_ZINC_800);
    Marvin_BUTTON_DASHBOARD_ROBOT_1X->fn->setBorderType(Marvin_BUTTON_DASHBOARD_ROBOT_1X, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_DASHBOARD_ROBOT_1X->fn->setVAlignment(Marvin_BUTTON_DASHBOARD_ROBOT_1X, LE_VALIGN_TOP);
    Marvin_BUTTON_DASHBOARD_ROBOT_1X->fn->setMargins(Marvin_BUTTON_DASHBOARD_ROBOT_1X, 4, 0, 4, 4);
    Marvin_BUTTON_DASHBOARD_ROBOT_1X->fn->setToggleable(Marvin_BUTTON_DASHBOARD_ROBOT_1X, LE_TRUE);
    Marvin_BUTTON_DASHBOARD_ROBOT_1X->fn->setPressed(Marvin_BUTTON_DASHBOARD_ROBOT_1X, LE_TRUE);
    Marvin_BUTTON_DASHBOARD_ROBOT_1X->fn->setString(Marvin_BUTTON_DASHBOARD_ROBOT_1X, (leString*)&string_PLAYER_MULTIPLIER_1x);
    Marvin_BUTTON_DASHBOARD_ROBOT_1X->fn->setPressedOffset(Marvin_BUTTON_DASHBOARD_ROBOT_1X, 0);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_BUTTON_DASHBOARD_ROBOT_1X);

    Marvin_BUTTON_DASHBOARD_ROBOT_2X = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_ROBOT_2X->fn->setPosition(Marvin_BUTTON_DASHBOARD_ROBOT_2X, 169, 221);
    Marvin_BUTTON_DASHBOARD_ROBOT_2X->fn->setSize(Marvin_BUTTON_DASHBOARD_ROBOT_2X, 23, 16);
    Marvin_BUTTON_DASHBOARD_ROBOT_2X->fn->setScheme(Marvin_BUTTON_DASHBOARD_ROBOT_2X, &SCHEME_PILL_ZINC_800);
    Marvin_BUTTON_DASHBOARD_ROBOT_2X->fn->setBorderType(Marvin_BUTTON_DASHBOARD_ROBOT_2X, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_DASHBOARD_ROBOT_2X->fn->setVAlignment(Marvin_BUTTON_DASHBOARD_ROBOT_2X, LE_VALIGN_TOP);
    Marvin_BUTTON_DASHBOARD_ROBOT_2X->fn->setMargins(Marvin_BUTTON_DASHBOARD_ROBOT_2X, 4, 0, 4, 4);
    Marvin_BUTTON_DASHBOARD_ROBOT_2X->fn->setToggleable(Marvin_BUTTON_DASHBOARD_ROBOT_2X, LE_TRUE);
    Marvin_BUTTON_DASHBOARD_ROBOT_2X->fn->setString(Marvin_BUTTON_DASHBOARD_ROBOT_2X, (leString*)&string_PLAYER_MULTIPLIER_2x);
    Marvin_BUTTON_DASHBOARD_ROBOT_2X->fn->setPressedOffset(Marvin_BUTTON_DASHBOARD_ROBOT_2X, 0);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_BUTTON_DASHBOARD_ROBOT_2X);

    Marvin_BUTTON_DASHBOARD_ROBOT_3X = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_ROBOT_3X->fn->setPosition(Marvin_BUTTON_DASHBOARD_ROBOT_3X, 196, 221);
    Marvin_BUTTON_DASHBOARD_ROBOT_3X->fn->setSize(Marvin_BUTTON_DASHBOARD_ROBOT_3X, 23, 16);
    Marvin_BUTTON_DASHBOARD_ROBOT_3X->fn->setScheme(Marvin_BUTTON_DASHBOARD_ROBOT_3X, &SCHEME_PILL_ZINC_800);
    Marvin_BUTTON_DASHBOARD_ROBOT_3X->fn->setBorderType(Marvin_BUTTON_DASHBOARD_ROBOT_3X, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_DASHBOARD_ROBOT_3X->fn->setVAlignment(Marvin_BUTTON_DASHBOARD_ROBOT_3X, LE_VALIGN_TOP);
    Marvin_BUTTON_DASHBOARD_ROBOT_3X->fn->setMargins(Marvin_BUTTON_DASHBOARD_ROBOT_3X, 4, 0, 4, 4);
    Marvin_BUTTON_DASHBOARD_ROBOT_3X->fn->setToggleable(Marvin_BUTTON_DASHBOARD_ROBOT_3X, LE_TRUE);
    Marvin_BUTTON_DASHBOARD_ROBOT_3X->fn->setString(Marvin_BUTTON_DASHBOARD_ROBOT_3X, (leString*)&string_PLAYER_MULTIPLIER_3x);
    Marvin_BUTTON_DASHBOARD_ROBOT_3X->fn->setPressedOffset(Marvin_BUTTON_DASHBOARD_ROBOT_3X, 0);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_BUTTON_DASHBOARD_ROBOT_3X);

    Marvin_BUTTON_DASHBOARD_ROBOT_4X = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_ROBOT_4X->fn->setPosition(Marvin_BUTTON_DASHBOARD_ROBOT_4X, 223, 221);
    Marvin_BUTTON_DASHBOARD_ROBOT_4X->fn->setSize(Marvin_BUTTON_DASHBOARD_ROBOT_4X, 23, 16);
    Marvin_BUTTON_DASHBOARD_ROBOT_4X->fn->setScheme(Marvin_BUTTON_DASHBOARD_ROBOT_4X, &SCHEME_PILL_ZINC_800);
    Marvin_BUTTON_DASHBOARD_ROBOT_4X->fn->setBorderType(Marvin_BUTTON_DASHBOARD_ROBOT_4X, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_DASHBOARD_ROBOT_4X->fn->setVAlignment(Marvin_BUTTON_DASHBOARD_ROBOT_4X, LE_VALIGN_TOP);
    Marvin_BUTTON_DASHBOARD_ROBOT_4X->fn->setMargins(Marvin_BUTTON_DASHBOARD_ROBOT_4X, 4, 0, 4, 4);
    Marvin_BUTTON_DASHBOARD_ROBOT_4X->fn->setToggleable(Marvin_BUTTON_DASHBOARD_ROBOT_4X, LE_TRUE);
    Marvin_BUTTON_DASHBOARD_ROBOT_4X->fn->setString(Marvin_BUTTON_DASHBOARD_ROBOT_4X, (leString*)&string_PLAYER_MULTIPLIER_4x);
    Marvin_BUTTON_DASHBOARD_ROBOT_4X->fn->setPressedOffset(Marvin_BUTTON_DASHBOARD_ROBOT_4X, 0);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_BUTTON_DASHBOARD_ROBOT_4X);

    Marvin_LABEL_DASHBOARD_ROBOT_Score = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_ROBOT_Score->fn->setPosition(Marvin_LABEL_DASHBOARD_ROBOT_Score, 13, 241);
    Marvin_LABEL_DASHBOARD_ROBOT_Score->fn->setSize(Marvin_LABEL_DASHBOARD_ROBOT_Score, 85, 32);
    Marvin_LABEL_DASHBOARD_ROBOT_Score->fn->setScheme(Marvin_LABEL_DASHBOARD_ROBOT_Score, &STYLE_TEXT_ROBOT);
    Marvin_LABEL_DASHBOARD_ROBOT_Score->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_ROBOT_Score, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_ROBOT_Score->fn->setHAlignment(Marvin_LABEL_DASHBOARD_ROBOT_Score, LE_HALIGN_RIGHT);
    Marvin_LABEL_DASHBOARD_ROBOT_Score->fn->setVAlignment(Marvin_LABEL_DASHBOARD_ROBOT_Score, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_ROBOT_Score->fn->setMargins(Marvin_LABEL_DASHBOARD_ROBOT_Score, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_ROBOT_Score->fn->setString(Marvin_LABEL_DASHBOARD_ROBOT_Score, (leString*)&string_PLAYER_ROBOT_Score);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_LABEL_DASHBOARD_ROBOT_Score);

    Marvin_LABEL_DASHBOARD_ROBOT_STREAK = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_ROBOT_STREAK->fn->setPosition(Marvin_LABEL_DASHBOARD_ROBOT_STREAK, 13, 283);
    Marvin_LABEL_DASHBOARD_ROBOT_STREAK->fn->setSize(Marvin_LABEL_DASHBOARD_ROBOT_STREAK, 60, 16);
    Marvin_LABEL_DASHBOARD_ROBOT_STREAK->fn->setScheme(Marvin_LABEL_DASHBOARD_ROBOT_STREAK, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_DASHBOARD_ROBOT_STREAK->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_ROBOT_STREAK, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_ROBOT_STREAK->fn->setVAlignment(Marvin_LABEL_DASHBOARD_ROBOT_STREAK, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_ROBOT_STREAK->fn->setMargins(Marvin_LABEL_DASHBOARD_ROBOT_STREAK, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_ROBOT_STREAK->fn->setString(Marvin_LABEL_DASHBOARD_ROBOT_STREAK, (leString*)&string_PLAYER_STREAK);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_LABEL_DASHBOARD_ROBOT_STREAK);

    Marvin_LABEL_DASHBOARD_ROBOT_Streak = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_ROBOT_Streak->fn->setPosition(Marvin_LABEL_DASHBOARD_ROBOT_Streak, 92, 283);
    Marvin_LABEL_DASHBOARD_ROBOT_Streak->fn->setSize(Marvin_LABEL_DASHBOARD_ROBOT_Streak, 32, 16);
    Marvin_LABEL_DASHBOARD_ROBOT_Streak->fn->setScheme(Marvin_LABEL_DASHBOARD_ROBOT_Streak, &SCHEME_TEXT_ZINC_300);
    Marvin_LABEL_DASHBOARD_ROBOT_Streak->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_ROBOT_Streak, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_ROBOT_Streak->fn->setHAlignment(Marvin_LABEL_DASHBOARD_ROBOT_Streak, LE_HALIGN_RIGHT);
    Marvin_LABEL_DASHBOARD_ROBOT_Streak->fn->setVAlignment(Marvin_LABEL_DASHBOARD_ROBOT_Streak, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_ROBOT_Streak->fn->setMargins(Marvin_LABEL_DASHBOARD_ROBOT_Streak, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_ROBOT_Streak->fn->setString(Marvin_LABEL_DASHBOARD_ROBOT_Streak, (leString*)&string_figmaStr__0_0);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_LABEL_DASHBOARD_ROBOT_Streak);

    Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY->fn->setPosition(Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY, 132, 283);
    Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY->fn->setSize(Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY, 70, 16);
    Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY->fn->setScheme(Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY->fn->setVAlignment(Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY->fn->setMargins(Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY->fn->setString(Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY, (leString*)&string_PLAYER_ACCURACY);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY);

    Marvin_LABEL_DASHBOARD_ROBOT_Accuracy = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_ROBOT_Accuracy->fn->setPosition(Marvin_LABEL_DASHBOARD_ROBOT_Accuracy, 235, 283);
    Marvin_LABEL_DASHBOARD_ROBOT_Accuracy->fn->setSize(Marvin_LABEL_DASHBOARD_ROBOT_Accuracy, 8, 16);
    Marvin_LABEL_DASHBOARD_ROBOT_Accuracy->fn->setScheme(Marvin_LABEL_DASHBOARD_ROBOT_Accuracy, &SCHEME_TEXT_ZINC_300);
    Marvin_LABEL_DASHBOARD_ROBOT_Accuracy->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_ROBOT_Accuracy, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_ROBOT_Accuracy->fn->setVAlignment(Marvin_LABEL_DASHBOARD_ROBOT_Accuracy, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_ROBOT_Accuracy->fn->setMargins(Marvin_LABEL_DASHBOARD_ROBOT_Accuracy, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_ROBOT_Accuracy->fn->setString(Marvin_LABEL_DASHBOARD_ROBOT_Accuracy, (leString*)&string_figmaStr__);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_LABEL_DASHBOARD_ROBOT_Accuracy);

    Marvin_PROGRESSBAR_DASHBOARD_ROBOT_Accuracy = leProgressBarWidget_New();
    Marvin_PROGRESSBAR_DASHBOARD_ROBOT_Accuracy->fn->setPosition(Marvin_PROGRESSBAR_DASHBOARD_ROBOT_Accuracy, 132, 303);
    Marvin_PROGRESSBAR_DASHBOARD_ROBOT_Accuracy->fn->setSize(Marvin_PROGRESSBAR_DASHBOARD_ROBOT_Accuracy, 111, 6);
    Marvin_PROGRESSBAR_DASHBOARD_ROBOT_Accuracy->fn->setScheme(Marvin_PROGRESSBAR_DASHBOARD_ROBOT_Accuracy, &SCHEME_FILL_ZINC_800);
    Marvin_PROGRESSBAR_DASHBOARD_ROBOT_Accuracy->fn->setBorderType(Marvin_PROGRESSBAR_DASHBOARD_ROBOT_Accuracy, LE_WIDGET_BORDER_NONE);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_PROGRESSBAR_DASHBOARD_ROBOT_Accuracy);

    Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER->fn->setPosition(Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER, 13, 319);
    Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER->fn->setSize(Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER, 87, 16);
    Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER->fn->setScheme(Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER->fn->setVAlignment(Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER->fn->setMargins(Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER->fn->setString(Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER, (leString*)&string_PLAYER_STAR_POWER);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER);

    Marvin_LABEL_DASHBOARD_ROBOT_StarPower = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_ROBOT_StarPower->fn->setPosition(Marvin_LABEL_DASHBOARD_ROBOT_StarPower, 235, 319);
    Marvin_LABEL_DASHBOARD_ROBOT_StarPower->fn->setSize(Marvin_LABEL_DASHBOARD_ROBOT_StarPower, 8, 16);
    Marvin_LABEL_DASHBOARD_ROBOT_StarPower->fn->setScheme(Marvin_LABEL_DASHBOARD_ROBOT_StarPower, &SCHEME_TEXT_ZINC_300);
    Marvin_LABEL_DASHBOARD_ROBOT_StarPower->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_ROBOT_StarPower, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_ROBOT_StarPower->fn->setVAlignment(Marvin_LABEL_DASHBOARD_ROBOT_StarPower, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_ROBOT_StarPower->fn->setMargins(Marvin_LABEL_DASHBOARD_ROBOT_StarPower, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_ROBOT_StarPower->fn->setString(Marvin_LABEL_DASHBOARD_ROBOT_StarPower, (leString*)&string_figmaStr___0);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_LABEL_DASHBOARD_ROBOT_StarPower);

    Marvin_PROGRESSBAR_DASHBOARD_ROBOT_StarPower = leProgressBarWidget_New();
    Marvin_PROGRESSBAR_DASHBOARD_ROBOT_StarPower->fn->setPosition(Marvin_PROGRESSBAR_DASHBOARD_ROBOT_StarPower, 13, 339);
    Marvin_PROGRESSBAR_DASHBOARD_ROBOT_StarPower->fn->setSize(Marvin_PROGRESSBAR_DASHBOARD_ROBOT_StarPower, 230, 8);
    Marvin_PROGRESSBAR_DASHBOARD_ROBOT_StarPower->fn->setScheme(Marvin_PROGRESSBAR_DASHBOARD_ROBOT_StarPower, &SCHEME_FILL_ZINC_800);
    Marvin_PROGRESSBAR_DASHBOARD_ROBOT_StarPower->fn->setBorderType(Marvin_PROGRESSBAR_DASHBOARD_ROBOT_StarPower, LE_WIDGET_BORDER_NONE);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_PROGRESSBAR_DASHBOARD_ROBOT_StarPower);

    Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_1 = leWidget_New();
    Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_1->fn->setPosition(Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_1, 13, 357);
    Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_1->fn->setSize(Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_1, 230, 1);
    Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_1->fn->setScheme(Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_1, &SCHEME_FILL_ZINC_800);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_1);

    Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY->fn->setPosition(Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY, 13, 368);
    Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY->fn->setSize(Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY, 231, 16);
    Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY->fn->setScheme(Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY->fn->setVAlignment(Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY->fn->setMargins(Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY->fn->setString(Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY, (leString*)&string_PLAYER_ROBOT_FRET_ACTIVITY);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY);

    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN->fn->setPosition(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN, 13, 390);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN->fn->setSize(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN, 43, 24);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN->fn->setScheme(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN, &SCHEME_GUITAR_FRET_GREEN);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN->fn->setBorderType(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN->fn->setToggleable(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN, LE_TRUE);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN);

    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED->fn->setPosition(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED, 60, 390);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED->fn->setSize(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED, 43, 24);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED->fn->setScheme(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED, &SCHEME_GUITAR_FRET_RED);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED->fn->setBorderType(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED->fn->setToggleable(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED, LE_TRUE);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED);

    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW->fn->setPosition(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW, 107, 390);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW->fn->setSize(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW, 43, 24);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW->fn->setScheme(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW, &SCHEME_GUITAR_FRET_YELLOW);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW->fn->setBorderType(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW->fn->setToggleable(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW, LE_TRUE);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW);

    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE->fn->setPosition(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE, 154, 390);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE->fn->setSize(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE, 43, 24);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE->fn->setScheme(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE, &SCHEME_GUITAR_FRET_BLUE);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE->fn->setBorderType(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE->fn->setToggleable(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE, LE_TRUE);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE);

    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE->fn->setPosition(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE, 201, 390);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE->fn->setSize(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE, 43, 24);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE->fn->setScheme(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE, &SCHEME_GUITAR_FRET_ORANGE);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE->fn->setBorderType(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE->fn->setToggleable(Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE, LE_TRUE);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE);

    Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR->fn->setPosition(Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR, 13, 426);
    Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR->fn->setSize(Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR, 96, 16);
    Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR->fn->setScheme(Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR->fn->setVAlignment(Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR->fn->setMargins(Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR->fn->setString(Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR, (leString*)&string_PLAYER_STRUM_BAR);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR);

    Marvin_panel_Container_36_0 = leWidget_New();
    Marvin_panel_Container_36_0->fn->setPosition(Marvin_panel_Container_36_0, 198, 424);
    Marvin_panel_Container_36_0->fn->setSize(Marvin_panel_Container_36_0, 45, 20);
    Marvin_panel_Container_36_0->fn->setScheme(Marvin_panel_Container_36_0, &SCHEME_FILL_ZINC_800);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_panel_Container_36_0);

    Marvin_label_IDLE_0_0 = leLabelWidget_New();
    Marvin_label_IDLE_0_0->fn->setPosition(Marvin_label_IDLE_0_0, 8, 2);
    Marvin_label_IDLE_0_0->fn->setSize(Marvin_label_IDLE_0_0, 29, 16);
    Marvin_label_IDLE_0_0->fn->setScheme(Marvin_label_IDLE_0_0, &SCHEME_TEXT_ZINC_600);
    Marvin_label_IDLE_0_0->fn->setBackgroundType(Marvin_label_IDLE_0_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_label_IDLE_0_0->fn->setVAlignment(Marvin_label_IDLE_0_0, LE_VALIGN_TOP);
    Marvin_label_IDLE_0_0->fn->setMargins(Marvin_label_IDLE_0_0, 0, 0, 0, 0);
    Marvin_label_IDLE_0_0->fn->setString(Marvin_label_IDLE_0_0, (leString*)&string_figmaStr_IDLE_0);
    Marvin_panel_Container_36_0->fn->addChild(Marvin_panel_Container_36_0, (leWidget*)Marvin_label_IDLE_0_0);

    Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_2 = leWidget_New();
    Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_2->fn->setPosition(Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_2, 13, 454);
    Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_2->fn->setSize(Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_2, 230, 1);
    Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_2->fn->setScheme(Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_2, &SCHEME_FILL_ZINC_800);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_2);

    Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR->fn->setPosition(Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR, 13, 465);
    Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR->fn->setSize(Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR, 230, 16);
    Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR->fn->setScheme(Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR->fn->setVAlignment(Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR->fn->setMargins(Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR->fn->setString(Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR, (leString*)&string_PLAYER_ROBOT_DETECTOR);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR);

    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN->fn->setPosition(Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN, 13, 487);
    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN->fn->setSize(Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN, 230, 30);
    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN->fn->setScheme(Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN, &SCHEME_PILL_ZINC_700);
    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN->fn->setBorderType(Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN->fn->setVAlignment(Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN, LE_VALIGN_TOP);
    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN->fn->setMargins(Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN, 4, 7, 4, 4);
    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN->fn->setString(Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN, (leString*)&string_PLAYER_ROBOT_Neural_Network);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN);

    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV->fn->setPosition(Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV, 13, 523);
    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV->fn->setSize(Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV, 230, 30);
    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV->fn->setScheme(Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV, &SCHEME_PILL_ZINC_700);
    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV->fn->setBorderType(Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV->fn->setVAlignment(Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV, LE_VALIGN_TOP);
    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV->fn->setMargins(Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV, 4, 7, 4, 4);
    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV->fn->setString(Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV, (leString*)&string_PLAYER_ROBOT_Computer_Vision);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV);

    Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_3 = leWidget_New();
    Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_3->fn->setPosition(Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_3, 13, 563);
    Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_3->fn->setSize(Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_3, 230, 1);
    Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_3->fn->setScheme(Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_3, &SCHEME_FILL_ZINC_800);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_3);

    Marvin_PANEL_DASHBOARD_ROBOT_BORDER = leWidget_New();
    Marvin_PANEL_DASHBOARD_ROBOT_BORDER->fn->setPosition(Marvin_PANEL_DASHBOARD_ROBOT_BORDER, 0, 0);
    Marvin_PANEL_DASHBOARD_ROBOT_BORDER->fn->setSize(Marvin_PANEL_DASHBOARD_ROBOT_BORDER, 256, 716);
    Marvin_PANEL_DASHBOARD_ROBOT_BORDER->fn->setScheme(Marvin_PANEL_DASHBOARD_ROBOT_BORDER, &SCHEME_FILL_ZINC_900);
    Marvin_PANEL_DASHBOARD_ROBOT_BORDER->fn->setBackgroundType(Marvin_PANEL_DASHBOARD_ROBOT_BORDER, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_DASHBOARD_ROBOT_BORDER->fn->setBorderType(Marvin_PANEL_DASHBOARD_ROBOT_BORDER, LE_WIDGET_BORDER_LINE);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_PANEL_DASHBOARD_ROBOT_BORDER);

    Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS->fn->setPosition(Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS, 13, 574);
    Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS->fn->setSize(Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS, 116, 16);
    Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS->fn->setScheme(Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS->fn->setVAlignment(Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS->fn->setMargins(Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS->fn->setString(Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS, (leString*)&string_PLAYER_ROBOT_GUITAR_ACTUATORS);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS);

    Marvin_BUTTON_DASHBOARD_ROBOT_ACTUATOR_GUITAR_1 = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_ROBOT_ACTUATOR_GUITAR_1->fn->setPosition(Marvin_BUTTON_DASHBOARD_ROBOT_ACTUATOR_GUITAR_1, 13, 596);
    Marvin_BUTTON_DASHBOARD_ROBOT_ACTUATOR_GUITAR_1->fn->setSize(Marvin_BUTTON_DASHBOARD_ROBOT_ACTUATOR_GUITAR_1, 230, 34);
    Marvin_BUTTON_DASHBOARD_ROBOT_ACTUATOR_GUITAR_1->fn->setScheme(Marvin_BUTTON_DASHBOARD_ROBOT_ACTUATOR_GUITAR_1, &SCHEME_PILL_ZINC_700);
    Marvin_BUTTON_DASHBOARD_ROBOT_ACTUATOR_GUITAR_1->fn->setBorderType(Marvin_BUTTON_DASHBOARD_ROBOT_ACTUATOR_GUITAR_1, LE_WIDGET_BORDER_NONE);
    Marvin_PANEL_DASHBOARD_ROBOT->fn->addChild(Marvin_PANEL_DASHBOARD_ROBOT, (leWidget*)Marvin_BUTTON_DASHBOARD_ROBOT_ACTUATOR_GUITAR_1);

    Marvin_PANEL_DASHBOARD_GAMEPLAY = leWidget_New();
    Marvin_PANEL_DASHBOARD_GAMEPLAY->fn->setPosition(Marvin_PANEL_DASHBOARD_GAMEPLAY, 268, 12);
    Marvin_PANEL_DASHBOARD_GAMEPLAY->fn->setSize(Marvin_PANEL_DASHBOARD_GAMEPLAY, 720, 716);
    Marvin_PANEL_DASHBOARD_GAMEPLAY->fn->setScheme(Marvin_PANEL_DASHBOARD_GAMEPLAY, &SCHEME_BACKGROUND);
    Marvin_PANEL_DASHBOARD_GAMEPLAY->fn->setBackgroundType(Marvin_PANEL_DASHBOARD_GAMEPLAY, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_DASHBOARD_BOTTOM->fn->addChild(Marvin_PANEL_DASHBOARD_BOTTOM, (leWidget*)Marvin_PANEL_DASHBOARD_GAMEPLAY);

    Marvin_PANEL_DASHBOARD_VIDEO = leWidget_New();
    Marvin_PANEL_DASHBOARD_VIDEO->fn->setPosition(Marvin_PANEL_DASHBOARD_VIDEO, 0, 0);
    Marvin_PANEL_DASHBOARD_VIDEO->fn->setSize(Marvin_PANEL_DASHBOARD_VIDEO, 720, 480);
    Marvin_PANEL_DASHBOARD_VIDEO->fn->setBackgroundType(Marvin_PANEL_DASHBOARD_VIDEO, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_DASHBOARD_GAMEPLAY->fn->addChild(Marvin_PANEL_DASHBOARD_GAMEPLAY, (leWidget*)Marvin_PANEL_DASHBOARD_VIDEO);

    Marvin_PANEL_DASHBOARD_TEST_BAR_WHITE = leWidget_New();
    Marvin_PANEL_DASHBOARD_TEST_BAR_WHITE->fn->setPosition(Marvin_PANEL_DASHBOARD_TEST_BAR_WHITE, 1, 1);
    Marvin_PANEL_DASHBOARD_TEST_BAR_WHITE->fn->setSize(Marvin_PANEL_DASHBOARD_TEST_BAR_WHITE, 103, 320);
    Marvin_PANEL_DASHBOARD_TEST_BAR_WHITE->fn->setScheme(Marvin_PANEL_DASHBOARD_TEST_BAR_WHITE, &SCHEME_TEST_PATTERN_WHITE);
    Marvin_PANEL_DASHBOARD_VIDEO->fn->addChild(Marvin_PANEL_DASHBOARD_VIDEO, (leWidget*)Marvin_PANEL_DASHBOARD_TEST_BAR_WHITE);

    Marvin_PANEL_DASHBOARD_TEST_BAR_YELLOW = leWidget_New();
    Marvin_PANEL_DASHBOARD_TEST_BAR_YELLOW->fn->setPosition(Marvin_PANEL_DASHBOARD_TEST_BAR_YELLOW, 104, 1);
    Marvin_PANEL_DASHBOARD_TEST_BAR_YELLOW->fn->setSize(Marvin_PANEL_DASHBOARD_TEST_BAR_YELLOW, 102, 320);
    Marvin_PANEL_DASHBOARD_TEST_BAR_YELLOW->fn->setScheme(Marvin_PANEL_DASHBOARD_TEST_BAR_YELLOW, &SCHEME_TEST_PATTERN_YELLOW);
    Marvin_PANEL_DASHBOARD_VIDEO->fn->addChild(Marvin_PANEL_DASHBOARD_VIDEO, (leWidget*)Marvin_PANEL_DASHBOARD_TEST_BAR_YELLOW);

    Marvin_PANEL_DASHBOARD_TEST_BAR_CYAN = leWidget_New();
    Marvin_PANEL_DASHBOARD_TEST_BAR_CYAN->fn->setPosition(Marvin_PANEL_DASHBOARD_TEST_BAR_CYAN, 206, 1);
    Marvin_PANEL_DASHBOARD_TEST_BAR_CYAN->fn->setSize(Marvin_PANEL_DASHBOARD_TEST_BAR_CYAN, 103, 320);
    Marvin_PANEL_DASHBOARD_TEST_BAR_CYAN->fn->setScheme(Marvin_PANEL_DASHBOARD_TEST_BAR_CYAN, &SCHEME_TEST_PATTERN_CYAN);
    Marvin_PANEL_DASHBOARD_VIDEO->fn->addChild(Marvin_PANEL_DASHBOARD_VIDEO, (leWidget*)Marvin_PANEL_DASHBOARD_TEST_BAR_CYAN);

    Marvin_PANEL_DASHBOARD_TEST_BAR_GREEN = leWidget_New();
    Marvin_PANEL_DASHBOARD_TEST_BAR_GREEN->fn->setPosition(Marvin_PANEL_DASHBOARD_TEST_BAR_GREEN, 309, 1);
    Marvin_PANEL_DASHBOARD_TEST_BAR_GREEN->fn->setSize(Marvin_PANEL_DASHBOARD_TEST_BAR_GREEN, 102, 320);
    Marvin_PANEL_DASHBOARD_TEST_BAR_GREEN->fn->setScheme(Marvin_PANEL_DASHBOARD_TEST_BAR_GREEN, &SCHEME_TEST_PATTERN_GREEN);
    Marvin_PANEL_DASHBOARD_VIDEO->fn->addChild(Marvin_PANEL_DASHBOARD_VIDEO, (leWidget*)Marvin_PANEL_DASHBOARD_TEST_BAR_GREEN);

    Marvin_PANEL_DASHBOARD_TEST_BAR_MAGENTA = leWidget_New();
    Marvin_PANEL_DASHBOARD_TEST_BAR_MAGENTA->fn->setPosition(Marvin_PANEL_DASHBOARD_TEST_BAR_MAGENTA, 411, 1);
    Marvin_PANEL_DASHBOARD_TEST_BAR_MAGENTA->fn->setSize(Marvin_PANEL_DASHBOARD_TEST_BAR_MAGENTA, 103, 320);
    Marvin_PANEL_DASHBOARD_TEST_BAR_MAGENTA->fn->setScheme(Marvin_PANEL_DASHBOARD_TEST_BAR_MAGENTA, &SCHEME_TEST_PATTERN_MAGENTA);
    Marvin_PANEL_DASHBOARD_VIDEO->fn->addChild(Marvin_PANEL_DASHBOARD_VIDEO, (leWidget*)Marvin_PANEL_DASHBOARD_TEST_BAR_MAGENTA);

    Marvin_PANEL_DASHBOARD_TEST_BAR_RED = leWidget_New();
    Marvin_PANEL_DASHBOARD_TEST_BAR_RED->fn->setPosition(Marvin_PANEL_DASHBOARD_TEST_BAR_RED, 514, 1);
    Marvin_PANEL_DASHBOARD_TEST_BAR_RED->fn->setSize(Marvin_PANEL_DASHBOARD_TEST_BAR_RED, 102, 320);
    Marvin_PANEL_DASHBOARD_TEST_BAR_RED->fn->setScheme(Marvin_PANEL_DASHBOARD_TEST_BAR_RED, &SCHEME_TEST_PATTERN_RED);
    Marvin_PANEL_DASHBOARD_VIDEO->fn->addChild(Marvin_PANEL_DASHBOARD_VIDEO, (leWidget*)Marvin_PANEL_DASHBOARD_TEST_BAR_RED);

    Marvin_PANEL_DASHBOARD_TEST_BAR_BLUE = leWidget_New();
    Marvin_PANEL_DASHBOARD_TEST_BAR_BLUE->fn->setPosition(Marvin_PANEL_DASHBOARD_TEST_BAR_BLUE, 616, 1);
    Marvin_PANEL_DASHBOARD_TEST_BAR_BLUE->fn->setSize(Marvin_PANEL_DASHBOARD_TEST_BAR_BLUE, 103, 320);
    Marvin_PANEL_DASHBOARD_TEST_BAR_BLUE->fn->setScheme(Marvin_PANEL_DASHBOARD_TEST_BAR_BLUE, &SCHEME_TEST_PATTERN_BLUE);
    Marvin_PANEL_DASHBOARD_VIDEO->fn->addChild(Marvin_PANEL_DASHBOARD_VIDEO, (leWidget*)Marvin_PANEL_DASHBOARD_TEST_BAR_BLUE);

    Marvin_GRADIENT_DASHBOARD_TEST_BAR_GRAY = leGradientWidget_New();
    Marvin_GRADIENT_DASHBOARD_TEST_BAR_GRAY->fn->setPosition(Marvin_GRADIENT_DASHBOARD_TEST_BAR_GRAY, 1, 379);
    Marvin_GRADIENT_DASHBOARD_TEST_BAR_GRAY->fn->setSize(Marvin_GRADIENT_DASHBOARD_TEST_BAR_GRAY, 718, 100);
    Marvin_GRADIENT_DASHBOARD_TEST_BAR_GRAY->fn->setScheme(Marvin_GRADIENT_DASHBOARD_TEST_BAR_GRAY, &SCHEME_BACKGROUND);
    Marvin_PANEL_DASHBOARD_VIDEO->fn->addChild(Marvin_PANEL_DASHBOARD_VIDEO, (leWidget*)Marvin_GRADIENT_DASHBOARD_TEST_BAR_GRAY);

    Marvin_PANEL_DASHBOARD_NO_SIGNAL = leWidget_New();
    Marvin_PANEL_DASHBOARD_NO_SIGNAL->fn->setPosition(Marvin_PANEL_DASHBOARD_NO_SIGNAL, 613, 10);
    Marvin_PANEL_DASHBOARD_NO_SIGNAL->fn->setSize(Marvin_PANEL_DASHBOARD_NO_SIGNAL, 98, 24);
    Marvin_PANEL_DASHBOARD_NO_SIGNAL->fn->setScheme(Marvin_PANEL_DASHBOARD_NO_SIGNAL, &SCHEME_BACKGROUND);
    Marvin_PANEL_DASHBOARD_VIDEO->fn->addChild(Marvin_PANEL_DASHBOARD_VIDEO, (leWidget*)Marvin_PANEL_DASHBOARD_NO_SIGNAL);

    Marvin_PANEL_DASHBOARD_NO_SIGNAL_LED = leWidget_New();
    Marvin_PANEL_DASHBOARD_NO_SIGNAL_LED->fn->setPosition(Marvin_PANEL_DASHBOARD_NO_SIGNAL_LED, 8, 8);
    Marvin_PANEL_DASHBOARD_NO_SIGNAL_LED->fn->setSize(Marvin_PANEL_DASHBOARD_NO_SIGNAL_LED, 9, 9);
    Marvin_PANEL_DASHBOARD_NO_SIGNAL_LED->fn->setScheme(Marvin_PANEL_DASHBOARD_NO_SIGNAL_LED, &SCHEME_TEST_PATTERN_RED);
    Marvin_PANEL_DASHBOARD_NO_SIGNAL->fn->addChild(Marvin_PANEL_DASHBOARD_NO_SIGNAL, (leWidget*)Marvin_PANEL_DASHBOARD_NO_SIGNAL_LED);

    Marvin_LABEL_DASHBOARD_NO_SIGNAL = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_NO_SIGNAL->fn->setPosition(Marvin_LABEL_DASHBOARD_NO_SIGNAL, 24, 4);
    Marvin_LABEL_DASHBOARD_NO_SIGNAL->fn->setSize(Marvin_LABEL_DASHBOARD_NO_SIGNAL, 66, 16);
    Marvin_LABEL_DASHBOARD_NO_SIGNAL->fn->setScheme(Marvin_LABEL_DASHBOARD_NO_SIGNAL, &SCHEME_TEXT_WHITE);
    Marvin_LABEL_DASHBOARD_NO_SIGNAL->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_NO_SIGNAL, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_NO_SIGNAL->fn->setVAlignment(Marvin_LABEL_DASHBOARD_NO_SIGNAL, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_NO_SIGNAL->fn->setMargins(Marvin_LABEL_DASHBOARD_NO_SIGNAL, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_NO_SIGNAL->fn->setString(Marvin_LABEL_DASHBOARD_NO_SIGNAL, (leString*)&string_figmaStr_NO_SIGNAL_0);
    Marvin_PANEL_DASHBOARD_NO_SIGNAL->fn->addChild(Marvin_PANEL_DASHBOARD_NO_SIGNAL, (leWidget*)Marvin_LABEL_DASHBOARD_NO_SIGNAL);

    Marvin_PANEL_DASHBOARD_TEST_PATTERN_BORDER = leWidget_New();
    Marvin_PANEL_DASHBOARD_TEST_PATTERN_BORDER->fn->setPosition(Marvin_PANEL_DASHBOARD_TEST_PATTERN_BORDER, 0, 0);
    Marvin_PANEL_DASHBOARD_TEST_PATTERN_BORDER->fn->setSize(Marvin_PANEL_DASHBOARD_TEST_PATTERN_BORDER, 720, 480);
    Marvin_PANEL_DASHBOARD_TEST_PATTERN_BORDER->fn->setScheme(Marvin_PANEL_DASHBOARD_TEST_PATTERN_BORDER, &SCHEME_FILL_ZINC_900);
    Marvin_PANEL_DASHBOARD_TEST_PATTERN_BORDER->fn->setBackgroundType(Marvin_PANEL_DASHBOARD_TEST_PATTERN_BORDER, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_DASHBOARD_TEST_PATTERN_BORDER->fn->setBorderType(Marvin_PANEL_DASHBOARD_TEST_PATTERN_BORDER, LE_WIDGET_BORDER_LINE);
    Marvin_PANEL_DASHBOARD_VIDEO->fn->addChild(Marvin_PANEL_DASHBOARD_VIDEO, (leWidget*)Marvin_PANEL_DASHBOARD_TEST_PATTERN_BORDER);

    Marvin_PANEL_DASHBOARD_SONG = leWidget_New();
    Marvin_PANEL_DASHBOARD_SONG->fn->setPosition(Marvin_PANEL_DASHBOARD_SONG, 0, 492);
    Marvin_PANEL_DASHBOARD_SONG->fn->setSize(Marvin_PANEL_DASHBOARD_SONG, 720, 224);
    Marvin_PANEL_DASHBOARD_SONG->fn->setScheme(Marvin_PANEL_DASHBOARD_SONG, &SCHEME_FILL_ZINC_900);
    Marvin_PANEL_DASHBOARD_SONG->fn->setBorderType(Marvin_PANEL_DASHBOARD_SONG, LE_WIDGET_BORDER_LINE);
    Marvin_PANEL_DASHBOARD_GAMEPLAY->fn->addChild(Marvin_PANEL_DASHBOARD_GAMEPLAY, (leWidget*)Marvin_PANEL_DASHBOARD_SONG);

    Marvin_PANEL_DASHBOARD_SONG_INFO = leWidget_New();
    Marvin_PANEL_DASHBOARD_SONG_INFO->fn->setPosition(Marvin_PANEL_DASHBOARD_SONG_INFO, 1, 1);
    Marvin_PANEL_DASHBOARD_SONG_INFO->fn->setSize(Marvin_PANEL_DASHBOARD_SONG_INFO, 541, 222);
    Marvin_PANEL_DASHBOARD_SONG_INFO->fn->setBackgroundType(Marvin_PANEL_DASHBOARD_SONG_INFO, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_DASHBOARD_SONG->fn->addChild(Marvin_PANEL_DASHBOARD_SONG, (leWidget*)Marvin_PANEL_DASHBOARD_SONG_INFO);

    Marvin_PANEL_DASHBOARD_SONG_AlbumArt = leImageWidget_New();
    Marvin_PANEL_DASHBOARD_SONG_AlbumArt->fn->setPosition(Marvin_PANEL_DASHBOARD_SONG_AlbumArt, 16, 16);
    Marvin_PANEL_DASHBOARD_SONG_AlbumArt->fn->setSize(Marvin_PANEL_DASHBOARD_SONG_AlbumArt, 144, 144);
    Marvin_PANEL_DASHBOARD_SONG_AlbumArt->fn->setBackgroundType(Marvin_PANEL_DASHBOARD_SONG_AlbumArt, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_DASHBOARD_SONG_AlbumArt->fn->setBorderType(Marvin_PANEL_DASHBOARD_SONG_AlbumArt, LE_WIDGET_BORDER_NONE);
    Marvin_PANEL_DASHBOARD_SONG_INFO->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_INFO, (leWidget*)Marvin_PANEL_DASHBOARD_SONG_AlbumArt);

    Marvin_PANEL_DASHBOARD_SONG_ALBUM_ART_BORDER = leWidget_New();
    Marvin_PANEL_DASHBOARD_SONG_ALBUM_ART_BORDER->fn->setPosition(Marvin_PANEL_DASHBOARD_SONG_ALBUM_ART_BORDER, 16, 16);
    Marvin_PANEL_DASHBOARD_SONG_ALBUM_ART_BORDER->fn->setSize(Marvin_PANEL_DASHBOARD_SONG_ALBUM_ART_BORDER, 144, 144);
    Marvin_PANEL_DASHBOARD_SONG_ALBUM_ART_BORDER->fn->setScheme(Marvin_PANEL_DASHBOARD_SONG_ALBUM_ART_BORDER, &SCHEME_FILL_ZINC_900);
    Marvin_PANEL_DASHBOARD_SONG_ALBUM_ART_BORDER->fn->setBackgroundType(Marvin_PANEL_DASHBOARD_SONG_ALBUM_ART_BORDER, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_DASHBOARD_SONG_ALBUM_ART_BORDER->fn->setBorderType(Marvin_PANEL_DASHBOARD_SONG_ALBUM_ART_BORDER, LE_WIDGET_BORDER_LINE);
    Marvin_PANEL_DASHBOARD_SONG_INFO->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_INFO, (leWidget*)Marvin_PANEL_DASHBOARD_SONG_ALBUM_ART_BORDER);

    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT = leWidget_New();
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT->fn->setPosition(Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT, 176, 16);
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT->fn->setSize(Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT, 349, 190);
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT->fn->setScheme(Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT, &SCHEME_BACKGROUND);
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT->fn->setBackgroundType(Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_DASHBOARD_SONG_INFO->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_INFO, (leWidget*)Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT);

    Marvin_LABEL_DASHBOARD_SONG_Status = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_SONG_Status->fn->setPosition(Marvin_LABEL_DASHBOARD_SONG_Status, 0, 0);
    Marvin_LABEL_DASHBOARD_SONG_Status->fn->setSize(Marvin_LABEL_DASHBOARD_SONG_Status, 350, 16);
    Marvin_LABEL_DASHBOARD_SONG_Status->fn->setScheme(Marvin_LABEL_DASHBOARD_SONG_Status, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_DASHBOARD_SONG_Status->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_SONG_Status, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_SONG_Status->fn->setVAlignment(Marvin_LABEL_DASHBOARD_SONG_Status, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_SONG_Status->fn->setMargins(Marvin_LABEL_DASHBOARD_SONG_Status, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_SONG_Status->fn->setString(Marvin_LABEL_DASHBOARD_SONG_Status, (leString*)&string_SONG_INFO_Status);
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT, (leWidget*)Marvin_LABEL_DASHBOARD_SONG_Status);

    Marvin_LABEL_DASHBOARD_SONG_SongTitle = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_SONG_SongTitle->fn->setPosition(Marvin_LABEL_DASHBOARD_SONG_SongTitle, 1, 18);
    Marvin_LABEL_DASHBOARD_SONG_SongTitle->fn->setSize(Marvin_LABEL_DASHBOARD_SONG_SongTitle, 350, 25);
    Marvin_LABEL_DASHBOARD_SONG_SongTitle->fn->setScheme(Marvin_LABEL_DASHBOARD_SONG_SongTitle, &SCHEME_TEXT_WHITE);
    Marvin_LABEL_DASHBOARD_SONG_SongTitle->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_SONG_SongTitle, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_SONG_SongTitle->fn->setVAlignment(Marvin_LABEL_DASHBOARD_SONG_SongTitle, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_SONG_SongTitle->fn->setMargins(Marvin_LABEL_DASHBOARD_SONG_SongTitle, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_SONG_SongTitle->fn->setString(Marvin_LABEL_DASHBOARD_SONG_SongTitle, (leString*)&string_SONG_INFO_Title);
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT, (leWidget*)Marvin_LABEL_DASHBOARD_SONG_SongTitle);

    Marvin_LABEL_DASHBOARD_SONG_SongArtist = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_SONG_SongArtist->fn->setPosition(Marvin_LABEL_DASHBOARD_SONG_SongArtist, 0, 43);
    Marvin_LABEL_DASHBOARD_SONG_SongArtist->fn->setSize(Marvin_LABEL_DASHBOARD_SONG_SongArtist, 350, 20);
    Marvin_LABEL_DASHBOARD_SONG_SongArtist->fn->setScheme(Marvin_LABEL_DASHBOARD_SONG_SongArtist, &SCHEME_TEXT_ZINC_400);
    Marvin_LABEL_DASHBOARD_SONG_SongArtist->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_SONG_SongArtist, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_SONG_SongArtist->fn->setVAlignment(Marvin_LABEL_DASHBOARD_SONG_SongArtist, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_SONG_SongArtist->fn->setMargins(Marvin_LABEL_DASHBOARD_SONG_SongArtist, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_SONG_SongArtist->fn->setString(Marvin_LABEL_DASHBOARD_SONG_SongArtist, (leString*)&string_SONG_INFO_Artist);
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT, (leWidget*)Marvin_LABEL_DASHBOARD_SONG_SongArtist);

    Marvin_LABEL_DASHBOARD_SONG_SongAlbum = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_SONG_SongAlbum->fn->setPosition(Marvin_LABEL_DASHBOARD_SONG_SongAlbum, 0, 63);
    Marvin_LABEL_DASHBOARD_SONG_SongAlbum->fn->setSize(Marvin_LABEL_DASHBOARD_SONG_SongAlbum, 350, 16);
    Marvin_LABEL_DASHBOARD_SONG_SongAlbum->fn->setScheme(Marvin_LABEL_DASHBOARD_SONG_SongAlbum, &SCHEME_TEXT_ZINC_600);
    Marvin_LABEL_DASHBOARD_SONG_SongAlbum->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_SONG_SongAlbum, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_SONG_SongAlbum->fn->setVAlignment(Marvin_LABEL_DASHBOARD_SONG_SongAlbum, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_SONG_SongAlbum->fn->setMargins(Marvin_LABEL_DASHBOARD_SONG_SongAlbum, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_SONG_SongAlbum->fn->setString(Marvin_LABEL_DASHBOARD_SONG_SongAlbum, (leString*)&string_SONG_INFO_Album_Year);
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT, (leWidget*)Marvin_LABEL_DASHBOARD_SONG_SongAlbum);

    Marvin_LABEL_DASHBOARD_SONG_GENRE = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_SONG_GENRE->fn->setPosition(Marvin_LABEL_DASHBOARD_SONG_GENRE, 0, 105);
    Marvin_LABEL_DASHBOARD_SONG_GENRE->fn->setSize(Marvin_LABEL_DASHBOARD_SONG_GENRE, 130, 16);
    Marvin_LABEL_DASHBOARD_SONG_GENRE->fn->setScheme(Marvin_LABEL_DASHBOARD_SONG_GENRE, &SCHEME_TEXT_ZINC_600);
    Marvin_LABEL_DASHBOARD_SONG_GENRE->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_SONG_GENRE, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_SONG_GENRE->fn->setVAlignment(Marvin_LABEL_DASHBOARD_SONG_GENRE, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_SONG_GENRE->fn->setMargins(Marvin_LABEL_DASHBOARD_SONG_GENRE, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_SONG_GENRE->fn->setString(Marvin_LABEL_DASHBOARD_SONG_GENRE, (leString*)&string_SONG_INFO_GENRE);
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT, (leWidget*)Marvin_LABEL_DASHBOARD_SONG_GENRE);

    Marvin_LABEL_DASHBOARD_SONG_SongGenre = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_SONG_SongGenre->fn->setPosition(Marvin_LABEL_DASHBOARD_SONG_SongGenre, 0, 121);
    Marvin_LABEL_DASHBOARD_SONG_SongGenre->fn->setSize(Marvin_LABEL_DASHBOARD_SONG_SongGenre, 130, 16);
    Marvin_LABEL_DASHBOARD_SONG_SongGenre->fn->setScheme(Marvin_LABEL_DASHBOARD_SONG_SongGenre, &SCHEME_TEXT_ZINC_300);
    Marvin_LABEL_DASHBOARD_SONG_SongGenre->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_SONG_SongGenre, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_SONG_SongGenre->fn->setVAlignment(Marvin_LABEL_DASHBOARD_SONG_SongGenre, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_SONG_SongGenre->fn->setMargins(Marvin_LABEL_DASHBOARD_SONG_SongGenre, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_SONG_SongGenre->fn->setString(Marvin_LABEL_DASHBOARD_SONG_SongGenre, (leString*)&string_SONG_INFO_Genre);
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT, (leWidget*)Marvin_LABEL_DASHBOARD_SONG_SongGenre);

    Marvin_LABEL_DASHBOARD_SONG_DURATION = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_SONG_DURATION->fn->setPosition(Marvin_LABEL_DASHBOARD_SONG_DURATION, 142, 105);
    Marvin_LABEL_DASHBOARD_SONG_DURATION->fn->setSize(Marvin_LABEL_DASHBOARD_SONG_DURATION, 70, 16);
    Marvin_LABEL_DASHBOARD_SONG_DURATION->fn->setScheme(Marvin_LABEL_DASHBOARD_SONG_DURATION, &SCHEME_TEXT_ZINC_600);
    Marvin_LABEL_DASHBOARD_SONG_DURATION->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_SONG_DURATION, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_SONG_DURATION->fn->setVAlignment(Marvin_LABEL_DASHBOARD_SONG_DURATION, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_SONG_DURATION->fn->setMargins(Marvin_LABEL_DASHBOARD_SONG_DURATION, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_SONG_DURATION->fn->setString(Marvin_LABEL_DASHBOARD_SONG_DURATION, (leString*)&string_SONG_INFO_DURATION);
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT, (leWidget*)Marvin_LABEL_DASHBOARD_SONG_DURATION);

    Marvin_LABEL_DASHBOARD_SONG_SongDuration = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_SONG_SongDuration->fn->setPosition(Marvin_LABEL_DASHBOARD_SONG_SongDuration, 142, 121);
    Marvin_LABEL_DASHBOARD_SONG_SongDuration->fn->setSize(Marvin_LABEL_DASHBOARD_SONG_SongDuration, 70, 16);
    Marvin_LABEL_DASHBOARD_SONG_SongDuration->fn->setScheme(Marvin_LABEL_DASHBOARD_SONG_SongDuration, &SCHEME_TEXT_ZINC_300);
    Marvin_LABEL_DASHBOARD_SONG_SongDuration->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_SONG_SongDuration, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_SONG_SongDuration->fn->setVAlignment(Marvin_LABEL_DASHBOARD_SONG_SongDuration, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_SONG_SongDuration->fn->setMargins(Marvin_LABEL_DASHBOARD_SONG_SongDuration, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_SONG_SongDuration->fn->setString(Marvin_LABEL_DASHBOARD_SONG_SongDuration, (leString*)&string_SONG_INFO_Duration);
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT, (leWidget*)Marvin_LABEL_DASHBOARD_SONG_SongDuration);

    Marvin_LABEL_DASHBOARD_SONG_TIER = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_SONG_TIER->fn->setPosition(Marvin_LABEL_DASHBOARD_SONG_TIER, 219, 105);
    Marvin_LABEL_DASHBOARD_SONG_TIER->fn->setSize(Marvin_LABEL_DASHBOARD_SONG_TIER, 140, 16);
    Marvin_LABEL_DASHBOARD_SONG_TIER->fn->setScheme(Marvin_LABEL_DASHBOARD_SONG_TIER, &SCHEME_TEXT_ZINC_600);
    Marvin_LABEL_DASHBOARD_SONG_TIER->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_SONG_TIER, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_SONG_TIER->fn->setVAlignment(Marvin_LABEL_DASHBOARD_SONG_TIER, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_SONG_TIER->fn->setMargins(Marvin_LABEL_DASHBOARD_SONG_TIER, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_SONG_TIER->fn->setString(Marvin_LABEL_DASHBOARD_SONG_TIER, (leString*)&string_SONG_INFO_TIER);
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT, (leWidget*)Marvin_LABEL_DASHBOARD_SONG_TIER);

    Marvin_LABEL_DASHBOARD_SONG_SongTier = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_SONG_SongTier->fn->setPosition(Marvin_LABEL_DASHBOARD_SONG_SongTier, 219, 121);
    Marvin_LABEL_DASHBOARD_SONG_SongTier->fn->setSize(Marvin_LABEL_DASHBOARD_SONG_SongTier, 140, 16);
    Marvin_LABEL_DASHBOARD_SONG_SongTier->fn->setScheme(Marvin_LABEL_DASHBOARD_SONG_SongTier, &SCHEME_TEXT_RED_500);
    Marvin_LABEL_DASHBOARD_SONG_SongTier->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_SONG_SongTier, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_SONG_SongTier->fn->setVAlignment(Marvin_LABEL_DASHBOARD_SONG_SongTier, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_SONG_SongTier->fn->setMargins(Marvin_LABEL_DASHBOARD_SONG_SongTier, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_SONG_SongTier->fn->setString(Marvin_LABEL_DASHBOARD_SONG_SongTier, (leString*)&string_SONG_INFO_Tier);
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT, (leWidget*)Marvin_LABEL_DASHBOARD_SONG_SongTier);

    Marvin_LABEL_DASHBOARD_SONG_START_TIME = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_SONG_START_TIME->fn->setPosition(Marvin_LABEL_DASHBOARD_SONG_START_TIME, 0, 161);
    Marvin_LABEL_DASHBOARD_SONG_START_TIME->fn->setSize(Marvin_LABEL_DASHBOARD_SONG_START_TIME, 29, 16);
    Marvin_LABEL_DASHBOARD_SONG_START_TIME->fn->setScheme(Marvin_LABEL_DASHBOARD_SONG_START_TIME, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_DASHBOARD_SONG_START_TIME->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_SONG_START_TIME, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_SONG_START_TIME->fn->setVAlignment(Marvin_LABEL_DASHBOARD_SONG_START_TIME, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_SONG_START_TIME->fn->setMargins(Marvin_LABEL_DASHBOARD_SONG_START_TIME, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_SONG_START_TIME->fn->setString(Marvin_LABEL_DASHBOARD_SONG_START_TIME, (leString*)&string_SONG_INFO_START_TIME);
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT, (leWidget*)Marvin_LABEL_DASHBOARD_SONG_START_TIME);

    Marvin_LABEL_DASHBOARD_SONG_STOP_TIME = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_SONG_STOP_TIME->fn->setPosition(Marvin_LABEL_DASHBOARD_SONG_STOP_TIME, 320, 161);
    Marvin_LABEL_DASHBOARD_SONG_STOP_TIME->fn->setSize(Marvin_LABEL_DASHBOARD_SONG_STOP_TIME, 29, 16);
    Marvin_LABEL_DASHBOARD_SONG_STOP_TIME->fn->setScheme(Marvin_LABEL_DASHBOARD_SONG_STOP_TIME, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_DASHBOARD_SONG_STOP_TIME->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_SONG_STOP_TIME, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_SONG_STOP_TIME->fn->setVAlignment(Marvin_LABEL_DASHBOARD_SONG_STOP_TIME, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_SONG_STOP_TIME->fn->setMargins(Marvin_LABEL_DASHBOARD_SONG_STOP_TIME, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_SONG_STOP_TIME->fn->setString(Marvin_LABEL_DASHBOARD_SONG_STOP_TIME, (leString*)&string_SONG_INFO_STOP_TIME);
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT, (leWidget*)Marvin_LABEL_DASHBOARD_SONG_STOP_TIME);

    Marvin_PROGRESSBAR_DASHBOARD_SONG_PLAYTIME = leProgressBarWidget_New();
    Marvin_PROGRESSBAR_DASHBOARD_SONG_PLAYTIME->fn->setPosition(Marvin_PROGRESSBAR_DASHBOARD_SONG_PLAYTIME, 0, 182);
    Marvin_PROGRESSBAR_DASHBOARD_SONG_PLAYTIME->fn->setSize(Marvin_PROGRESSBAR_DASHBOARD_SONG_PLAYTIME, 349, 8);
    Marvin_PROGRESSBAR_DASHBOARD_SONG_PLAYTIME->fn->setScheme(Marvin_PROGRESSBAR_DASHBOARD_SONG_PLAYTIME, &SCHEME_FILL_ZINC_800);
    Marvin_PROGRESSBAR_DASHBOARD_SONG_PLAYTIME->fn->setBorderType(Marvin_PROGRESSBAR_DASHBOARD_SONG_PLAYTIME, LE_WIDGET_BORDER_NONE);
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT, (leWidget*)Marvin_PROGRESSBAR_DASHBOARD_SONG_PLAYTIME);

    Marvin_PANEL_DASHBOARD_SONG_DIVIDER = leWidget_New();
    Marvin_PANEL_DASHBOARD_SONG_DIVIDER->fn->setPosition(Marvin_PANEL_DASHBOARD_SONG_DIVIDER, 542, 1);
    Marvin_PANEL_DASHBOARD_SONG_DIVIDER->fn->setSize(Marvin_PANEL_DASHBOARD_SONG_DIVIDER, 1, 222);
    Marvin_PANEL_DASHBOARD_SONG_DIVIDER->fn->setScheme(Marvin_PANEL_DASHBOARD_SONG_DIVIDER, &SCHEME_PILL_ZINC_700);
    Marvin_PANEL_DASHBOARD_SONG->fn->addChild(Marvin_PANEL_DASHBOARD_SONG, (leWidget*)Marvin_PANEL_DASHBOARD_SONG_DIVIDER);

    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY = leWidget_New();
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY->fn->setPosition(Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY, 543, 1);
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY->fn->setSize(Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY, 176, 222);
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY->fn->setBackgroundType(Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_DASHBOARD_SONG->fn->addChild(Marvin_PANEL_DASHBOARD_SONG, (leWidget*)Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY);

    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE->fn->setPosition(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE, 16, 16);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE->fn->setSize(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE, 40, 16);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE->fn->setScheme(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE, &SCHEME_TEXT_ZINC_600);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE->fn->setVAlignment(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE->fn->setMargins(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE->fn->setString(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE, (leString*)&string_SONG_GAMEPLAY_MODE);
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY, (leWidget*)Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE);

    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode->fn->setPosition(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode, 87, 16);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode->fn->setSize(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode, 73, 16);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode->fn->setScheme(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode, &SCHEME_TEXT_ZINC_300);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode->fn->setVAlignment(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode->fn->setMargins(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode->fn->setString(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode, (leString*)&string_GAMEPLAY_MODE_1P_ROBOT);
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY, (leWidget*)Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode);

    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY->fn->setPosition(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY, 16, 40);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY->fn->setSize(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY, 80, 16);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY->fn->setScheme(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY, &SCHEME_TEXT_ZINC_600);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY->fn->setVAlignment(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY->fn->setMargins(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY->fn->setString(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY, (leString*)&string_SONG_GAMEPLAY_DIFFICULTY);
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY, (leWidget*)Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY);

    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_Difficulty = leWidget_New();
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_Difficulty->fn->setPosition(Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_Difficulty, 104, 40);
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_Difficulty->fn->setSize(Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_Difficulty, 56, 20);
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_Difficulty->fn->setScheme(Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_Difficulty, &SCHEME_BUTTON_EASY);
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY, (leWidget*)Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_Difficulty);

    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty->fn->setPosition(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty, 0, 2);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty->fn->setSize(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty, 56, 18);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty->fn->setScheme(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty, &SCHEME_TEXT_RED_200);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty->fn->setHAlignment(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty, LE_HALIGN_CENTER);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty->fn->setVAlignment(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty->fn->setMargins(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty->fn->setString(Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty, (leString*)&string_SONG_GAMEPLAY_EASY);
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_Difficulty->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_Difficulty, (leWidget*)Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty);

    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_DIVIDER = leWidget_New();
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_DIVIDER->fn->setPosition(Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_DIVIDER, 16, 70);
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_DIVIDER->fn->setSize(Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_DIVIDER, 144, 1);
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_DIVIDER->fn->setScheme(Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_DIVIDER, &SCHEME_PILL_ZINC_700);
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY, (leWidget*)Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_DIVIDER);

    Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG->fn->setPosition(Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG, 16, 126);
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG->fn->setSize(Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG, 144, 36);
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG->fn->setScheme(Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG, &SCHEME_BUTTON_MODE);
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG->fn->setBorderType(Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG->fn->setString(Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG, (leString*)&string_GAMEPLAY_SELECT_SONG);
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG->fn->setPressedImage(Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG, (leImage*)&figmaImg_Icon_0_0);
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG->fn->setReleasedImage(Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG, (leImage*)&figmaImg_Icon_0_0);
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG->fn->setPressedOffset(Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG, 0);
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY, (leWidget*)Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG);

    Marvin_BUTTON_DASHBOARD_GAMEPLAY_START = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_START->fn->setPosition(Marvin_BUTTON_DASHBOARD_GAMEPLAY_START, 16, 170);
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_START->fn->setSize(Marvin_BUTTON_DASHBOARD_GAMEPLAY_START, 144, 36);
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_START->fn->setScheme(Marvin_BUTTON_DASHBOARD_GAMEPLAY_START, &SCHEME_GUITAR_FRET_GREEN);
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_START->fn->setBorderType(Marvin_BUTTON_DASHBOARD_GAMEPLAY_START, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_START->fn->setString(Marvin_BUTTON_DASHBOARD_GAMEPLAY_START, (leString*)&string_GAMEPLAY_START);
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_START->fn->setPressedImage(Marvin_BUTTON_DASHBOARD_GAMEPLAY_START, (leImage*)&figmaImg_Icon_1_0);
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_START->fn->setReleasedImage(Marvin_BUTTON_DASHBOARD_GAMEPLAY_START, (leImage*)&figmaImg_Icon_1_0);
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_START->fn->setPressedOffset(Marvin_BUTTON_DASHBOARD_GAMEPLAY_START, 0);
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY->fn->addChild(Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY, (leWidget*)Marvin_BUTTON_DASHBOARD_GAMEPLAY_START);

    Marvin_PANEL_DASHBOARD_HUMAN = leWidget_New();
    Marvin_PANEL_DASHBOARD_HUMAN->fn->setPosition(Marvin_PANEL_DASHBOARD_HUMAN, 1000, 12);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->setSize(Marvin_PANEL_DASHBOARD_HUMAN, 256, 716);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->setScheme(Marvin_PANEL_DASHBOARD_HUMAN, &SCHEME_FILL_ZINC_900);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->setBorderType(Marvin_PANEL_DASHBOARD_HUMAN, LE_WIDGET_BORDER_LINE);
    Marvin_PANEL_DASHBOARD_BOTTOM->fn->addChild(Marvin_PANEL_DASHBOARD_BOTTOM, (leWidget*)Marvin_PANEL_DASHBOARD_HUMAN);

    Marvin_IMAGE_DASHBOARD_HUMAN_PLAYER = leImageWidget_New();
    Marvin_IMAGE_DASHBOARD_HUMAN_PLAYER->fn->setPosition(Marvin_IMAGE_DASHBOARD_HUMAN_PLAYER, 1, 1);
    Marvin_IMAGE_DASHBOARD_HUMAN_PLAYER->fn->setSize(Marvin_IMAGE_DASHBOARD_HUMAN_PLAYER, 254, 208);
    Marvin_IMAGE_DASHBOARD_HUMAN_PLAYER->fn->setScheme(Marvin_IMAGE_DASHBOARD_HUMAN_PLAYER, &SCHEME_BACKGROUND);
    Marvin_IMAGE_DASHBOARD_HUMAN_PLAYER->fn->setBorderType(Marvin_IMAGE_DASHBOARD_HUMAN_PLAYER, LE_WIDGET_BORDER_NONE);
    Marvin_IMAGE_DASHBOARD_HUMAN_PLAYER->fn->setImage(Marvin_IMAGE_DASHBOARD_HUMAN_PLAYER, (leImage*)&HumanPlayer_gradient);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_IMAGE_DASHBOARD_HUMAN_PLAYER);

    Marvin_LABEL_DASHBOARD_HUMAN_Name = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_HUMAN_Name->fn->setPosition(Marvin_LABEL_DASHBOARD_HUMAN_Name, 13, 168);
    Marvin_LABEL_DASHBOARD_HUMAN_Name->fn->setSize(Marvin_LABEL_DASHBOARD_HUMAN_Name, 160, 18);
    Marvin_LABEL_DASHBOARD_HUMAN_Name->fn->setScheme(Marvin_LABEL_DASHBOARD_HUMAN_Name, &SCHEME_TEXT_HUMAN);
    Marvin_LABEL_DASHBOARD_HUMAN_Name->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_HUMAN_Name, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_HUMAN_Name->fn->setVAlignment(Marvin_LABEL_DASHBOARD_HUMAN_Name, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_HUMAN_Name->fn->setMargins(Marvin_LABEL_DASHBOARD_HUMAN_Name, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_HUMAN_Name->fn->setString(Marvin_LABEL_DASHBOARD_HUMAN_Name, (leString*)&string_PLAYER_HUMAN_Name);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_LABEL_DASHBOARD_HUMAN_Name);

    Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer->fn->setPosition(Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer, 13, 187);
    Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer->fn->setSize(Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer, 160, 16);
    Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer->fn->setScheme(Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer, &SCHEME_TEXT_ZINC_400);
    Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer->fn->setVAlignment(Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer->fn->setMargins(Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer->fn->setString(Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer, (leString*)&string_PLAYER_HUMAN_HumanPlayer);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer);

    Marvin_PANEL_DASHBOARD_HUMAN_STATE = leWidget_New();
    Marvin_PANEL_DASHBOARD_HUMAN_STATE->fn->setPosition(Marvin_PANEL_DASHBOARD_HUMAN_STATE, 186, 182);
    Marvin_PANEL_DASHBOARD_HUMAN_STATE->fn->setSize(Marvin_PANEL_DASHBOARD_HUMAN_STATE, 57, 20);
    Marvin_PANEL_DASHBOARD_HUMAN_STATE->fn->setScheme(Marvin_PANEL_DASHBOARD_HUMAN_STATE, &SCHEME_FILL_ZINC_800);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_PANEL_DASHBOARD_HUMAN_STATE);

    Marvin_PANEL_DASHBOARD_HUMAN_STATE_LED = leWidget_New();
    Marvin_PANEL_DASHBOARD_HUMAN_STATE_LED->fn->setPosition(Marvin_PANEL_DASHBOARD_HUMAN_STATE_LED, 8, 7);
    Marvin_PANEL_DASHBOARD_HUMAN_STATE_LED->fn->setSize(Marvin_PANEL_DASHBOARD_HUMAN_STATE_LED, 6, 6);
    Marvin_PANEL_DASHBOARD_HUMAN_STATE_LED->fn->setScheme(Marvin_PANEL_DASHBOARD_HUMAN_STATE_LED, &SCHEME_FILL_ZINC_600);
    Marvin_PANEL_DASHBOARD_HUMAN_STATE->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN_STATE, (leWidget*)Marvin_PANEL_DASHBOARD_HUMAN_STATE_LED);

    Marvin_LABEL_DASHBOARD_HUMAN_HumanState = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_HUMAN_HumanState->fn->setPosition(Marvin_LABEL_DASHBOARD_HUMAN_HumanState, 20, 2);
    Marvin_LABEL_DASHBOARD_HUMAN_HumanState->fn->setSize(Marvin_LABEL_DASHBOARD_HUMAN_HumanState, 29, 16);
    Marvin_LABEL_DASHBOARD_HUMAN_HumanState->fn->setScheme(Marvin_LABEL_DASHBOARD_HUMAN_HumanState, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_DASHBOARD_HUMAN_HumanState->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_HUMAN_HumanState, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_HUMAN_HumanState->fn->setVAlignment(Marvin_LABEL_DASHBOARD_HUMAN_HumanState, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_HUMAN_HumanState->fn->setMargins(Marvin_LABEL_DASHBOARD_HUMAN_HumanState, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_HUMAN_HumanState->fn->setString(Marvin_LABEL_DASHBOARD_HUMAN_HumanState, (leString*)&string_PLAYER_ROBOT_Status);
    Marvin_PANEL_DASHBOARD_HUMAN_STATE->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN_STATE, (leWidget*)Marvin_LABEL_DASHBOARD_HUMAN_HumanState);

    Marvin_LABEL_DASHBOARD_HUMAN_SCORE = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_HUMAN_SCORE->fn->setPosition(Marvin_LABEL_DASHBOARD_HUMAN_SCORE, 13, 221);
    Marvin_LABEL_DASHBOARD_HUMAN_SCORE->fn->setSize(Marvin_LABEL_DASHBOARD_HUMAN_SCORE, 120, 16);
    Marvin_LABEL_DASHBOARD_HUMAN_SCORE->fn->setScheme(Marvin_LABEL_DASHBOARD_HUMAN_SCORE, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_DASHBOARD_HUMAN_SCORE->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_HUMAN_SCORE, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_HUMAN_SCORE->fn->setVAlignment(Marvin_LABEL_DASHBOARD_HUMAN_SCORE, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_HUMAN_SCORE->fn->setMargins(Marvin_LABEL_DASHBOARD_HUMAN_SCORE, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_HUMAN_SCORE->fn->setString(Marvin_LABEL_DASHBOARD_HUMAN_SCORE, (leString*)&string_PLAYER_SCORE);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_LABEL_DASHBOARD_HUMAN_SCORE);

    Marvin_BUTTON_DASHBOARD_HUMAN_1X = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_HUMAN_1X->fn->setPosition(Marvin_BUTTON_DASHBOARD_HUMAN_1X, 142, 221);
    Marvin_BUTTON_DASHBOARD_HUMAN_1X->fn->setSize(Marvin_BUTTON_DASHBOARD_HUMAN_1X, 23, 16);
    Marvin_BUTTON_DASHBOARD_HUMAN_1X->fn->setScheme(Marvin_BUTTON_DASHBOARD_HUMAN_1X, &SCHEME_FILL_YELLOW_400);
    Marvin_BUTTON_DASHBOARD_HUMAN_1X->fn->setBorderType(Marvin_BUTTON_DASHBOARD_HUMAN_1X, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_DASHBOARD_HUMAN_1X->fn->setVAlignment(Marvin_BUTTON_DASHBOARD_HUMAN_1X, LE_VALIGN_TOP);
    Marvin_BUTTON_DASHBOARD_HUMAN_1X->fn->setMargins(Marvin_BUTTON_DASHBOARD_HUMAN_1X, 4, 0, 4, 4);
    Marvin_BUTTON_DASHBOARD_HUMAN_1X->fn->setToggleable(Marvin_BUTTON_DASHBOARD_HUMAN_1X, LE_TRUE);
    Marvin_BUTTON_DASHBOARD_HUMAN_1X->fn->setPressed(Marvin_BUTTON_DASHBOARD_HUMAN_1X, LE_TRUE);
    Marvin_BUTTON_DASHBOARD_HUMAN_1X->fn->setString(Marvin_BUTTON_DASHBOARD_HUMAN_1X, (leString*)&string_PLAYER_MULTIPLIER_1x);
    Marvin_BUTTON_DASHBOARD_HUMAN_1X->fn->setPressedOffset(Marvin_BUTTON_DASHBOARD_HUMAN_1X, 0);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_BUTTON_DASHBOARD_HUMAN_1X);

    Marvin_BUTTON_DASHBOARD_HUMAN_2X = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_HUMAN_2X->fn->setPosition(Marvin_BUTTON_DASHBOARD_HUMAN_2X, 169, 221);
    Marvin_BUTTON_DASHBOARD_HUMAN_2X->fn->setSize(Marvin_BUTTON_DASHBOARD_HUMAN_2X, 23, 16);
    Marvin_BUTTON_DASHBOARD_HUMAN_2X->fn->setScheme(Marvin_BUTTON_DASHBOARD_HUMAN_2X, &SCHEME_FILL_YELLOW_400);
    Marvin_BUTTON_DASHBOARD_HUMAN_2X->fn->setBorderType(Marvin_BUTTON_DASHBOARD_HUMAN_2X, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_DASHBOARD_HUMAN_2X->fn->setVAlignment(Marvin_BUTTON_DASHBOARD_HUMAN_2X, LE_VALIGN_TOP);
    Marvin_BUTTON_DASHBOARD_HUMAN_2X->fn->setMargins(Marvin_BUTTON_DASHBOARD_HUMAN_2X, 4, 0, 4, 4);
    Marvin_BUTTON_DASHBOARD_HUMAN_2X->fn->setToggleable(Marvin_BUTTON_DASHBOARD_HUMAN_2X, LE_TRUE);
    Marvin_BUTTON_DASHBOARD_HUMAN_2X->fn->setString(Marvin_BUTTON_DASHBOARD_HUMAN_2X, (leString*)&string_PLAYER_MULTIPLIER_2x);
    Marvin_BUTTON_DASHBOARD_HUMAN_2X->fn->setPressedOffset(Marvin_BUTTON_DASHBOARD_HUMAN_2X, 0);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_BUTTON_DASHBOARD_HUMAN_2X);

    Marvin_BUTTON_DASHBOARD_HUMAN_3X = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_HUMAN_3X->fn->setPosition(Marvin_BUTTON_DASHBOARD_HUMAN_3X, 196, 221);
    Marvin_BUTTON_DASHBOARD_HUMAN_3X->fn->setSize(Marvin_BUTTON_DASHBOARD_HUMAN_3X, 23, 16);
    Marvin_BUTTON_DASHBOARD_HUMAN_3X->fn->setScheme(Marvin_BUTTON_DASHBOARD_HUMAN_3X, &SCHEME_FILL_YELLOW_400);
    Marvin_BUTTON_DASHBOARD_HUMAN_3X->fn->setBorderType(Marvin_BUTTON_DASHBOARD_HUMAN_3X, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_DASHBOARD_HUMAN_3X->fn->setVAlignment(Marvin_BUTTON_DASHBOARD_HUMAN_3X, LE_VALIGN_TOP);
    Marvin_BUTTON_DASHBOARD_HUMAN_3X->fn->setMargins(Marvin_BUTTON_DASHBOARD_HUMAN_3X, 4, 0, 4, 4);
    Marvin_BUTTON_DASHBOARD_HUMAN_3X->fn->setToggleable(Marvin_BUTTON_DASHBOARD_HUMAN_3X, LE_TRUE);
    Marvin_BUTTON_DASHBOARD_HUMAN_3X->fn->setString(Marvin_BUTTON_DASHBOARD_HUMAN_3X, (leString*)&string_PLAYER_MULTIPLIER_3x);
    Marvin_BUTTON_DASHBOARD_HUMAN_3X->fn->setPressedOffset(Marvin_BUTTON_DASHBOARD_HUMAN_3X, 0);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_BUTTON_DASHBOARD_HUMAN_3X);

    Marvin_BUTTON_DASHBOARD_HUMAN_4X = leButtonWidget_New();
    Marvin_BUTTON_DASHBOARD_HUMAN_4X->fn->setPosition(Marvin_BUTTON_DASHBOARD_HUMAN_4X, 223, 221);
    Marvin_BUTTON_DASHBOARD_HUMAN_4X->fn->setSize(Marvin_BUTTON_DASHBOARD_HUMAN_4X, 23, 16);
    Marvin_BUTTON_DASHBOARD_HUMAN_4X->fn->setScheme(Marvin_BUTTON_DASHBOARD_HUMAN_4X, &SCHEME_FILL_YELLOW_400);
    Marvin_BUTTON_DASHBOARD_HUMAN_4X->fn->setBorderType(Marvin_BUTTON_DASHBOARD_HUMAN_4X, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_DASHBOARD_HUMAN_4X->fn->setVAlignment(Marvin_BUTTON_DASHBOARD_HUMAN_4X, LE_VALIGN_TOP);
    Marvin_BUTTON_DASHBOARD_HUMAN_4X->fn->setMargins(Marvin_BUTTON_DASHBOARD_HUMAN_4X, 4, 0, 4, 4);
    Marvin_BUTTON_DASHBOARD_HUMAN_4X->fn->setToggleable(Marvin_BUTTON_DASHBOARD_HUMAN_4X, LE_TRUE);
    Marvin_BUTTON_DASHBOARD_HUMAN_4X->fn->setString(Marvin_BUTTON_DASHBOARD_HUMAN_4X, (leString*)&string_PLAYER_MULTIPLIER_4x);
    Marvin_BUTTON_DASHBOARD_HUMAN_4X->fn->setPressedOffset(Marvin_BUTTON_DASHBOARD_HUMAN_4X, 0);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_BUTTON_DASHBOARD_HUMAN_4X);

    Marvin_LABEL_DASHBOARD_HUMAN_Score = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_HUMAN_Score->fn->setPosition(Marvin_LABEL_DASHBOARD_HUMAN_Score, 13, 241);
    Marvin_LABEL_DASHBOARD_HUMAN_Score->fn->setSize(Marvin_LABEL_DASHBOARD_HUMAN_Score, 230, 32);
    Marvin_LABEL_DASHBOARD_HUMAN_Score->fn->setScheme(Marvin_LABEL_DASHBOARD_HUMAN_Score, &SCHEME_TEXT_HUMAN);
    Marvin_LABEL_DASHBOARD_HUMAN_Score->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_HUMAN_Score, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_HUMAN_Score->fn->setVAlignment(Marvin_LABEL_DASHBOARD_HUMAN_Score, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_HUMAN_Score->fn->setMargins(Marvin_LABEL_DASHBOARD_HUMAN_Score, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_HUMAN_Score->fn->setString(Marvin_LABEL_DASHBOARD_HUMAN_Score, (leString*)&string_PLAYER_ROBOT_Score);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_LABEL_DASHBOARD_HUMAN_Score);

    Marvin_LABEL_DASHBOARD_HUMAN_STREAK = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_HUMAN_STREAK->fn->setPosition(Marvin_LABEL_DASHBOARD_HUMAN_STREAK, 13, 283);
    Marvin_LABEL_DASHBOARD_HUMAN_STREAK->fn->setSize(Marvin_LABEL_DASHBOARD_HUMAN_STREAK, 60, 16);
    Marvin_LABEL_DASHBOARD_HUMAN_STREAK->fn->setScheme(Marvin_LABEL_DASHBOARD_HUMAN_STREAK, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_DASHBOARD_HUMAN_STREAK->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_HUMAN_STREAK, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_HUMAN_STREAK->fn->setVAlignment(Marvin_LABEL_DASHBOARD_HUMAN_STREAK, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_HUMAN_STREAK->fn->setMargins(Marvin_LABEL_DASHBOARD_HUMAN_STREAK, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_HUMAN_STREAK->fn->setString(Marvin_LABEL_DASHBOARD_HUMAN_STREAK, (leString*)&string_PLAYER_STREAK);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_LABEL_DASHBOARD_HUMAN_STREAK);

    Marvin_LABEL_DASHBOARD_HUMAN_Streak = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_HUMAN_Streak->fn->setPosition(Marvin_LABEL_DASHBOARD_HUMAN_Streak, 116, 283);
    Marvin_LABEL_DASHBOARD_HUMAN_Streak->fn->setSize(Marvin_LABEL_DASHBOARD_HUMAN_Streak, 8, 16);
    Marvin_LABEL_DASHBOARD_HUMAN_Streak->fn->setScheme(Marvin_LABEL_DASHBOARD_HUMAN_Streak, &SCHEME_TEXT_ZINC_300);
    Marvin_LABEL_DASHBOARD_HUMAN_Streak->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_HUMAN_Streak, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_HUMAN_Streak->fn->setVAlignment(Marvin_LABEL_DASHBOARD_HUMAN_Streak, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_HUMAN_Streak->fn->setMargins(Marvin_LABEL_DASHBOARD_HUMAN_Streak, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_HUMAN_Streak->fn->setString(Marvin_LABEL_DASHBOARD_HUMAN_Streak, (leString*)&string_figmaStr__0_0);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_LABEL_DASHBOARD_HUMAN_Streak);

    Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY->fn->setPosition(Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY, 132, 283);
    Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY->fn->setSize(Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY, 70, 16);
    Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY->fn->setScheme(Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY->fn->setVAlignment(Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY->fn->setMargins(Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY->fn->setString(Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY, (leString*)&string_PLAYER_ACCURACY);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY);

    Marvin_LABEL_DASHBOARD_HUMAN_Accuracy = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_HUMAN_Accuracy->fn->setPosition(Marvin_LABEL_DASHBOARD_HUMAN_Accuracy, 235, 283);
    Marvin_LABEL_DASHBOARD_HUMAN_Accuracy->fn->setSize(Marvin_LABEL_DASHBOARD_HUMAN_Accuracy, 8, 16);
    Marvin_LABEL_DASHBOARD_HUMAN_Accuracy->fn->setScheme(Marvin_LABEL_DASHBOARD_HUMAN_Accuracy, &SCHEME_TEXT_ZINC_300);
    Marvin_LABEL_DASHBOARD_HUMAN_Accuracy->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_HUMAN_Accuracy, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_HUMAN_Accuracy->fn->setVAlignment(Marvin_LABEL_DASHBOARD_HUMAN_Accuracy, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_HUMAN_Accuracy->fn->setMargins(Marvin_LABEL_DASHBOARD_HUMAN_Accuracy, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_HUMAN_Accuracy->fn->setString(Marvin_LABEL_DASHBOARD_HUMAN_Accuracy, (leString*)&string_figmaStr__);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_LABEL_DASHBOARD_HUMAN_Accuracy);

    Marvin_PROGRESSBAR_DASHBOARD_HUMAN_Accuracy = leProgressBarWidget_New();
    Marvin_PROGRESSBAR_DASHBOARD_HUMAN_Accuracy->fn->setPosition(Marvin_PROGRESSBAR_DASHBOARD_HUMAN_Accuracy, 132, 303);
    Marvin_PROGRESSBAR_DASHBOARD_HUMAN_Accuracy->fn->setSize(Marvin_PROGRESSBAR_DASHBOARD_HUMAN_Accuracy, 111, 6);
    Marvin_PROGRESSBAR_DASHBOARD_HUMAN_Accuracy->fn->setScheme(Marvin_PROGRESSBAR_DASHBOARD_HUMAN_Accuracy, &SCHEME_FILL_ZINC_800);
    Marvin_PROGRESSBAR_DASHBOARD_HUMAN_Accuracy->fn->setBorderType(Marvin_PROGRESSBAR_DASHBOARD_HUMAN_Accuracy, LE_WIDGET_BORDER_NONE);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_PROGRESSBAR_DASHBOARD_HUMAN_Accuracy);

    Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER->fn->setPosition(Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER, 13, 319);
    Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER->fn->setSize(Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER, 87, 16);
    Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER->fn->setScheme(Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER->fn->setVAlignment(Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER->fn->setMargins(Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER->fn->setString(Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER, (leString*)&string_PLAYER_STAR_POWER);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER);

    Marvin_LABEL_DASHBOARD_HUMAN_StarPower = leLabelWidget_New();
    Marvin_LABEL_DASHBOARD_HUMAN_StarPower->fn->setPosition(Marvin_LABEL_DASHBOARD_HUMAN_StarPower, 235, 319);
    Marvin_LABEL_DASHBOARD_HUMAN_StarPower->fn->setSize(Marvin_LABEL_DASHBOARD_HUMAN_StarPower, 8, 16);
    Marvin_LABEL_DASHBOARD_HUMAN_StarPower->fn->setScheme(Marvin_LABEL_DASHBOARD_HUMAN_StarPower, &SCHEME_TEXT_ZINC_300);
    Marvin_LABEL_DASHBOARD_HUMAN_StarPower->fn->setBackgroundType(Marvin_LABEL_DASHBOARD_HUMAN_StarPower, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_DASHBOARD_HUMAN_StarPower->fn->setVAlignment(Marvin_LABEL_DASHBOARD_HUMAN_StarPower, LE_VALIGN_TOP);
    Marvin_LABEL_DASHBOARD_HUMAN_StarPower->fn->setMargins(Marvin_LABEL_DASHBOARD_HUMAN_StarPower, 0, 0, 0, 0);
    Marvin_LABEL_DASHBOARD_HUMAN_StarPower->fn->setString(Marvin_LABEL_DASHBOARD_HUMAN_StarPower, (leString*)&string_figmaStr___0);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_LABEL_DASHBOARD_HUMAN_StarPower);

    Marvin_PROGRESSBAR_DASHBOARD_HUMAN_StarPower = leProgressBarWidget_New();
    Marvin_PROGRESSBAR_DASHBOARD_HUMAN_StarPower->fn->setPosition(Marvin_PROGRESSBAR_DASHBOARD_HUMAN_StarPower, 13, 339);
    Marvin_PROGRESSBAR_DASHBOARD_HUMAN_StarPower->fn->setSize(Marvin_PROGRESSBAR_DASHBOARD_HUMAN_StarPower, 230, 8);
    Marvin_PROGRESSBAR_DASHBOARD_HUMAN_StarPower->fn->setScheme(Marvin_PROGRESSBAR_DASHBOARD_HUMAN_StarPower, &SCHEME_FILL_ZINC_800);
    Marvin_PROGRESSBAR_DASHBOARD_HUMAN_StarPower->fn->setBorderType(Marvin_PROGRESSBAR_DASHBOARD_HUMAN_StarPower, LE_WIDGET_BORDER_NONE);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_PROGRESSBAR_DASHBOARD_HUMAN_StarPower);

    Marvin_PANEL_DASHBOARD_HUMAN_DIVIDER_1 = leWidget_New();
    Marvin_PANEL_DASHBOARD_HUMAN_DIVIDER_1->fn->setPosition(Marvin_PANEL_DASHBOARD_HUMAN_DIVIDER_1, 13, 357);
    Marvin_PANEL_DASHBOARD_HUMAN_DIVIDER_1->fn->setSize(Marvin_PANEL_DASHBOARD_HUMAN_DIVIDER_1, 230, 1);
    Marvin_PANEL_DASHBOARD_HUMAN_DIVIDER_1->fn->setScheme(Marvin_PANEL_DASHBOARD_HUMAN_DIVIDER_1, &SCHEME_FILL_ZINC_800);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_PANEL_DASHBOARD_HUMAN_DIVIDER_1);

    Marvin_panel_Container_98_0 = leWidget_New();
    Marvin_panel_Container_98_0->fn->setPosition(Marvin_panel_Container_98_0, 13, 368);
    Marvin_panel_Container_98_0->fn->setSize(Marvin_panel_Container_98_0, 230, 82);
    Marvin_panel_Container_98_0->fn->setScheme(Marvin_panel_Container_98_0, &SCHEME_BACKGROUND);
    Marvin_panel_Container_98_0->fn->setBackgroundType(Marvin_panel_Container_98_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_panel_Container_98_0);

    Marvin_panel_Paragraph_13_0 = leWidget_New();
    Marvin_panel_Paragraph_13_0->fn->setPosition(Marvin_panel_Paragraph_13_0, 0, 0);
    Marvin_panel_Paragraph_13_0->fn->setSize(Marvin_panel_Paragraph_13_0, 230, 16);
    Marvin_panel_Paragraph_13_0->fn->setScheme(Marvin_panel_Paragraph_13_0, &SCHEME_BACKGROUND);
    Marvin_panel_Paragraph_13_0->fn->setBackgroundType(Marvin_panel_Paragraph_13_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_panel_Container_98_0->fn->addChild(Marvin_panel_Container_98_0, (leWidget*)Marvin_panel_Paragraph_13_0);

    Marvin_label_CONTROLLER_0 = leLabelWidget_New();
    Marvin_label_CONTROLLER_0->fn->setPosition(Marvin_label_CONTROLLER_0, 0, 0);
    Marvin_label_CONTROLLER_0->fn->setSize(Marvin_label_CONTROLLER_0, 100, 16);
    Marvin_label_CONTROLLER_0->fn->setScheme(Marvin_label_CONTROLLER_0, &SCHEME_TEXT_ZINC_500);
    Marvin_label_CONTROLLER_0->fn->setBackgroundType(Marvin_label_CONTROLLER_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_label_CONTROLLER_0->fn->setVAlignment(Marvin_label_CONTROLLER_0, LE_VALIGN_TOP);
    Marvin_label_CONTROLLER_0->fn->setMargins(Marvin_label_CONTROLLER_0, 0, 0, 0, 0);
    Marvin_label_CONTROLLER_0->fn->setString(Marvin_label_CONTROLLER_0, (leString*)&string_figmaStr_CONTROLLER);
    Marvin_panel_Paragraph_13_0->fn->addChild(Marvin_panel_Paragraph_13_0, (leWidget*)Marvin_label_CONTROLLER_0);

    Marvin_panel_Container_99_0 = leWidget_New();
    Marvin_panel_Container_99_0->fn->setPosition(Marvin_panel_Container_99_0, 0, 16);
    Marvin_panel_Container_99_0->fn->setSize(Marvin_panel_Container_99_0, 230, 66);
    Marvin_panel_Container_99_0->fn->setScheme(Marvin_panel_Container_99_0, &SCHEME_BACKGROUND);
    Marvin_panel_Container_99_0->fn->setBackgroundType(Marvin_panel_Container_99_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_panel_Container_98_0->fn->addChild(Marvin_panel_Container_98_0, (leWidget*)Marvin_panel_Container_99_0);

    Marvin_panel_Container_100_0 = leWidget_New();
    Marvin_panel_Container_100_0->fn->setPosition(Marvin_panel_Container_100_0, 0, 6);
    Marvin_panel_Container_100_0->fn->setSize(Marvin_panel_Container_100_0, 230, 16);
    Marvin_panel_Container_100_0->fn->setScheme(Marvin_panel_Container_100_0, &SCHEME_BACKGROUND);
    Marvin_panel_Container_100_0->fn->setBackgroundType(Marvin_panel_Container_100_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_panel_Container_99_0->fn->addChild(Marvin_panel_Container_99_0, (leWidget*)Marvin_panel_Container_100_0);

    Marvin_panel_Text_32_0 = leWidget_New();
    Marvin_panel_Text_32_0->fn->setPosition(Marvin_panel_Text_32_0, 0, 0);
    Marvin_panel_Text_32_0->fn->setSize(Marvin_panel_Text_32_0, 73, 16);
    Marvin_panel_Text_32_0->fn->setScheme(Marvin_panel_Text_32_0, &SCHEME_BACKGROUND);
    Marvin_panel_Text_32_0->fn->setBackgroundType(Marvin_panel_Text_32_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_panel_Container_100_0->fn->addChild(Marvin_panel_Container_100_0, (leWidget*)Marvin_panel_Text_32_0);

    Marvin_label_Wii_guitar_0 = leLabelWidget_New();
    Marvin_label_Wii_guitar_0->fn->setPosition(Marvin_label_Wii_guitar_0, 0, 0);
    Marvin_label_Wii_guitar_0->fn->setSize(Marvin_label_Wii_guitar_0, 73, 16);
    Marvin_label_Wii_guitar_0->fn->setScheme(Marvin_label_Wii_guitar_0, &SCHEME_TEXT_ZINC_600);
    Marvin_label_Wii_guitar_0->fn->setBackgroundType(Marvin_label_Wii_guitar_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_label_Wii_guitar_0->fn->setVAlignment(Marvin_label_Wii_guitar_0, LE_VALIGN_TOP);
    Marvin_label_Wii_guitar_0->fn->setMargins(Marvin_label_Wii_guitar_0, 0, 0, 0, 0);
    Marvin_label_Wii_guitar_0->fn->setString(Marvin_label_Wii_guitar_0, (leString*)&string_figmaStr_Wii_guitar);
    Marvin_panel_Text_32_0->fn->addChild(Marvin_panel_Text_32_0, (leWidget*)Marvin_label_Wii_guitar_0);

    Marvin_panel_Text_33_0 = leWidget_New();
    Marvin_panel_Text_33_0->fn->setPosition(Marvin_panel_Text_33_0, 164, 0);
    Marvin_panel_Text_33_0->fn->setSize(Marvin_panel_Text_33_0, 66, 16);
    Marvin_panel_Text_33_0->fn->setScheme(Marvin_panel_Text_33_0, &SCHEME_BACKGROUND);
    Marvin_panel_Text_33_0->fn->setBackgroundType(Marvin_panel_Text_33_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_panel_Container_100_0->fn->addChild(Marvin_panel_Container_100_0, (leWidget*)Marvin_panel_Text_33_0);

    Marvin_label_Connected_1 = leLabelWidget_New();
    Marvin_label_Connected_1->fn->setPosition(Marvin_label_Connected_1, 0, 0);
    Marvin_label_Connected_1->fn->setSize(Marvin_label_Connected_1, 66, 16);
    Marvin_label_Connected_1->fn->setScheme(Marvin_label_Connected_1, &SCHEME_TEXT_GREEN_400);
    Marvin_label_Connected_1->fn->setBackgroundType(Marvin_label_Connected_1, LE_WIDGET_BACKGROUND_NONE);
    Marvin_label_Connected_1->fn->setVAlignment(Marvin_label_Connected_1, LE_VALIGN_TOP);
    Marvin_label_Connected_1->fn->setMargins(Marvin_label_Connected_1, 0, 0, 0, 0);
    Marvin_label_Connected_1->fn->setString(Marvin_label_Connected_1, (leString*)&string_figmaStr_Connected_0);
    Marvin_panel_Text_33_0->fn->addChild(Marvin_panel_Text_33_0, (leWidget*)Marvin_label_Connected_1);

    Marvin_panel_Container_101_0 = leWidget_New();
    Marvin_panel_Container_101_0->fn->setPosition(Marvin_panel_Container_101_0, 0, 22);
    Marvin_panel_Container_101_0->fn->setSize(Marvin_panel_Container_101_0, 230, 22);
    Marvin_panel_Container_101_0->fn->setScheme(Marvin_panel_Container_101_0, &SCHEME_BACKGROUND);
    Marvin_panel_Container_101_0->fn->setBackgroundType(Marvin_panel_Container_101_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_panel_Container_99_0->fn->addChild(Marvin_panel_Container_99_0, (leWidget*)Marvin_panel_Container_101_0);

    Marvin_panel_Text_34_0 = leWidget_New();
    Marvin_panel_Text_34_0->fn->setPosition(Marvin_panel_Text_34_0, 0, 6);
    Marvin_panel_Text_34_0->fn->setSize(Marvin_panel_Text_34_0, 73, 16);
    Marvin_panel_Text_34_0->fn->setScheme(Marvin_panel_Text_34_0, &SCHEME_BACKGROUND);
    Marvin_panel_Text_34_0->fn->setBackgroundType(Marvin_panel_Text_34_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_panel_Container_101_0->fn->addChild(Marvin_panel_Container_101_0, (leWidget*)Marvin_panel_Text_34_0);

    Marvin_label_Wii_remote_0 = leLabelWidget_New();
    Marvin_label_Wii_remote_0->fn->setPosition(Marvin_label_Wii_remote_0, 0, 0);
    Marvin_label_Wii_remote_0->fn->setSize(Marvin_label_Wii_remote_0, 73, 16);
    Marvin_label_Wii_remote_0->fn->setScheme(Marvin_label_Wii_remote_0, &SCHEME_TEXT_ZINC_600);
    Marvin_label_Wii_remote_0->fn->setBackgroundType(Marvin_label_Wii_remote_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_label_Wii_remote_0->fn->setVAlignment(Marvin_label_Wii_remote_0, LE_VALIGN_TOP);
    Marvin_label_Wii_remote_0->fn->setMargins(Marvin_label_Wii_remote_0, 0, 0, 0, 0);
    Marvin_label_Wii_remote_0->fn->setString(Marvin_label_Wii_remote_0, (leString*)&string_figmaStr_Wii_remote);
    Marvin_panel_Text_34_0->fn->addChild(Marvin_panel_Text_34_0, (leWidget*)Marvin_label_Wii_remote_0);

    Marvin_panel_Text_35_0 = leWidget_New();
    Marvin_panel_Text_35_0->fn->setPosition(Marvin_panel_Text_35_0, 164, 6);
    Marvin_panel_Text_35_0->fn->setSize(Marvin_panel_Text_35_0, 66, 16);
    Marvin_panel_Text_35_0->fn->setScheme(Marvin_panel_Text_35_0, &SCHEME_BACKGROUND);
    Marvin_panel_Text_35_0->fn->setBackgroundType(Marvin_panel_Text_35_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_panel_Container_101_0->fn->addChild(Marvin_panel_Container_101_0, (leWidget*)Marvin_panel_Text_35_0);

    Marvin_label_Connected_0_0 = leLabelWidget_New();
    Marvin_label_Connected_0_0->fn->setPosition(Marvin_label_Connected_0_0, 0, 0);
    Marvin_label_Connected_0_0->fn->setSize(Marvin_label_Connected_0_0, 66, 16);
    Marvin_label_Connected_0_0->fn->setScheme(Marvin_label_Connected_0_0, &SCHEME_TEXT_GREEN_400);
    Marvin_label_Connected_0_0->fn->setBackgroundType(Marvin_label_Connected_0_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_label_Connected_0_0->fn->setVAlignment(Marvin_label_Connected_0_0, LE_VALIGN_TOP);
    Marvin_label_Connected_0_0->fn->setMargins(Marvin_label_Connected_0_0, 0, 0, 0, 0);
    Marvin_label_Connected_0_0->fn->setString(Marvin_label_Connected_0_0, (leString*)&string_figmaStr_Connected_0_0);
    Marvin_panel_Text_35_0->fn->addChild(Marvin_panel_Text_35_0, (leWidget*)Marvin_label_Connected_0_0);

    Marvin_panel_Container_102_0 = leWidget_New();
    Marvin_panel_Container_102_0->fn->setPosition(Marvin_panel_Container_102_0, 0, 44);
    Marvin_panel_Container_102_0->fn->setSize(Marvin_panel_Container_102_0, 230, 22);
    Marvin_panel_Container_102_0->fn->setScheme(Marvin_panel_Container_102_0, &SCHEME_BACKGROUND);
    Marvin_panel_Container_102_0->fn->setBackgroundType(Marvin_panel_Container_102_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_panel_Container_99_0->fn->addChild(Marvin_panel_Container_99_0, (leWidget*)Marvin_panel_Container_102_0);

    Marvin_panel_Text_36_0 = leWidget_New();
    Marvin_panel_Text_36_0->fn->setPosition(Marvin_panel_Text_36_0, 0, 6);
    Marvin_panel_Text_36_0->fn->setSize(Marvin_panel_Text_36_0, 51, 16);
    Marvin_panel_Text_36_0->fn->setScheme(Marvin_panel_Text_36_0, &SCHEME_BACKGROUND);
    Marvin_panel_Text_36_0->fn->setBackgroundType(Marvin_panel_Text_36_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_panel_Container_102_0->fn->addChild(Marvin_panel_Container_102_0, (leWidget*)Marvin_panel_Text_36_0);

    Marvin_label_Battery_0 = leLabelWidget_New();
    Marvin_label_Battery_0->fn->setPosition(Marvin_label_Battery_0, 0, 0);
    Marvin_label_Battery_0->fn->setSize(Marvin_label_Battery_0, 51, 16);
    Marvin_label_Battery_0->fn->setScheme(Marvin_label_Battery_0, &SCHEME_TEXT_ZINC_600);
    Marvin_label_Battery_0->fn->setBackgroundType(Marvin_label_Battery_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_label_Battery_0->fn->setVAlignment(Marvin_label_Battery_0, LE_VALIGN_TOP);
    Marvin_label_Battery_0->fn->setMargins(Marvin_label_Battery_0, 0, 0, 0, 0);
    Marvin_label_Battery_0->fn->setString(Marvin_label_Battery_0, (leString*)&string_figmaStr_Battery);
    Marvin_panel_Text_36_0->fn->addChild(Marvin_panel_Text_36_0, (leWidget*)Marvin_label_Battery_0);

    Marvin_panel_Text_37_0 = leWidget_New();
    Marvin_panel_Text_37_0->fn->setPosition(Marvin_panel_Text_37_0, 208, 6);
    Marvin_panel_Text_37_0->fn->setSize(Marvin_panel_Text_37_0, 22, 16);
    Marvin_panel_Text_37_0->fn->setScheme(Marvin_panel_Text_37_0, &SCHEME_BACKGROUND);
    Marvin_panel_Text_37_0->fn->setBackgroundType(Marvin_panel_Text_37_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_panel_Container_102_0->fn->addChild(Marvin_panel_Container_102_0, (leWidget*)Marvin_panel_Text_37_0);

    Marvin_label__68__0 = leLabelWidget_New();
    Marvin_label__68__0->fn->setPosition(Marvin_label__68__0, -4, 0);
    Marvin_label__68__0->fn->setSize(Marvin_label__68__0, 26, 16);
    Marvin_label__68__0->fn->setScheme(Marvin_label__68__0, &SCHEME_TEXT_HUMAN);
    Marvin_label__68__0->fn->setBackgroundType(Marvin_label__68__0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_label__68__0->fn->setVAlignment(Marvin_label__68__0, LE_VALIGN_TOP);
    Marvin_label__68__0->fn->setMargins(Marvin_label__68__0, 0, 0, 0, 0);
    Marvin_label__68__0->fn->setString(Marvin_label__68__0, (leString*)&string_figmaStr_68_);
    Marvin_panel_Text_37_0->fn->addChild(Marvin_panel_Text_37_0, (leWidget*)Marvin_label__68__0);

    Marvin_PANEL_DASHBOARD_HUMAN_BORDER = leWidget_New();
    Marvin_PANEL_DASHBOARD_HUMAN_BORDER->fn->setPosition(Marvin_PANEL_DASHBOARD_HUMAN_BORDER, 0, 0);
    Marvin_PANEL_DASHBOARD_HUMAN_BORDER->fn->setSize(Marvin_PANEL_DASHBOARD_HUMAN_BORDER, 256, 716);
    Marvin_PANEL_DASHBOARD_HUMAN_BORDER->fn->setScheme(Marvin_PANEL_DASHBOARD_HUMAN_BORDER, &SCHEME_FILL_ZINC_900);
    Marvin_PANEL_DASHBOARD_HUMAN_BORDER->fn->setBackgroundType(Marvin_PANEL_DASHBOARD_HUMAN_BORDER, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_DASHBOARD_HUMAN_BORDER->fn->setBorderType(Marvin_PANEL_DASHBOARD_HUMAN_BORDER, LE_WIDGET_BORDER_LINE);
    Marvin_PANEL_DASHBOARD_HUMAN->fn->addChild(Marvin_PANEL_DASHBOARD_HUMAN, (leWidget*)Marvin_PANEL_DASHBOARD_HUMAN_BORDER);

    leAddRootWidget(root0, 0);
    leSetLayerColorMode(0, LE_COLOR_MODE_RGB_565);

    // layer 1
    root1 = leWidget_New();
    root1->fn->setSize(root1, 320, 800);
    root1->fn->setBackgroundType(root1, LE_WIDGET_BACKGROUND_NONE);
    root1->fn->setMargins(root1, 0, 0, 0, 0);
    root1->flags |= LE_WIDGET_IGNOREEVENTS;
    root1->flags |= LE_WIDGET_IGNOREPICK;

    Marvin_PANEL_NAVIGATION = leWidget_New();
    Marvin_PANEL_NAVIGATION->fn->setPosition(Marvin_PANEL_NAVIGATION, 0, 0);
    Marvin_PANEL_NAVIGATION->fn->setSize(Marvin_PANEL_NAVIGATION, 320, 800);
    Marvin_PANEL_NAVIGATION->fn->setEnabled(Marvin_PANEL_NAVIGATION, LE_FALSE);
    Marvin_PANEL_NAVIGATION->fn->setVisible(Marvin_PANEL_NAVIGATION, LE_FALSE);
    Marvin_PANEL_NAVIGATION->fn->setScheme(Marvin_PANEL_NAVIGATION, &SCHEME_NAV_BUTTON_UNSELECTED);
    Marvin_PANEL_NAVIGATION->fn->setBorderType(Marvin_PANEL_NAVIGATION, LE_WIDGET_BORDER_LINE);
    root1->fn->addChild(root1, (leWidget*)Marvin_PANEL_NAVIGATION);

    Marvin_PANEL_NAVIGATION_TOP = leWidget_New();
    Marvin_PANEL_NAVIGATION_TOP->fn->setPosition(Marvin_PANEL_NAVIGATION_TOP, 0, 0);
    Marvin_PANEL_NAVIGATION_TOP->fn->setSize(Marvin_PANEL_NAVIGATION_TOP, 319, 97);
    Marvin_PANEL_NAVIGATION_TOP->fn->setScheme(Marvin_PANEL_NAVIGATION_TOP, &SCHEME_FILL_ZINC_900);
    Marvin_PANEL_NAVIGATION_TOP->fn->setBackgroundType(Marvin_PANEL_NAVIGATION_TOP, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_NAVIGATION_TOP->fn->setBorderType(Marvin_PANEL_NAVIGATION_TOP, LE_WIDGET_BORDER_LINE);
    Marvin_PANEL_NAVIGATION->fn->addChild(Marvin_PANEL_NAVIGATION, (leWidget*)Marvin_PANEL_NAVIGATION_TOP);

    Marvin_LABEL_NAVIGATION = leLabelWidget_New();
    Marvin_LABEL_NAVIGATION->fn->setPosition(Marvin_LABEL_NAVIGATION, 24, 24);
    Marvin_LABEL_NAVIGATION->fn->setSize(Marvin_LABEL_NAVIGATION, 271, 28);
    Marvin_LABEL_NAVIGATION->fn->setScheme(Marvin_LABEL_NAVIGATION, &SCHEME_TEXT_WHITE);
    Marvin_LABEL_NAVIGATION->fn->setBackgroundType(Marvin_LABEL_NAVIGATION, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_NAVIGATION->fn->setVAlignment(Marvin_LABEL_NAVIGATION, LE_VALIGN_TOP);
    Marvin_LABEL_NAVIGATION->fn->setMargins(Marvin_LABEL_NAVIGATION, 0, 0, 0, 0);
    Marvin_LABEL_NAVIGATION->fn->setString(Marvin_LABEL_NAVIGATION, (leString*)&string_NAV_NAVIGATION);
    Marvin_PANEL_NAVIGATION_TOP->fn->addChild(Marvin_PANEL_NAVIGATION_TOP, (leWidget*)Marvin_LABEL_NAVIGATION);

    Marvin_LABEL_NAV_SUB_HEADING = leLabelWidget_New();
    Marvin_LABEL_NAV_SUB_HEADING->fn->setPosition(Marvin_LABEL_NAV_SUB_HEADING, 24, 56);
    Marvin_LABEL_NAV_SUB_HEADING->fn->setSize(Marvin_LABEL_NAV_SUB_HEADING, 294, 16);
    Marvin_LABEL_NAV_SUB_HEADING->fn->setScheme(Marvin_LABEL_NAV_SUB_HEADING, &SCHEME_TEXT_ZINC_200);
    Marvin_LABEL_NAV_SUB_HEADING->fn->setBackgroundType(Marvin_LABEL_NAV_SUB_HEADING, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_NAV_SUB_HEADING->fn->setVAlignment(Marvin_LABEL_NAV_SUB_HEADING, LE_VALIGN_TOP);
    Marvin_LABEL_NAV_SUB_HEADING->fn->setMargins(Marvin_LABEL_NAV_SUB_HEADING, 0, 0, 0, 0);
    Marvin_LABEL_NAV_SUB_HEADING->fn->setString(Marvin_LABEL_NAV_SUB_HEADING, (leString*)&string_NAV_Marvin_v1_0_0);
    Marvin_PANEL_NAVIGATION_TOP->fn->addChild(Marvin_PANEL_NAVIGATION_TOP, (leWidget*)Marvin_LABEL_NAV_SUB_HEADING);

    Marvin_PANEL_NAVIGATION_MIDDLE = leWidget_New();
    Marvin_PANEL_NAVIGATION_MIDDLE->fn->setPosition(Marvin_PANEL_NAVIGATION_MIDDLE, 0, 97);
    Marvin_PANEL_NAVIGATION_MIDDLE->fn->setSize(Marvin_PANEL_NAVIGATION_MIDDLE, 319, 617);
    Marvin_PANEL_NAVIGATION_MIDDLE->fn->setScheme(Marvin_PANEL_NAVIGATION_MIDDLE, &SCHEME_FILL_ZINC_900);
    Marvin_PANEL_NAVIGATION_MIDDLE->fn->setBackgroundType(Marvin_PANEL_NAVIGATION_MIDDLE, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_NAVIGATION->fn->addChild(Marvin_PANEL_NAVIGATION, (leWidget*)Marvin_PANEL_NAVIGATION_MIDDLE);

    Marvin_BUTTON_NAV_DASHBOARD = leButtonWidget_New();
    Marvin_BUTTON_NAV_DASHBOARD->fn->setPosition(Marvin_BUTTON_NAV_DASHBOARD, 16, 16);
    Marvin_BUTTON_NAV_DASHBOARD->fn->setSize(Marvin_BUTTON_NAV_DASHBOARD, 287, 56);
    Marvin_BUTTON_NAV_DASHBOARD->fn->setScheme(Marvin_BUTTON_NAV_DASHBOARD, &SCHEME_NAV_BUTTON_UNSELECTED);
    Marvin_BUTTON_NAV_DASHBOARD->fn->setBorderType(Marvin_BUTTON_NAV_DASHBOARD, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_NAV_DASHBOARD->fn->setHAlignment(Marvin_BUTTON_NAV_DASHBOARD, LE_HALIGN_LEFT);
    Marvin_BUTTON_NAV_DASHBOARD->fn->setMargins(Marvin_BUTTON_NAV_DASHBOARD, 16, 4, 4, 4);
    Marvin_BUTTON_NAV_DASHBOARD->fn->setString(Marvin_BUTTON_NAV_DASHBOARD, (leString*)&string_NAV_BUTTON_Dashboard);
    Marvin_BUTTON_NAV_DASHBOARD->fn->setPressedImage(Marvin_BUTTON_NAV_DASHBOARD, (leImage*)&figmaImg_Icon_11);
    Marvin_BUTTON_NAV_DASHBOARD->fn->setReleasedImage(Marvin_BUTTON_NAV_DASHBOARD, (leImage*)&figmaImg_Icon_11);
    Marvin_BUTTON_NAV_DASHBOARD->fn->setImageMargin(Marvin_BUTTON_NAV_DASHBOARD, 16);
    Marvin_BUTTON_NAV_DASHBOARD->fn->setPressedOffset(Marvin_BUTTON_NAV_DASHBOARD, 0);
    Marvin_PANEL_NAVIGATION_MIDDLE->fn->addChild(Marvin_PANEL_NAVIGATION_MIDDLE, (leWidget*)Marvin_BUTTON_NAV_DASHBOARD);

    Marvin_BUTTON_NAV_WIIMOTES = leButtonWidget_New();
    Marvin_BUTTON_NAV_WIIMOTES->fn->setPosition(Marvin_BUTTON_NAV_WIIMOTES, 16, 80);
    Marvin_BUTTON_NAV_WIIMOTES->fn->setSize(Marvin_BUTTON_NAV_WIIMOTES, 287, 56);
    Marvin_BUTTON_NAV_WIIMOTES->fn->setScheme(Marvin_BUTTON_NAV_WIIMOTES, &SCHEME_NAV_BUTTON_UNSELECTED);
    Marvin_BUTTON_NAV_WIIMOTES->fn->setBorderType(Marvin_BUTTON_NAV_WIIMOTES, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_NAV_WIIMOTES->fn->setHAlignment(Marvin_BUTTON_NAV_WIIMOTES, LE_HALIGN_LEFT);
    Marvin_BUTTON_NAV_WIIMOTES->fn->setMargins(Marvin_BUTTON_NAV_WIIMOTES, 16, 4, 4, 4);
    Marvin_BUTTON_NAV_WIIMOTES->fn->setString(Marvin_BUTTON_NAV_WIIMOTES, (leString*)&string_NAV_BUTTON_Wiimotes);
    Marvin_BUTTON_NAV_WIIMOTES->fn->setPressedImage(Marvin_BUTTON_NAV_WIIMOTES, (leImage*)&figmaImg_Icon_12);
    Marvin_BUTTON_NAV_WIIMOTES->fn->setReleasedImage(Marvin_BUTTON_NAV_WIIMOTES, (leImage*)&figmaImg_Icon_12);
    Marvin_BUTTON_NAV_WIIMOTES->fn->setImageMargin(Marvin_BUTTON_NAV_WIIMOTES, 16);
    Marvin_BUTTON_NAV_WIIMOTES->fn->setPressedOffset(Marvin_BUTTON_NAV_WIIMOTES, 0);
    Marvin_PANEL_NAVIGATION_MIDDLE->fn->addChild(Marvin_PANEL_NAVIGATION_MIDDLE, (leWidget*)Marvin_BUTTON_NAV_WIIMOTES);

    Marvin_BUTTON_NAV_LOGS = leButtonWidget_New();
    Marvin_BUTTON_NAV_LOGS->fn->setPosition(Marvin_BUTTON_NAV_LOGS, 16, 144);
    Marvin_BUTTON_NAV_LOGS->fn->setSize(Marvin_BUTTON_NAV_LOGS, 287, 56);
    Marvin_BUTTON_NAV_LOGS->fn->setScheme(Marvin_BUTTON_NAV_LOGS, &SCHEME_NAV_BUTTON_UNSELECTED);
    Marvin_BUTTON_NAV_LOGS->fn->setBorderType(Marvin_BUTTON_NAV_LOGS, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_NAV_LOGS->fn->setHAlignment(Marvin_BUTTON_NAV_LOGS, LE_HALIGN_LEFT);
    Marvin_BUTTON_NAV_LOGS->fn->setMargins(Marvin_BUTTON_NAV_LOGS, 16, 4, 4, 4);
    Marvin_BUTTON_NAV_LOGS->fn->setString(Marvin_BUTTON_NAV_LOGS, (leString*)&string_NAV_BUTTON_Activity_Logs);
    Marvin_BUTTON_NAV_LOGS->fn->setPressedImage(Marvin_BUTTON_NAV_LOGS, (leImage*)&figmaImg_Icon_12);
    Marvin_BUTTON_NAV_LOGS->fn->setReleasedImage(Marvin_BUTTON_NAV_LOGS, (leImage*)&figmaImg_Icon_12);
    Marvin_BUTTON_NAV_LOGS->fn->setImageMargin(Marvin_BUTTON_NAV_LOGS, 16);
    Marvin_BUTTON_NAV_LOGS->fn->setPressedOffset(Marvin_BUTTON_NAV_LOGS, 0);
    Marvin_PANEL_NAVIGATION_MIDDLE->fn->addChild(Marvin_PANEL_NAVIGATION_MIDDLE, (leWidget*)Marvin_BUTTON_NAV_LOGS);

    Marvin_BUTTON_NAV_PERFORMANCE = leButtonWidget_New();
    Marvin_BUTTON_NAV_PERFORMANCE->fn->setPosition(Marvin_BUTTON_NAV_PERFORMANCE, 16, 208);
    Marvin_BUTTON_NAV_PERFORMANCE->fn->setSize(Marvin_BUTTON_NAV_PERFORMANCE, 287, 56);
    Marvin_BUTTON_NAV_PERFORMANCE->fn->setScheme(Marvin_BUTTON_NAV_PERFORMANCE, &SCHEME_NAV_BUTTON_UNSELECTED);
    Marvin_BUTTON_NAV_PERFORMANCE->fn->setBorderType(Marvin_BUTTON_NAV_PERFORMANCE, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_NAV_PERFORMANCE->fn->setHAlignment(Marvin_BUTTON_NAV_PERFORMANCE, LE_HALIGN_LEFT);
    Marvin_BUTTON_NAV_PERFORMANCE->fn->setMargins(Marvin_BUTTON_NAV_PERFORMANCE, 16, 4, 4, 4);
    Marvin_BUTTON_NAV_PERFORMANCE->fn->setString(Marvin_BUTTON_NAV_PERFORMANCE, (leString*)&string_NAV_BUTTON_Performance);
    Marvin_BUTTON_NAV_PERFORMANCE->fn->setPressedImage(Marvin_BUTTON_NAV_PERFORMANCE, (leImage*)&figmaImg_Icon_13);
    Marvin_BUTTON_NAV_PERFORMANCE->fn->setReleasedImage(Marvin_BUTTON_NAV_PERFORMANCE, (leImage*)&figmaImg_Icon_13);
    Marvin_BUTTON_NAV_PERFORMANCE->fn->setImageMargin(Marvin_BUTTON_NAV_PERFORMANCE, 16);
    Marvin_BUTTON_NAV_PERFORMANCE->fn->setPressedOffset(Marvin_BUTTON_NAV_PERFORMANCE, 0);
    Marvin_PANEL_NAVIGATION_MIDDLE->fn->addChild(Marvin_PANEL_NAVIGATION_MIDDLE, (leWidget*)Marvin_BUTTON_NAV_PERFORMANCE);

    Marvin_BUTTON_NAV_SYSTEM_INFO = leButtonWidget_New();
    Marvin_BUTTON_NAV_SYSTEM_INFO->fn->setPosition(Marvin_BUTTON_NAV_SYSTEM_INFO, 16, 272);
    Marvin_BUTTON_NAV_SYSTEM_INFO->fn->setSize(Marvin_BUTTON_NAV_SYSTEM_INFO, 287, 56);
    Marvin_BUTTON_NAV_SYSTEM_INFO->fn->setScheme(Marvin_BUTTON_NAV_SYSTEM_INFO, &SCHEME_NAV_BUTTON_UNSELECTED);
    Marvin_BUTTON_NAV_SYSTEM_INFO->fn->setBorderType(Marvin_BUTTON_NAV_SYSTEM_INFO, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_NAV_SYSTEM_INFO->fn->setHAlignment(Marvin_BUTTON_NAV_SYSTEM_INFO, LE_HALIGN_LEFT);
    Marvin_BUTTON_NAV_SYSTEM_INFO->fn->setMargins(Marvin_BUTTON_NAV_SYSTEM_INFO, 16, 4, 4, 4);
    Marvin_BUTTON_NAV_SYSTEM_INFO->fn->setString(Marvin_BUTTON_NAV_SYSTEM_INFO, (leString*)&string_NAV_BUTTON_System_Info);
    Marvin_BUTTON_NAV_SYSTEM_INFO->fn->setPressedImage(Marvin_BUTTON_NAV_SYSTEM_INFO, (leImage*)&figmaImg_Icon_14);
    Marvin_BUTTON_NAV_SYSTEM_INFO->fn->setReleasedImage(Marvin_BUTTON_NAV_SYSTEM_INFO, (leImage*)&figmaImg_Icon_14);
    Marvin_BUTTON_NAV_SYSTEM_INFO->fn->setImageMargin(Marvin_BUTTON_NAV_SYSTEM_INFO, 16);
    Marvin_BUTTON_NAV_SYSTEM_INFO->fn->setPressedOffset(Marvin_BUTTON_NAV_SYSTEM_INFO, 0);
    Marvin_PANEL_NAVIGATION_MIDDLE->fn->addChild(Marvin_PANEL_NAVIGATION_MIDDLE, (leWidget*)Marvin_BUTTON_NAV_SYSTEM_INFO);

    Marvin_BUTTON_NAV_DIAGNOSTICS = leButtonWidget_New();
    Marvin_BUTTON_NAV_DIAGNOSTICS->fn->setPosition(Marvin_BUTTON_NAV_DIAGNOSTICS, 16, 336);
    Marvin_BUTTON_NAV_DIAGNOSTICS->fn->setSize(Marvin_BUTTON_NAV_DIAGNOSTICS, 287, 56);
    Marvin_BUTTON_NAV_DIAGNOSTICS->fn->setScheme(Marvin_BUTTON_NAV_DIAGNOSTICS, &SCHEME_NAV_BUTTON_UNSELECTED);
    Marvin_BUTTON_NAV_DIAGNOSTICS->fn->setBorderType(Marvin_BUTTON_NAV_DIAGNOSTICS, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_NAV_DIAGNOSTICS->fn->setHAlignment(Marvin_BUTTON_NAV_DIAGNOSTICS, LE_HALIGN_LEFT);
    Marvin_BUTTON_NAV_DIAGNOSTICS->fn->setMargins(Marvin_BUTTON_NAV_DIAGNOSTICS, 16, 4, 4, 4);
    Marvin_BUTTON_NAV_DIAGNOSTICS->fn->setString(Marvin_BUTTON_NAV_DIAGNOSTICS, (leString*)&string_NAV_BUTTON_Diagnostics);
    Marvin_BUTTON_NAV_DIAGNOSTICS->fn->setPressedImage(Marvin_BUTTON_NAV_DIAGNOSTICS, (leImage*)&figmaImg_Icon_15);
    Marvin_BUTTON_NAV_DIAGNOSTICS->fn->setReleasedImage(Marvin_BUTTON_NAV_DIAGNOSTICS, (leImage*)&figmaImg_Icon_15);
    Marvin_BUTTON_NAV_DIAGNOSTICS->fn->setImageMargin(Marvin_BUTTON_NAV_DIAGNOSTICS, 16);
    Marvin_BUTTON_NAV_DIAGNOSTICS->fn->setPressedOffset(Marvin_BUTTON_NAV_DIAGNOSTICS, 0);
    Marvin_PANEL_NAVIGATION_MIDDLE->fn->addChild(Marvin_PANEL_NAVIGATION_MIDDLE, (leWidget*)Marvin_BUTTON_NAV_DIAGNOSTICS);

    Marvin_BUTTON_NAV_SETTINGS = leButtonWidget_New();
    Marvin_BUTTON_NAV_SETTINGS->fn->setPosition(Marvin_BUTTON_NAV_SETTINGS, 16, 400);
    Marvin_BUTTON_NAV_SETTINGS->fn->setSize(Marvin_BUTTON_NAV_SETTINGS, 287, 56);
    Marvin_BUTTON_NAV_SETTINGS->fn->setScheme(Marvin_BUTTON_NAV_SETTINGS, &SCHEME_NAV_BUTTON_UNSELECTED);
    Marvin_BUTTON_NAV_SETTINGS->fn->setBorderType(Marvin_BUTTON_NAV_SETTINGS, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_NAV_SETTINGS->fn->setHAlignment(Marvin_BUTTON_NAV_SETTINGS, LE_HALIGN_LEFT);
    Marvin_BUTTON_NAV_SETTINGS->fn->setMargins(Marvin_BUTTON_NAV_SETTINGS, 16, 4, 4, 4);
    Marvin_BUTTON_NAV_SETTINGS->fn->setString(Marvin_BUTTON_NAV_SETTINGS, (leString*)&string_NAV_BUTTON_Settings);
    Marvin_BUTTON_NAV_SETTINGS->fn->setPressedImage(Marvin_BUTTON_NAV_SETTINGS, (leImage*)&figmaImg_Icon_16);
    Marvin_BUTTON_NAV_SETTINGS->fn->setReleasedImage(Marvin_BUTTON_NAV_SETTINGS, (leImage*)&figmaImg_Icon_16);
    Marvin_BUTTON_NAV_SETTINGS->fn->setImageMargin(Marvin_BUTTON_NAV_SETTINGS, 16);
    Marvin_BUTTON_NAV_SETTINGS->fn->setPressedOffset(Marvin_BUTTON_NAV_SETTINGS, 0);
    Marvin_PANEL_NAVIGATION_MIDDLE->fn->addChild(Marvin_PANEL_NAVIGATION_MIDDLE, (leWidget*)Marvin_BUTTON_NAV_SETTINGS);

    Marvin_PANEL_NAVIGATION_BOTTOM = leWidget_New();
    Marvin_PANEL_NAVIGATION_BOTTOM->fn->setPosition(Marvin_PANEL_NAVIGATION_BOTTOM, 0, 715);
    Marvin_PANEL_NAVIGATION_BOTTOM->fn->setSize(Marvin_PANEL_NAVIGATION_BOTTOM, 319, 85);
    Marvin_PANEL_NAVIGATION_BOTTOM->fn->setScheme(Marvin_PANEL_NAVIGATION_BOTTOM, &SCHEME_FILL_ZINC_900);
    Marvin_PANEL_NAVIGATION_BOTTOM->fn->setBackgroundType(Marvin_PANEL_NAVIGATION_BOTTOM, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_NAVIGATION_BOTTOM->fn->setBorderType(Marvin_PANEL_NAVIGATION_BOTTOM, LE_WIDGET_BORDER_LINE);
    Marvin_PANEL_NAVIGATION->fn->addChild(Marvin_PANEL_NAVIGATION, (leWidget*)Marvin_PANEL_NAVIGATION_BOTTOM);

    Marvin_panel_Container_196 = leWidget_New();
    Marvin_panel_Container_196->fn->setPosition(Marvin_panel_Container_196, 24, 25);
    Marvin_panel_Container_196->fn->setSize(Marvin_panel_Container_196, 271, 36);
    Marvin_panel_Container_196->fn->setBackgroundType(Marvin_panel_Container_196, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_NAVIGATION_BOTTOM->fn->addChild(Marvin_PANEL_NAVIGATION_BOTTOM, (leWidget*)Marvin_panel_Container_196);

    Marvin_panel_Container_197 = leWidget_New();
    Marvin_panel_Container_197->fn->setPosition(Marvin_panel_Container_197, 0, 12);
    Marvin_panel_Container_197->fn->setSize(Marvin_panel_Container_197, 12, 12);
    Marvin_panel_Container_197->fn->setScheme(Marvin_panel_Container_197, &SCHEME_FILL_GREEN_500);
    Marvin_panel_Container_196->fn->addChild(Marvin_panel_Container_196, (leWidget*)Marvin_panel_Container_197);

    Marvin_panel_Container_198 = leWidget_New();
    Marvin_panel_Container_198->fn->setPosition(Marvin_panel_Container_198, 24, 0);
    Marvin_panel_Container_198->fn->setSize(Marvin_panel_Container_198, 76, 36);
    Marvin_panel_Container_198->fn->setBackgroundType(Marvin_panel_Container_198, LE_WIDGET_BACKGROUND_NONE);
    Marvin_panel_Container_196->fn->addChild(Marvin_panel_Container_196, (leWidget*)Marvin_panel_Container_198);

    Marvin_panel_Container_199 = leWidget_New();
    Marvin_panel_Container_199->fn->setPosition(Marvin_panel_Container_199, 0, 0);
    Marvin_panel_Container_199->fn->setSize(Marvin_panel_Container_199, 76, 16);
    Marvin_panel_Container_199->fn->setBackgroundType(Marvin_panel_Container_199, LE_WIDGET_BACKGROUND_NONE);
    Marvin_panel_Container_198->fn->addChild(Marvin_panel_Container_198, (leWidget*)Marvin_panel_Container_199);

    Marvin_label_STATUS = leLabelWidget_New();
    Marvin_label_STATUS->fn->setPosition(Marvin_label_STATUS, 0, 0);
    Marvin_label_STATUS->fn->setSize(Marvin_label_STATUS, 44, 16);
    Marvin_label_STATUS->fn->setScheme(Marvin_label_STATUS, &SCHEME_TEXT_ZINC_200);
    Marvin_label_STATUS->fn->setBackgroundType(Marvin_label_STATUS, LE_WIDGET_BACKGROUND_NONE);
    Marvin_label_STATUS->fn->setVAlignment(Marvin_label_STATUS, LE_VALIGN_TOP);
    Marvin_label_STATUS->fn->setMargins(Marvin_label_STATUS, 0, 0, 0, 0);
    Marvin_label_STATUS->fn->setString(Marvin_label_STATUS, (leString*)&string_figmaStr_STATUS);
    Marvin_panel_Container_199->fn->addChild(Marvin_panel_Container_199, (leWidget*)Marvin_label_STATUS);

    Marvin_panel_Container_200 = leWidget_New();
    Marvin_panel_Container_200->fn->setPosition(Marvin_panel_Container_200, 0, 16);
    Marvin_panel_Container_200->fn->setSize(Marvin_panel_Container_200, 76, 20);
    Marvin_panel_Container_200->fn->setBackgroundType(Marvin_panel_Container_200, LE_WIDGET_BACKGROUND_NONE);
    Marvin_panel_Container_198->fn->addChild(Marvin_panel_Container_198, (leWidget*)Marvin_panel_Container_200);

    Marvin_label_Connected = leLabelWidget_New();
    Marvin_label_Connected->fn->setPosition(Marvin_label_Connected, 0, 0);
    Marvin_label_Connected->fn->setSize(Marvin_label_Connected, 76, 20);
    Marvin_label_Connected->fn->setScheme(Marvin_label_Connected, &SCHEME_TEXT_WHITE);
    Marvin_label_Connected->fn->setBackgroundType(Marvin_label_Connected, LE_WIDGET_BACKGROUND_NONE);
    Marvin_label_Connected->fn->setVAlignment(Marvin_label_Connected, LE_VALIGN_TOP);
    Marvin_label_Connected->fn->setMargins(Marvin_label_Connected, 0, 0, 0, 0);
    Marvin_label_Connected->fn->setString(Marvin_label_Connected, (leString*)&string_figmaStr_Connected);
    Marvin_panel_Container_200->fn->addChild(Marvin_panel_Container_200, (leWidget*)Marvin_label_Connected);

    leAddRootWidget(root1, 1);
    leSetLayerColorMode(1, LE_COLOR_MODE_RGB_565);

    // layer 2
    root2 = leWidget_New();
    root2->fn->setSize(root2, 1100, 660);
    root2->fn->setBackgroundType(root2, LE_WIDGET_BACKGROUND_NONE);
    root2->fn->setMargins(root2, 0, 0, 0, 0);
    root2->flags |= LE_WIDGET_IGNOREEVENTS;
    root2->flags |= LE_WIDGET_IGNOREPICK;

    Marvin_PANEL_SONG_SELECT = leWidget_New();
    Marvin_PANEL_SONG_SELECT->fn->setPosition(Marvin_PANEL_SONG_SELECT, 0, 0);
    Marvin_PANEL_SONG_SELECT->fn->setSize(Marvin_PANEL_SONG_SELECT, 1100, 660);
    Marvin_PANEL_SONG_SELECT->fn->setScheme(Marvin_PANEL_SONG_SELECT, &SCHEME_FILL_ZINC_900);
    root2->fn->addChild(root2, (leWidget*)Marvin_PANEL_SONG_SELECT);

    Marvin_PANEL_SONG_SELECT_TOP = leWidget_New();
    Marvin_PANEL_SONG_SELECT_TOP->fn->setPosition(Marvin_PANEL_SONG_SELECT_TOP, 0, 0);
    Marvin_PANEL_SONG_SELECT_TOP->fn->setSize(Marvin_PANEL_SONG_SELECT_TOP, 1100, 67);
    Marvin_PANEL_SONG_SELECT_TOP->fn->setBackgroundType(Marvin_PANEL_SONG_SELECT_TOP, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_SONG_SELECT_TOP->fn->setBorderType(Marvin_PANEL_SONG_SELECT_TOP, LE_WIDGET_BORDER_LINE);
    Marvin_PANEL_SONG_SELECT->fn->addChild(Marvin_PANEL_SONG_SELECT, (leWidget*)Marvin_PANEL_SONG_SELECT_TOP);

    Marvin_LABEL_SELECT_SONG = leLabelWidget_New();
    Marvin_LABEL_SELECT_SONG->fn->setPosition(Marvin_LABEL_SELECT_SONG, 24, 18);
    Marvin_LABEL_SELECT_SONG->fn->setSize(Marvin_LABEL_SELECT_SONG, 1000, 28);
    Marvin_LABEL_SELECT_SONG->fn->setScheme(Marvin_LABEL_SELECT_SONG, &SCHEME_TEXT_ZINC_200);
    Marvin_LABEL_SELECT_SONG->fn->setBackgroundType(Marvin_LABEL_SELECT_SONG, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_SELECT_SONG->fn->setMargins(Marvin_LABEL_SELECT_SONG, 0, 0, 0, 0);
    Marvin_LABEL_SELECT_SONG->fn->setString(Marvin_LABEL_SELECT_SONG, (leString*)&string_SONG_SELECT_SELECT_SONG);
    Marvin_PANEL_SONG_SELECT_TOP->fn->addChild(Marvin_PANEL_SONG_SELECT_TOP, (leWidget*)Marvin_LABEL_SELECT_SONG);

    Marvin_BUTTON_SONG_SELECT_CLOSE = leButtonWidget_New();
    Marvin_BUTTON_SONG_SELECT_CLOSE->fn->setPosition(Marvin_BUTTON_SONG_SELECT_CLOSE, 1033, 8);
    Marvin_BUTTON_SONG_SELECT_CLOSE->fn->setSize(Marvin_BUTTON_SONG_SELECT_CLOSE, 50, 50);
    Marvin_BUTTON_SONG_SELECT_CLOSE->fn->setBackgroundType(Marvin_BUTTON_SONG_SELECT_CLOSE, LE_WIDGET_BACKGROUND_NONE);
    Marvin_BUTTON_SONG_SELECT_CLOSE->fn->setBorderType(Marvin_BUTTON_SONG_SELECT_CLOSE, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_SONG_SELECT_CLOSE->fn->setPressedImage(Marvin_BUTTON_SONG_SELECT_CLOSE, (leImage*)&figmaImg_Icon_17);
    Marvin_BUTTON_SONG_SELECT_CLOSE->fn->setReleasedImage(Marvin_BUTTON_SONG_SELECT_CLOSE, (leImage*)&figmaImg_Icon_17);
    Marvin_PANEL_SONG_SELECT_TOP->fn->addChild(Marvin_PANEL_SONG_SELECT_TOP, (leWidget*)Marvin_BUTTON_SONG_SELECT_CLOSE);

    Marvin_PANEL_SONG_SELECT_BOTTOM = leWidget_New();
    Marvin_PANEL_SONG_SELECT_BOTTOM->fn->setPosition(Marvin_PANEL_SONG_SELECT_BOTTOM, 0, 66);
    Marvin_PANEL_SONG_SELECT_BOTTOM->fn->setSize(Marvin_PANEL_SONG_SELECT_BOTTOM, 1100, 594);
    Marvin_PANEL_SONG_SELECT_BOTTOM->fn->setBackgroundType(Marvin_PANEL_SONG_SELECT_BOTTOM, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_SONG_SELECT_BOTTOM->fn->setBorderType(Marvin_PANEL_SONG_SELECT_BOTTOM, LE_WIDGET_BORDER_LINE);
    Marvin_PANEL_SONG_SELECT->fn->addChild(Marvin_PANEL_SONG_SELECT, (leWidget*)Marvin_PANEL_SONG_SELECT_BOTTOM);

    Marvin_PANEL_SONG_SELECT_LEFT = leWidget_New();
    Marvin_PANEL_SONG_SELECT_LEFT->fn->setPosition(Marvin_PANEL_SONG_SELECT_LEFT, 0, 0);
    Marvin_PANEL_SONG_SELECT_LEFT->fn->setSize(Marvin_PANEL_SONG_SELECT_LEFT, 320, 594);
    Marvin_PANEL_SONG_SELECT_LEFT->fn->setBackgroundType(Marvin_PANEL_SONG_SELECT_LEFT, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_SONG_SELECT_LEFT->fn->setBorderType(Marvin_PANEL_SONG_SELECT_LEFT, LE_WIDGET_BORDER_LINE);
    Marvin_PANEL_SONG_SELECT_BOTTOM->fn->addChild(Marvin_PANEL_SONG_SELECT_BOTTOM, (leWidget*)Marvin_PANEL_SONG_SELECT_LEFT);

    Marvin_PANEL_SETLIST = leWidget_New();
    Marvin_PANEL_SETLIST->fn->setPosition(Marvin_PANEL_SETLIST, 0, 0);
    Marvin_PANEL_SETLIST->fn->setSize(Marvin_PANEL_SETLIST, 320, 33);
    Marvin_PANEL_SETLIST->fn->setBackgroundType(Marvin_PANEL_SETLIST, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_SONG_SELECT_LEFT->fn->addChild(Marvin_PANEL_SONG_SELECT_LEFT, (leWidget*)Marvin_PANEL_SETLIST);

    Marvin_LABEL_SETLIST = leLabelWidget_New();
    Marvin_LABEL_SETLIST->fn->setPosition(Marvin_LABEL_SETLIST, 16, 8);
    Marvin_LABEL_SETLIST->fn->setSize(Marvin_LABEL_SETLIST, 285, 16);
    Marvin_LABEL_SETLIST->fn->setScheme(Marvin_LABEL_SETLIST, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_SETLIST->fn->setBackgroundType(Marvin_LABEL_SETLIST, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_SETLIST->fn->setVAlignment(Marvin_LABEL_SETLIST, LE_VALIGN_TOP);
    Marvin_LABEL_SETLIST->fn->setMargins(Marvin_LABEL_SETLIST, 0, 0, 0, 0);
    Marvin_LABEL_SETLIST->fn->setString(Marvin_LABEL_SETLIST, (leString*)&string_SONG_SELECT_SETLIST);
    Marvin_PANEL_SETLIST->fn->addChild(Marvin_PANEL_SETLIST, (leWidget*)Marvin_LABEL_SETLIST);

    Marvin_PANEL_SONG_SELECT_CENTER = leWidget_New();
    Marvin_PANEL_SONG_SELECT_CENTER->fn->setPosition(Marvin_PANEL_SONG_SELECT_CENTER, 319, 0);
    Marvin_PANEL_SONG_SELECT_CENTER->fn->setSize(Marvin_PANEL_SONG_SELECT_CENTER, 558, 594);
    Marvin_PANEL_SONG_SELECT_CENTER->fn->setBackgroundType(Marvin_PANEL_SONG_SELECT_CENTER, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_SONG_SELECT_CENTER->fn->setBorderType(Marvin_PANEL_SONG_SELECT_CENTER, LE_WIDGET_BORDER_LINE);
    Marvin_PANEL_SONG_SELECT_BOTTOM->fn->addChild(Marvin_PANEL_SONG_SELECT_BOTTOM, (leWidget*)Marvin_PANEL_SONG_SELECT_CENTER);

    Marvin_PANEL_SONG_SELECT_SONG_INFO = leWidget_New();
    Marvin_PANEL_SONG_SELECT_SONG_INFO->fn->setPosition(Marvin_PANEL_SONG_SELECT_SONG_INFO, 24, 248);
    Marvin_PANEL_SONG_SELECT_SONG_INFO->fn->setSize(Marvin_PANEL_SONG_SELECT_SONG_INFO, 505, 88);
    Marvin_PANEL_SONG_SELECT_SONG_INFO->fn->setScheme(Marvin_PANEL_SONG_SELECT_SONG_INFO, &SCHEME_BACKGROUND);
    Marvin_PANEL_SONG_SELECT_SONG_INFO->fn->setBackgroundType(Marvin_PANEL_SONG_SELECT_SONG_INFO, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_SONG_SELECT_CENTER->fn->addChild(Marvin_PANEL_SONG_SELECT_CENTER, (leWidget*)Marvin_PANEL_SONG_SELECT_SONG_INFO);

    Marvin_LABEL_SONG_SELECT_ALBUM = leLabelWidget_New();
    Marvin_LABEL_SONG_SELECT_ALBUM->fn->setPosition(Marvin_LABEL_SONG_SELECT_ALBUM, 0, 0);
    Marvin_LABEL_SONG_SELECT_ALBUM->fn->setSize(Marvin_LABEL_SONG_SELECT_ALBUM, 240, 16);
    Marvin_LABEL_SONG_SELECT_ALBUM->fn->setScheme(Marvin_LABEL_SONG_SELECT_ALBUM, &SCHEME_TEXT_ZINC_600);
    Marvin_LABEL_SONG_SELECT_ALBUM->fn->setBackgroundType(Marvin_LABEL_SONG_SELECT_ALBUM, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_SONG_SELECT_ALBUM->fn->setVAlignment(Marvin_LABEL_SONG_SELECT_ALBUM, LE_VALIGN_TOP);
    Marvin_LABEL_SONG_SELECT_ALBUM->fn->setMargins(Marvin_LABEL_SONG_SELECT_ALBUM, 0, 0, 0, 0);
    Marvin_LABEL_SONG_SELECT_ALBUM->fn->setString(Marvin_LABEL_SONG_SELECT_ALBUM, (leString*)&string_SONG_SELECT_ALBUM);
    Marvin_PANEL_SONG_SELECT_SONG_INFO->fn->addChild(Marvin_PANEL_SONG_SELECT_SONG_INFO, (leWidget*)Marvin_LABEL_SONG_SELECT_ALBUM);

    Marvin_LABEL_SONG_SELECT_SongAlbum = leLabelWidget_New();
    Marvin_LABEL_SONG_SELECT_SongAlbum->fn->setPosition(Marvin_LABEL_SONG_SELECT_SongAlbum, 0, 16);
    Marvin_LABEL_SONG_SELECT_SongAlbum->fn->setSize(Marvin_LABEL_SONG_SELECT_SongAlbum, 240, 20);
    Marvin_LABEL_SONG_SELECT_SongAlbum->fn->setScheme(Marvin_LABEL_SONG_SELECT_SongAlbum, &SCHEME_TEXT_ZINC_200);
    Marvin_LABEL_SONG_SELECT_SongAlbum->fn->setBackgroundType(Marvin_LABEL_SONG_SELECT_SongAlbum, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_SONG_SELECT_SongAlbum->fn->setVAlignment(Marvin_LABEL_SONG_SELECT_SongAlbum, LE_VALIGN_TOP);
    Marvin_LABEL_SONG_SELECT_SongAlbum->fn->setMargins(Marvin_LABEL_SONG_SELECT_SongAlbum, 0, 0, 0, 0);
    Marvin_LABEL_SONG_SELECT_SongAlbum->fn->setString(Marvin_LABEL_SONG_SELECT_SongAlbum, (leString*)&string_SONG_SELECT_SongAlbum);
    Marvin_PANEL_SONG_SELECT_SONG_INFO->fn->addChild(Marvin_PANEL_SONG_SELECT_SONG_INFO, (leWidget*)Marvin_LABEL_SONG_SELECT_SongAlbum);

    Marvin_LABEL_SONG_SELECT_YEAR = leLabelWidget_New();
    Marvin_LABEL_SONG_SELECT_YEAR->fn->setPosition(Marvin_LABEL_SONG_SELECT_YEAR, 259, 0);
    Marvin_LABEL_SONG_SELECT_YEAR->fn->setSize(Marvin_LABEL_SONG_SELECT_YEAR, 240, 16);
    Marvin_LABEL_SONG_SELECT_YEAR->fn->setScheme(Marvin_LABEL_SONG_SELECT_YEAR, &SCHEME_TEXT_ZINC_600);
    Marvin_LABEL_SONG_SELECT_YEAR->fn->setBackgroundType(Marvin_LABEL_SONG_SELECT_YEAR, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_SONG_SELECT_YEAR->fn->setVAlignment(Marvin_LABEL_SONG_SELECT_YEAR, LE_VALIGN_TOP);
    Marvin_LABEL_SONG_SELECT_YEAR->fn->setMargins(Marvin_LABEL_SONG_SELECT_YEAR, 0, 0, 0, 0);
    Marvin_LABEL_SONG_SELECT_YEAR->fn->setString(Marvin_LABEL_SONG_SELECT_YEAR, (leString*)&string_SONG_SELECT_YEAR);
    Marvin_PANEL_SONG_SELECT_SONG_INFO->fn->addChild(Marvin_PANEL_SONG_SELECT_SONG_INFO, (leWidget*)Marvin_LABEL_SONG_SELECT_YEAR);

    Marvin_LABEL_SONG_SELECT_SongYear = leLabelWidget_New();
    Marvin_LABEL_SONG_SELECT_SongYear->fn->setPosition(Marvin_LABEL_SONG_SELECT_SongYear, 259, 16);
    Marvin_LABEL_SONG_SELECT_SongYear->fn->setSize(Marvin_LABEL_SONG_SELECT_SongYear, 240, 20);
    Marvin_LABEL_SONG_SELECT_SongYear->fn->setScheme(Marvin_LABEL_SONG_SELECT_SongYear, &SCHEME_TEXT_ZINC_200);
    Marvin_LABEL_SONG_SELECT_SongYear->fn->setBackgroundType(Marvin_LABEL_SONG_SELECT_SongYear, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_SONG_SELECT_SongYear->fn->setVAlignment(Marvin_LABEL_SONG_SELECT_SongYear, LE_VALIGN_TOP);
    Marvin_LABEL_SONG_SELECT_SongYear->fn->setMargins(Marvin_LABEL_SONG_SELECT_SongYear, 0, 0, 0, 0);
    Marvin_LABEL_SONG_SELECT_SongYear->fn->setString(Marvin_LABEL_SONG_SELECT_SongYear, (leString*)&string_SONG_SELECT_SongYear);
    Marvin_PANEL_SONG_SELECT_SONG_INFO->fn->addChild(Marvin_PANEL_SONG_SELECT_SONG_INFO, (leWidget*)Marvin_LABEL_SONG_SELECT_SongYear);

    Marvin_LABEL_SONG_SELECT_GENRE = leLabelWidget_New();
    Marvin_LABEL_SONG_SELECT_GENRE->fn->setPosition(Marvin_LABEL_SONG_SELECT_GENRE, 0, 48);
    Marvin_LABEL_SONG_SELECT_GENRE->fn->setSize(Marvin_LABEL_SONG_SELECT_GENRE, 240, 16);
    Marvin_LABEL_SONG_SELECT_GENRE->fn->setScheme(Marvin_LABEL_SONG_SELECT_GENRE, &SCHEME_TEXT_ZINC_600);
    Marvin_LABEL_SONG_SELECT_GENRE->fn->setBackgroundType(Marvin_LABEL_SONG_SELECT_GENRE, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_SONG_SELECT_GENRE->fn->setVAlignment(Marvin_LABEL_SONG_SELECT_GENRE, LE_VALIGN_TOP);
    Marvin_LABEL_SONG_SELECT_GENRE->fn->setMargins(Marvin_LABEL_SONG_SELECT_GENRE, 0, 0, 0, 0);
    Marvin_LABEL_SONG_SELECT_GENRE->fn->setString(Marvin_LABEL_SONG_SELECT_GENRE, (leString*)&string_SONG_SELECT_GENRE);
    Marvin_PANEL_SONG_SELECT_SONG_INFO->fn->addChild(Marvin_PANEL_SONG_SELECT_SONG_INFO, (leWidget*)Marvin_LABEL_SONG_SELECT_GENRE);

    Marvin_LABEL_SONG_SELECT_SongGenre = leLabelWidget_New();
    Marvin_LABEL_SONG_SELECT_SongGenre->fn->setPosition(Marvin_LABEL_SONG_SELECT_SongGenre, 0, 64);
    Marvin_LABEL_SONG_SELECT_SongGenre->fn->setSize(Marvin_LABEL_SONG_SELECT_SongGenre, 240, 20);
    Marvin_LABEL_SONG_SELECT_SongGenre->fn->setScheme(Marvin_LABEL_SONG_SELECT_SongGenre, &SCHEME_TEXT_ZINC_200);
    Marvin_LABEL_SONG_SELECT_SongGenre->fn->setBackgroundType(Marvin_LABEL_SONG_SELECT_SongGenre, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_SONG_SELECT_SongGenre->fn->setVAlignment(Marvin_LABEL_SONG_SELECT_SongGenre, LE_VALIGN_TOP);
    Marvin_LABEL_SONG_SELECT_SongGenre->fn->setMargins(Marvin_LABEL_SONG_SELECT_SongGenre, 0, 0, 0, 0);
    Marvin_LABEL_SONG_SELECT_SongGenre->fn->setString(Marvin_LABEL_SONG_SELECT_SongGenre, (leString*)&string_SONG_SELECT_SongGenre);
    Marvin_PANEL_SONG_SELECT_SONG_INFO->fn->addChild(Marvin_PANEL_SONG_SELECT_SONG_INFO, (leWidget*)Marvin_LABEL_SONG_SELECT_SongGenre);

    Marvin_LABEL_SONG_SELECT_DURATION = leLabelWidget_New();
    Marvin_LABEL_SONG_SELECT_DURATION->fn->setPosition(Marvin_LABEL_SONG_SELECT_DURATION, 259, 48);
    Marvin_LABEL_SONG_SELECT_DURATION->fn->setSize(Marvin_LABEL_SONG_SELECT_DURATION, 240, 16);
    Marvin_LABEL_SONG_SELECT_DURATION->fn->setScheme(Marvin_LABEL_SONG_SELECT_DURATION, &SCHEME_TEXT_ZINC_600);
    Marvin_LABEL_SONG_SELECT_DURATION->fn->setBackgroundType(Marvin_LABEL_SONG_SELECT_DURATION, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_SONG_SELECT_DURATION->fn->setVAlignment(Marvin_LABEL_SONG_SELECT_DURATION, LE_VALIGN_TOP);
    Marvin_LABEL_SONG_SELECT_DURATION->fn->setMargins(Marvin_LABEL_SONG_SELECT_DURATION, 0, 0, 0, 0);
    Marvin_LABEL_SONG_SELECT_DURATION->fn->setString(Marvin_LABEL_SONG_SELECT_DURATION, (leString*)&string_SONG_SELECT_DURATION);
    Marvin_PANEL_SONG_SELECT_SONG_INFO->fn->addChild(Marvin_PANEL_SONG_SELECT_SONG_INFO, (leWidget*)Marvin_LABEL_SONG_SELECT_DURATION);

    Marvin_LABEL_SONG_SELECT_SongDuration = leLabelWidget_New();
    Marvin_LABEL_SONG_SELECT_SongDuration->fn->setPosition(Marvin_LABEL_SONG_SELECT_SongDuration, 259, 64);
    Marvin_LABEL_SONG_SELECT_SongDuration->fn->setSize(Marvin_LABEL_SONG_SELECT_SongDuration, 240, 20);
    Marvin_LABEL_SONG_SELECT_SongDuration->fn->setScheme(Marvin_LABEL_SONG_SELECT_SongDuration, &SCHEME_TEXT_ZINC_200);
    Marvin_LABEL_SONG_SELECT_SongDuration->fn->setBackgroundType(Marvin_LABEL_SONG_SELECT_SongDuration, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_SONG_SELECT_SongDuration->fn->setVAlignment(Marvin_LABEL_SONG_SELECT_SongDuration, LE_VALIGN_TOP);
    Marvin_LABEL_SONG_SELECT_SongDuration->fn->setMargins(Marvin_LABEL_SONG_SELECT_SongDuration, 0, 0, 0, 0);
    Marvin_LABEL_SONG_SELECT_SongDuration->fn->setString(Marvin_LABEL_SONG_SELECT_SongDuration, (leString*)&string_SONG_SELECT_SongDuration);
    Marvin_PANEL_SONG_SELECT_SONG_INFO->fn->addChild(Marvin_PANEL_SONG_SELECT_SONG_INFO, (leWidget*)Marvin_LABEL_SONG_SELECT_SongDuration);

    Marvin_PANEL_SONG_SELECT_RIGHT = leWidget_New();
    Marvin_PANEL_SONG_SELECT_RIGHT->fn->setPosition(Marvin_PANEL_SONG_SELECT_RIGHT, 876, 0);
    Marvin_PANEL_SONG_SELECT_RIGHT->fn->setSize(Marvin_PANEL_SONG_SELECT_RIGHT, 224, 594);
    Marvin_PANEL_SONG_SELECT_RIGHT->fn->setScheme(Marvin_PANEL_SONG_SELECT_RIGHT, &SCHEME_BACKGROUND);
    Marvin_PANEL_SONG_SELECT_RIGHT->fn->setBackgroundType(Marvin_PANEL_SONG_SELECT_RIGHT, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_SONG_SELECT_RIGHT->fn->setBorderType(Marvin_PANEL_SONG_SELECT_RIGHT, LE_WIDGET_BORDER_LINE);
    Marvin_PANEL_SONG_SELECT_BOTTOM->fn->addChild(Marvin_PANEL_SONG_SELECT_BOTTOM, (leWidget*)Marvin_PANEL_SONG_SELECT_RIGHT);

    Marvin_PANEL_SONG_SELECT_DIFFICULTY = leWidget_New();
    Marvin_PANEL_SONG_SELECT_DIFFICULTY->fn->setPosition(Marvin_PANEL_SONG_SELECT_DIFFICULTY, 16, 16);
    Marvin_PANEL_SONG_SELECT_DIFFICULTY->fn->setSize(Marvin_PANEL_SONG_SELECT_DIFFICULTY, 192, 224);
    Marvin_PANEL_SONG_SELECT_DIFFICULTY->fn->setBackgroundType(Marvin_PANEL_SONG_SELECT_DIFFICULTY, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_SONG_SELECT_RIGHT->fn->addChild(Marvin_PANEL_SONG_SELECT_RIGHT, (leWidget*)Marvin_PANEL_SONG_SELECT_DIFFICULTY);

    Marvin_LABEL_SONG_SELECT_DIFFICULTY = leLabelWidget_New();
    Marvin_LABEL_SONG_SELECT_DIFFICULTY->fn->setPosition(Marvin_LABEL_SONG_SELECT_DIFFICULTY, 0, 4);
    Marvin_LABEL_SONG_SELECT_DIFFICULTY->fn->setSize(Marvin_LABEL_SONG_SELECT_DIFFICULTY, 192, 16);
    Marvin_LABEL_SONG_SELECT_DIFFICULTY->fn->setScheme(Marvin_LABEL_SONG_SELECT_DIFFICULTY, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_SONG_SELECT_DIFFICULTY->fn->setBackgroundType(Marvin_LABEL_SONG_SELECT_DIFFICULTY, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_SONG_SELECT_DIFFICULTY->fn->setVAlignment(Marvin_LABEL_SONG_SELECT_DIFFICULTY, LE_VALIGN_TOP);
    Marvin_LABEL_SONG_SELECT_DIFFICULTY->fn->setMargins(Marvin_LABEL_SONG_SELECT_DIFFICULTY, 0, 0, 0, 0);
    Marvin_LABEL_SONG_SELECT_DIFFICULTY->fn->setString(Marvin_LABEL_SONG_SELECT_DIFFICULTY, (leString*)&string_SONG_SELECT_DIFFICULTY);
    Marvin_PANEL_SONG_SELECT_DIFFICULTY->fn->addChild(Marvin_PANEL_SONG_SELECT_DIFFICULTY, (leWidget*)Marvin_LABEL_SONG_SELECT_DIFFICULTY);

    Marvin_BUTTON_SONG_SELECT_EASY = leButtonWidget_New();
    Marvin_BUTTON_SONG_SELECT_EASY->fn->setPosition(Marvin_BUTTON_SONG_SELECT_EASY, 0, 32);
    Marvin_BUTTON_SONG_SELECT_EASY->fn->setSize(Marvin_BUTTON_SONG_SELECT_EASY, 192, 42);
    Marvin_BUTTON_SONG_SELECT_EASY->fn->setScheme(Marvin_BUTTON_SONG_SELECT_EASY, &SCHEME_BUTTON_DIFFICULTY);
    Marvin_BUTTON_SONG_SELECT_EASY->fn->setBorderType(Marvin_BUTTON_SONG_SELECT_EASY, LE_WIDGET_BORDER_LINE);
    Marvin_BUTTON_SONG_SELECT_EASY->fn->setVAlignment(Marvin_BUTTON_SONG_SELECT_EASY, LE_VALIGN_TOP);
    Marvin_BUTTON_SONG_SELECT_EASY->fn->setMargins(Marvin_BUTTON_SONG_SELECT_EASY, 4, 11, 4, 4);
    Marvin_BUTTON_SONG_SELECT_EASY->fn->setString(Marvin_BUTTON_SONG_SELECT_EASY, (leString*)&string_SONG_SELECT_EASY);
    Marvin_BUTTON_SONG_SELECT_EASY->fn->setPressedOffset(Marvin_BUTTON_SONG_SELECT_EASY, 0);
    Marvin_PANEL_SONG_SELECT_DIFFICULTY->fn->addChild(Marvin_PANEL_SONG_SELECT_DIFFICULTY, (leWidget*)Marvin_BUTTON_SONG_SELECT_EASY);

    Marvin_BUTTON_SONG_SELECT_MEDIUM = leButtonWidget_New();
    Marvin_BUTTON_SONG_SELECT_MEDIUM->fn->setPosition(Marvin_BUTTON_SONG_SELECT_MEDIUM, 0, 82);
    Marvin_BUTTON_SONG_SELECT_MEDIUM->fn->setSize(Marvin_BUTTON_SONG_SELECT_MEDIUM, 192, 42);
    Marvin_BUTTON_SONG_SELECT_MEDIUM->fn->setScheme(Marvin_BUTTON_SONG_SELECT_MEDIUM, &SCHEME_BUTTON_DIFFICULTY);
    Marvin_BUTTON_SONG_SELECT_MEDIUM->fn->setBorderType(Marvin_BUTTON_SONG_SELECT_MEDIUM, LE_WIDGET_BORDER_LINE);
    Marvin_BUTTON_SONG_SELECT_MEDIUM->fn->setVAlignment(Marvin_BUTTON_SONG_SELECT_MEDIUM, LE_VALIGN_TOP);
    Marvin_BUTTON_SONG_SELECT_MEDIUM->fn->setMargins(Marvin_BUTTON_SONG_SELECT_MEDIUM, 4, 11, 4, 4);
    Marvin_BUTTON_SONG_SELECT_MEDIUM->fn->setString(Marvin_BUTTON_SONG_SELECT_MEDIUM, (leString*)&string_SONG_SELECT_MEDIUM);
    Marvin_BUTTON_SONG_SELECT_MEDIUM->fn->setPressedOffset(Marvin_BUTTON_SONG_SELECT_MEDIUM, 0);
    Marvin_PANEL_SONG_SELECT_DIFFICULTY->fn->addChild(Marvin_PANEL_SONG_SELECT_DIFFICULTY, (leWidget*)Marvin_BUTTON_SONG_SELECT_MEDIUM);

    Marvin_BUTTON_SONG_SELECT_HARD = leButtonWidget_New();
    Marvin_BUTTON_SONG_SELECT_HARD->fn->setPosition(Marvin_BUTTON_SONG_SELECT_HARD, 0, 132);
    Marvin_BUTTON_SONG_SELECT_HARD->fn->setSize(Marvin_BUTTON_SONG_SELECT_HARD, 192, 42);
    Marvin_BUTTON_SONG_SELECT_HARD->fn->setScheme(Marvin_BUTTON_SONG_SELECT_HARD, &SCHEME_BUTTON_DIFFICULTY);
    Marvin_BUTTON_SONG_SELECT_HARD->fn->setBorderType(Marvin_BUTTON_SONG_SELECT_HARD, LE_WIDGET_BORDER_LINE);
    Marvin_BUTTON_SONG_SELECT_HARD->fn->setVAlignment(Marvin_BUTTON_SONG_SELECT_HARD, LE_VALIGN_TOP);
    Marvin_BUTTON_SONG_SELECT_HARD->fn->setMargins(Marvin_BUTTON_SONG_SELECT_HARD, 4, 11, 4, 4);
    Marvin_BUTTON_SONG_SELECT_HARD->fn->setString(Marvin_BUTTON_SONG_SELECT_HARD, (leString*)&string_SONG_SELECT_HARD);
    Marvin_BUTTON_SONG_SELECT_HARD->fn->setPressedOffset(Marvin_BUTTON_SONG_SELECT_HARD, 0);
    Marvin_PANEL_SONG_SELECT_DIFFICULTY->fn->addChild(Marvin_PANEL_SONG_SELECT_DIFFICULTY, (leWidget*)Marvin_BUTTON_SONG_SELECT_HARD);

    Marvin_BUTTON_SONG_SELECT_EXPERT = leButtonWidget_New();
    Marvin_BUTTON_SONG_SELECT_EXPERT->fn->setPosition(Marvin_BUTTON_SONG_SELECT_EXPERT, 0, 182);
    Marvin_BUTTON_SONG_SELECT_EXPERT->fn->setSize(Marvin_BUTTON_SONG_SELECT_EXPERT, 192, 42);
    Marvin_BUTTON_SONG_SELECT_EXPERT->fn->setScheme(Marvin_BUTTON_SONG_SELECT_EXPERT, &SCHEME_BUTTON_DIFFICULTY);
    Marvin_BUTTON_SONG_SELECT_EXPERT->fn->setBorderType(Marvin_BUTTON_SONG_SELECT_EXPERT, LE_WIDGET_BORDER_LINE);
    Marvin_BUTTON_SONG_SELECT_EXPERT->fn->setVAlignment(Marvin_BUTTON_SONG_SELECT_EXPERT, LE_VALIGN_TOP);
    Marvin_BUTTON_SONG_SELECT_EXPERT->fn->setMargins(Marvin_BUTTON_SONG_SELECT_EXPERT, 4, 11, 4, 4);
    Marvin_BUTTON_SONG_SELECT_EXPERT->fn->setString(Marvin_BUTTON_SONG_SELECT_EXPERT, (leString*)&string_SONG_SELECT_EXPERT);
    Marvin_BUTTON_SONG_SELECT_EXPERT->fn->setPressedOffset(Marvin_BUTTON_SONG_SELECT_EXPERT, 0);
    Marvin_PANEL_SONG_SELECT_DIFFICULTY->fn->addChild(Marvin_PANEL_SONG_SELECT_DIFFICULTY, (leWidget*)Marvin_BUTTON_SONG_SELECT_EXPERT);

    Marvin_PANEL_SONG_SELECT_MODE = leWidget_New();
    Marvin_PANEL_SONG_SELECT_MODE->fn->setPosition(Marvin_PANEL_SONG_SELECT_MODE, 16, 252);
    Marvin_PANEL_SONG_SELECT_MODE->fn->setSize(Marvin_PANEL_SONG_SELECT_MODE, 192, 182);
    Marvin_PANEL_SONG_SELECT_MODE->fn->setScheme(Marvin_PANEL_SONG_SELECT_MODE, &SCHEME_BACKGROUND);
    Marvin_PANEL_SONG_SELECT_MODE->fn->setBackgroundType(Marvin_PANEL_SONG_SELECT_MODE, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_SONG_SELECT_RIGHT->fn->addChild(Marvin_PANEL_SONG_SELECT_RIGHT, (leWidget*)Marvin_PANEL_SONG_SELECT_MODE);

    Marvin_LABEL_SONG_SELECT_MODE = leLabelWidget_New();
    Marvin_LABEL_SONG_SELECT_MODE->fn->setPosition(Marvin_LABEL_SONG_SELECT_MODE, 0, 12);
    Marvin_LABEL_SONG_SELECT_MODE->fn->setSize(Marvin_LABEL_SONG_SELECT_MODE, 192, 16);
    Marvin_LABEL_SONG_SELECT_MODE->fn->setScheme(Marvin_LABEL_SONG_SELECT_MODE, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_SONG_SELECT_MODE->fn->setBackgroundType(Marvin_LABEL_SONG_SELECT_MODE, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_SONG_SELECT_MODE->fn->setVAlignment(Marvin_LABEL_SONG_SELECT_MODE, LE_VALIGN_TOP);
    Marvin_LABEL_SONG_SELECT_MODE->fn->setMargins(Marvin_LABEL_SONG_SELECT_MODE, 0, 0, 0, 0);
    Marvin_LABEL_SONG_SELECT_MODE->fn->setString(Marvin_LABEL_SONG_SELECT_MODE, (leString*)&string_SONG_SELECT_MODE);
    Marvin_PANEL_SONG_SELECT_MODE->fn->addChild(Marvin_PANEL_SONG_SELECT_MODE, (leWidget*)Marvin_LABEL_SONG_SELECT_MODE);

    Marvin_BUTTON_SONG_SELECT_1P_ROBOT = leButtonWidget_New();
    Marvin_BUTTON_SONG_SELECT_1P_ROBOT->fn->setPosition(Marvin_BUTTON_SONG_SELECT_1P_ROBOT, 0, 40);
    Marvin_BUTTON_SONG_SELECT_1P_ROBOT->fn->setSize(Marvin_BUTTON_SONG_SELECT_1P_ROBOT, 192, 42);
    Marvin_BUTTON_SONG_SELECT_1P_ROBOT->fn->setScheme(Marvin_BUTTON_SONG_SELECT_1P_ROBOT, &SCHEME_BUTTON_MODE);
    Marvin_BUTTON_SONG_SELECT_1P_ROBOT->fn->setBorderType(Marvin_BUTTON_SONG_SELECT_1P_ROBOT, LE_WIDGET_BORDER_LINE);
    Marvin_BUTTON_SONG_SELECT_1P_ROBOT->fn->setVAlignment(Marvin_BUTTON_SONG_SELECT_1P_ROBOT, LE_VALIGN_TOP);
    Marvin_BUTTON_SONG_SELECT_1P_ROBOT->fn->setMargins(Marvin_BUTTON_SONG_SELECT_1P_ROBOT, 4, 11, 4, 4);
    Marvin_BUTTON_SONG_SELECT_1P_ROBOT->fn->setString(Marvin_BUTTON_SONG_SELECT_1P_ROBOT, (leString*)&string_SONG_SELECT_1P_ROBOT);
    Marvin_BUTTON_SONG_SELECT_1P_ROBOT->fn->setPressedOffset(Marvin_BUTTON_SONG_SELECT_1P_ROBOT, 0);
    Marvin_PANEL_SONG_SELECT_MODE->fn->addChild(Marvin_PANEL_SONG_SELECT_MODE, (leWidget*)Marvin_BUTTON_SONG_SELECT_1P_ROBOT);

    Marvin_BUTTON_SONG_SELECT_1P_HUMAN = leButtonWidget_New();
    Marvin_BUTTON_SONG_SELECT_1P_HUMAN->fn->setPosition(Marvin_BUTTON_SONG_SELECT_1P_HUMAN, 0, 90);
    Marvin_BUTTON_SONG_SELECT_1P_HUMAN->fn->setSize(Marvin_BUTTON_SONG_SELECT_1P_HUMAN, 192, 42);
    Marvin_BUTTON_SONG_SELECT_1P_HUMAN->fn->setScheme(Marvin_BUTTON_SONG_SELECT_1P_HUMAN, &SCHEME_BUTTON_MODE);
    Marvin_BUTTON_SONG_SELECT_1P_HUMAN->fn->setBorderType(Marvin_BUTTON_SONG_SELECT_1P_HUMAN, LE_WIDGET_BORDER_LINE);
    Marvin_BUTTON_SONG_SELECT_1P_HUMAN->fn->setVAlignment(Marvin_BUTTON_SONG_SELECT_1P_HUMAN, LE_VALIGN_TOP);
    Marvin_BUTTON_SONG_SELECT_1P_HUMAN->fn->setMargins(Marvin_BUTTON_SONG_SELECT_1P_HUMAN, 4, 11, 4, 4);
    Marvin_BUTTON_SONG_SELECT_1P_HUMAN->fn->setString(Marvin_BUTTON_SONG_SELECT_1P_HUMAN, (leString*)&string_SONG_SELECT_1P_HUMAN);
    Marvin_BUTTON_SONG_SELECT_1P_HUMAN->fn->setPressedOffset(Marvin_BUTTON_SONG_SELECT_1P_HUMAN, 0);
    Marvin_PANEL_SONG_SELECT_MODE->fn->addChild(Marvin_PANEL_SONG_SELECT_MODE, (leWidget*)Marvin_BUTTON_SONG_SELECT_1P_HUMAN);

    Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN = leButtonWidget_New();
    Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN->fn->setPosition(Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN, 0, 140);
    Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN->fn->setSize(Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN, 192, 42);
    Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN->fn->setScheme(Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN, &SCHEME_BUTTON_MODE);
    Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN->fn->setBorderType(Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN, LE_WIDGET_BORDER_LINE);
    Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN->fn->setVAlignment(Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN, LE_VALIGN_TOP);
    Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN->fn->setMargins(Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN, 4, 11, 4, 4);
    Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN->fn->setString(Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN, (leString*)&string_SONG_SELECT_2P_ROBOT_vs_HUMAN);
    Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN->fn->setPressedOffset(Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN, 0);
    Marvin_PANEL_SONG_SELECT_MODE->fn->addChild(Marvin_PANEL_SONG_SELECT_MODE, (leWidget*)Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN);

    Marvin_BUTTON_SONG_SELECT_SELECT = leButtonWidget_New();
    Marvin_BUTTON_SONG_SELECT_SELECT->fn->setPosition(Marvin_BUTTON_SONG_SELECT_SELECT, 16, 521);
    Marvin_BUTTON_SONG_SELECT_SELECT->fn->setSize(Marvin_BUTTON_SONG_SELECT_SELECT, 192, 56);
    Marvin_BUTTON_SONG_SELECT_SELECT->fn->setScheme(Marvin_BUTTON_SONG_SELECT_SELECT, &SCHEME_BUTTON_SELECT);
    Marvin_BUTTON_SONG_SELECT_SELECT->fn->setBorderType(Marvin_BUTTON_SONG_SELECT_SELECT, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_SONG_SELECT_SELECT->fn->setVAlignment(Marvin_BUTTON_SONG_SELECT_SELECT, LE_VALIGN_TOP);
    Marvin_BUTTON_SONG_SELECT_SELECT->fn->setMargins(Marvin_BUTTON_SONG_SELECT_SELECT, 4, 16, 4, 4);
    Marvin_BUTTON_SONG_SELECT_SELECT->fn->setString(Marvin_BUTTON_SONG_SELECT_SELECT, (leString*)&string_SONG_SELECT_SELECT);
    Marvin_BUTTON_SONG_SELECT_SELECT->fn->setPressedImage(Marvin_BUTTON_SONG_SELECT_SELECT, (leImage*)&BUTTON_ICON_CHECK);
    Marvin_BUTTON_SONG_SELECT_SELECT->fn->setReleasedImage(Marvin_BUTTON_SONG_SELECT_SELECT, (leImage*)&BUTTON_ICON_CHECK);
    Marvin_BUTTON_SONG_SELECT_SELECT->fn->setImageMargin(Marvin_BUTTON_SONG_SELECT_SELECT, 9);
    Marvin_BUTTON_SONG_SELECT_SELECT->fn->setPressedOffset(Marvin_BUTTON_SONG_SELECT_SELECT, 0);
    Marvin_PANEL_SONG_SELECT_RIGHT->fn->addChild(Marvin_PANEL_SONG_SELECT_RIGHT, (leWidget*)Marvin_BUTTON_SONG_SELECT_SELECT);

    leAddRootWidget(root2, 2);
    leSetLayerColorMode(2, LE_COLOR_MODE_RGB_565);

    // layer 3
    root3 = leWidget_New();
    root3->fn->setSize(root3, 508, 208);
    root3->fn->setBackgroundType(root3, LE_WIDGET_BACKGROUND_NONE);
    root3->fn->setMargins(root3, 0, 0, 0, 0);
    root3->flags |= LE_WIDGET_IGNOREEVENTS;
    root3->flags |= LE_WIDGET_IGNOREPICK;

    Marvin_PANEL_SONG_SELECT_ALBUM_ART = leWidget_New();
    Marvin_PANEL_SONG_SELECT_ALBUM_ART->fn->setPosition(Marvin_PANEL_SONG_SELECT_ALBUM_ART, 0, 0);
    Marvin_PANEL_SONG_SELECT_ALBUM_ART->fn->setSize(Marvin_PANEL_SONG_SELECT_ALBUM_ART, 508, 208);
    Marvin_PANEL_SONG_SELECT_ALBUM_ART->fn->setScheme(Marvin_PANEL_SONG_SELECT_ALBUM_ART, &SCHEME_FILL_ZINC_900);
    root3->fn->addChild(root3, (leWidget*)Marvin_PANEL_SONG_SELECT_ALBUM_ART);

    Marvin_IMAGE_ALBUM_ART = leImageWidget_New();
    Marvin_IMAGE_ALBUM_ART->fn->setPosition(Marvin_IMAGE_ALBUM_ART, 0, 0);
    Marvin_IMAGE_ALBUM_ART->fn->setSize(Marvin_IMAGE_ALBUM_ART, 508, 208);
    Marvin_IMAGE_ALBUM_ART->fn->setBackgroundType(Marvin_IMAGE_ALBUM_ART, LE_WIDGET_BACKGROUND_NONE);
    Marvin_IMAGE_ALBUM_ART->fn->setBorderType(Marvin_IMAGE_ALBUM_ART, LE_WIDGET_BORDER_NONE);
    Marvin_PANEL_SONG_SELECT_ALBUM_ART->fn->addChild(Marvin_PANEL_SONG_SELECT_ALBUM_ART, (leWidget*)Marvin_IMAGE_ALBUM_ART);

    Marvin_PANEL_ALBUM_ART_OVERLAY = leWidget_New();
    Marvin_PANEL_ALBUM_ART_OVERLAY->fn->setPosition(Marvin_PANEL_ALBUM_ART_OVERLAY, 0, 0);
    Marvin_PANEL_ALBUM_ART_OVERLAY->fn->setSize(Marvin_PANEL_ALBUM_ART_OVERLAY, 508, 208);
    Marvin_PANEL_ALBUM_ART_OVERLAY->fn->setScheme(Marvin_PANEL_ALBUM_ART_OVERLAY, &SCHEME_FILL_ZINC_900);
    Marvin_PANEL_ALBUM_ART_OVERLAY->fn->setBackgroundType(Marvin_PANEL_ALBUM_ART_OVERLAY, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_SONG_SELECT_ALBUM_ART->fn->addChild(Marvin_PANEL_SONG_SELECT_ALBUM_ART, (leWidget*)Marvin_PANEL_ALBUM_ART_OVERLAY);

    Marvin_LABEL_SONG_SELECT_SongTier = leLabelWidget_New();
    Marvin_LABEL_SONG_SELECT_SongTier->fn->setPosition(Marvin_LABEL_SONG_SELECT_SongTier, 16, 122);
    Marvin_LABEL_SONG_SELECT_SongTier->fn->setSize(Marvin_LABEL_SONG_SELECT_SongTier, 470, 16);
    Marvin_LABEL_SONG_SELECT_SongTier->fn->setScheme(Marvin_LABEL_SONG_SELECT_SongTier, &SCHEME_TEXT_ZINC_300);
    Marvin_LABEL_SONG_SELECT_SongTier->fn->setBackgroundType(Marvin_LABEL_SONG_SELECT_SongTier, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_SONG_SELECT_SongTier->fn->setVAlignment(Marvin_LABEL_SONG_SELECT_SongTier, LE_VALIGN_TOP);
    Marvin_LABEL_SONG_SELECT_SongTier->fn->setMargins(Marvin_LABEL_SONG_SELECT_SongTier, 0, 0, 0, 0);
    Marvin_LABEL_SONG_SELECT_SongTier->fn->setString(Marvin_LABEL_SONG_SELECT_SongTier, (leString*)&string_SONG_SELECT_SongTier);
    Marvin_PANEL_SONG_SELECT_ALBUM_ART->fn->addChild(Marvin_PANEL_SONG_SELECT_ALBUM_ART, (leWidget*)Marvin_LABEL_SONG_SELECT_SongTier);

    Marvin_LABEL_SONG_SELECT_SongTitle = leLabelWidget_New();
    Marvin_LABEL_SONG_SELECT_SongTitle->fn->setPosition(Marvin_LABEL_SONG_SELECT_SongTitle, 16, 143);
    Marvin_LABEL_SONG_SELECT_SongTitle->fn->setSize(Marvin_LABEL_SONG_SELECT_SongTitle, 470, 30);
    Marvin_LABEL_SONG_SELECT_SongTitle->fn->setScheme(Marvin_LABEL_SONG_SELECT_SongTitle, &SCHEME_TEXT_WHITE);
    Marvin_LABEL_SONG_SELECT_SongTitle->fn->setBackgroundType(Marvin_LABEL_SONG_SELECT_SongTitle, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_SONG_SELECT_SongTitle->fn->setVAlignment(Marvin_LABEL_SONG_SELECT_SongTitle, LE_VALIGN_TOP);
    Marvin_LABEL_SONG_SELECT_SongTitle->fn->setMargins(Marvin_LABEL_SONG_SELECT_SongTitle, 0, 0, 0, 0);
    Marvin_LABEL_SONG_SELECT_SongTitle->fn->setString(Marvin_LABEL_SONG_SELECT_SongTitle, (leString*)&string_SONG_SELECT_SongTitle);
    Marvin_PANEL_SONG_SELECT_ALBUM_ART->fn->addChild(Marvin_PANEL_SONG_SELECT_ALBUM_ART, (leWidget*)Marvin_LABEL_SONG_SELECT_SongTitle);

    Marvin_LABEL_SONG_SELECT_Artist = leLabelWidget_New();
    Marvin_LABEL_SONG_SELECT_Artist->fn->setPosition(Marvin_LABEL_SONG_SELECT_Artist, 16, 172);
    Marvin_LABEL_SONG_SELECT_Artist->fn->setSize(Marvin_LABEL_SONG_SELECT_Artist, 470, 19);
    Marvin_LABEL_SONG_SELECT_Artist->fn->setScheme(Marvin_LABEL_SONG_SELECT_Artist, &SCHEME_TEXT_ZINC_300);
    Marvin_LABEL_SONG_SELECT_Artist->fn->setBackgroundType(Marvin_LABEL_SONG_SELECT_Artist, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_SONG_SELECT_Artist->fn->setVAlignment(Marvin_LABEL_SONG_SELECT_Artist, LE_VALIGN_TOP);
    Marvin_LABEL_SONG_SELECT_Artist->fn->setMargins(Marvin_LABEL_SONG_SELECT_Artist, 0, 0, 0, 0);
    Marvin_LABEL_SONG_SELECT_Artist->fn->setString(Marvin_LABEL_SONG_SELECT_Artist, (leString*)&string_SONG_SELECT_SongArtist);
    Marvin_PANEL_SONG_SELECT_ALBUM_ART->fn->addChild(Marvin_PANEL_SONG_SELECT_ALBUM_ART, (leWidget*)Marvin_LABEL_SONG_SELECT_Artist);

    leAddRootWidget(root3, 3);
    leSetLayerColorMode(3, LE_COLOR_MODE_RGBA_8888);

    // layer 4
    root4 = leWidget_New();
    root4->fn->setSize(root4, LE_DEFAULT_SCREEN_WIDTH, LE_DEFAULT_SCREEN_HEIGHT);
    root4->fn->setBackgroundType(root4, LE_WIDGET_BACKGROUND_NONE);
    root4->fn->setMargins(root4, 0, 0, 0, 0);
    root4->flags |= LE_WIDGET_IGNOREEVENTS;
    root4->flags |= LE_WIDGET_IGNOREPICK;

    Marvin_PANEL_WIIMOTES = leWidget_New();
    Marvin_PANEL_WIIMOTES->fn->setPosition(Marvin_PANEL_WIIMOTES, 0, 0);
    Marvin_PANEL_WIIMOTES->fn->setSize(Marvin_PANEL_WIIMOTES, 1280, 800);
    Marvin_PANEL_WIIMOTES->fn->setScheme(Marvin_PANEL_WIIMOTES, &SCHEME_BACKGROUND);
    root4->fn->addChild(root4, (leWidget*)Marvin_PANEL_WIIMOTES);

    Marvin_PANEL_WIIMOTES_BOTTOM = leWidget_New();
    Marvin_PANEL_WIIMOTES_BOTTOM->fn->setPosition(Marvin_PANEL_WIIMOTES_BOTTOM, 12, 65);
    Marvin_PANEL_WIIMOTES_BOTTOM->fn->setSize(Marvin_PANEL_WIIMOTES_BOTTOM, 1256, 728);
    Marvin_PANEL_WIIMOTES_BOTTOM->fn->setBackgroundType(Marvin_PANEL_WIIMOTES_BOTTOM, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_WIIMOTES->fn->addChild(Marvin_PANEL_WIIMOTES, (leWidget*)Marvin_PANEL_WIIMOTES_BOTTOM);

    Marvin_PANEL_WIIMOTES_ROBOT = leWidget_New();
    Marvin_PANEL_WIIMOTES_ROBOT->fn->setPosition(Marvin_PANEL_WIIMOTES_ROBOT, 0, 12);
    Marvin_PANEL_WIIMOTES_ROBOT->fn->setSize(Marvin_PANEL_WIIMOTES_ROBOT, 620, 716);
    Marvin_PANEL_WIIMOTES_ROBOT->fn->setScheme(Marvin_PANEL_WIIMOTES_ROBOT, &SCHEME_FILL_ZINC_900);
    Marvin_PANEL_WIIMOTES_ROBOT->fn->setBorderType(Marvin_PANEL_WIIMOTES_ROBOT, LE_WIDGET_BORDER_LINE);
    Marvin_PANEL_WIIMOTES_BOTTOM->fn->addChild(Marvin_PANEL_WIIMOTES_BOTTOM, (leWidget*)Marvin_PANEL_WIIMOTES_ROBOT);

    Marvin_LABEL_WIIMOTES_ROBOT_RobotLemmy = leLabelWidget_New();
    Marvin_LABEL_WIIMOTES_ROBOT_RobotLemmy->fn->setPosition(Marvin_LABEL_WIIMOTES_ROBOT_RobotLemmy, 17, 19);
    Marvin_LABEL_WIIMOTES_ROBOT_RobotLemmy->fn->setSize(Marvin_LABEL_WIIMOTES_ROBOT_RobotLemmy, 110, 20);
    Marvin_LABEL_WIIMOTES_ROBOT_RobotLemmy->fn->setScheme(Marvin_LABEL_WIIMOTES_ROBOT_RobotLemmy, &SCHEME_TEXT_ZINC_400);
    Marvin_LABEL_WIIMOTES_ROBOT_RobotLemmy->fn->setBackgroundType(Marvin_LABEL_WIIMOTES_ROBOT_RobotLemmy, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_WIIMOTES_ROBOT_RobotLemmy->fn->setVAlignment(Marvin_LABEL_WIIMOTES_ROBOT_RobotLemmy, LE_VALIGN_TOP);
    Marvin_LABEL_WIIMOTES_ROBOT_RobotLemmy->fn->setMargins(Marvin_LABEL_WIIMOTES_ROBOT_RobotLemmy, 0, 0, 0, 0);
    Marvin_PANEL_WIIMOTES_ROBOT->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT, (leWidget*)Marvin_LABEL_WIIMOTES_ROBOT_RobotLemmy);

    Marvin_panel_Button_37 = leWidget_New();
    Marvin_panel_Button_37->fn->setPosition(Marvin_panel_Button_37, 516, 17);
    Marvin_panel_Button_37->fn->setSize(Marvin_panel_Button_37, 87, 24);
    Marvin_panel_Button_37->fn->setScheme(Marvin_panel_Button_37, &SCHEME_FILL_GREEN_800);
    Marvin_PANEL_WIIMOTES_ROBOT->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT, (leWidget*)Marvin_panel_Button_37);

    Marvin_panel_Text_1_1 = leWidget_New();
    Marvin_panel_Text_1_1->fn->setPosition(Marvin_panel_Text_1_1, 12, 9);
    Marvin_panel_Text_1_1->fn->setSize(Marvin_panel_Text_1_1, 6, 6);
    Marvin_panel_Text_1_1->fn->setScheme(Marvin_panel_Text_1_1, &SCHEME_FILL_GREEN_400);
    Marvin_panel_Button_37->fn->addChild(Marvin_panel_Button_37, (leWidget*)Marvin_panel_Text_1_1);

    Marvin_label_ENABLED_0 = leLabelWidget_New();
    Marvin_label_ENABLED_0->fn->setPosition(Marvin_label_ENABLED_0, 24, 4);
    Marvin_label_ENABLED_0->fn->setSize(Marvin_label_ENABLED_0, 51, 16);
    Marvin_label_ENABLED_0->fn->setScheme(Marvin_label_ENABLED_0, &SCHEME_TEXT_GREEN_300);
    Marvin_label_ENABLED_0->fn->setBackgroundType(Marvin_label_ENABLED_0, LE_WIDGET_BACKGROUND_NONE);
    Marvin_label_ENABLED_0->fn->setHAlignment(Marvin_label_ENABLED_0, LE_HALIGN_CENTER);
    Marvin_label_ENABLED_0->fn->setVAlignment(Marvin_label_ENABLED_0, LE_VALIGN_TOP);
    Marvin_label_ENABLED_0->fn->setMargins(Marvin_label_ENABLED_0, 0, 0, 0, 0);
    Marvin_label_ENABLED_0->fn->setString(Marvin_label_ENABLED_0, (leString*)&string_figmaStr_ENABLED);
    Marvin_panel_Button_37->fn->addChild(Marvin_panel_Button_37, (leWidget*)Marvin_label_ENABLED_0);

    Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION = leWidget_New();
    Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->setPosition(Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, 17, 53);
    Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->setSize(Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, 586, 272);
    Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->setScheme(Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, &SCHEME_FILL_ZINC_800);
    Marvin_PANEL_WIIMOTES_ROBOT->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT, (leWidget*)Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION);

    Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION = leLabelWidget_New();
    Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->setPosition(Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, 12, 12);
    Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->setSize(Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, 134, 16);
    Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->setScheme(Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->setBackgroundType(Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->setVAlignment(Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, LE_VALIGN_TOP);
    Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->setMargins(Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, 0, 0, 0, 0);
    Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->setString(Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, (leString*)&string_figmaStr_GUITAR_EXTENSION);
    Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, (leWidget*)Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION);

    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_GREEN = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_GREEN->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_GREEN, 12, 38);
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_GREEN->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_GREEN, 107, 64);
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_GREEN->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_GREEN, &SCHEME_GUITAR_FRET_GREEN);
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_GREEN->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_GREEN, LE_WIDGET_BORDER_NONE);
    Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_FRET_GREEN);

    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_RED = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_RED->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_RED, 127, 38);
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_RED->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_RED, 107, 64);
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_RED->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_RED, &SCHEME_GUITAR_FRET_RED);
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_RED->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_RED, LE_WIDGET_BORDER_NONE);
    Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_FRET_RED);

    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_YELLOW = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_YELLOW->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_YELLOW, 241, 38);
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_YELLOW->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_YELLOW, 107, 64);
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_YELLOW->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_YELLOW, &SCHEME_GUITAR_FRET_YELLOW);
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_YELLOW->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_YELLOW, LE_WIDGET_BORDER_NONE);
    Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_FRET_YELLOW);

    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_BLUE = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_BLUE->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_BLUE, 356, 38);
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_BLUE->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_BLUE, 107, 64);
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_BLUE->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_BLUE, &SCHEME_GUITAR_FRET_BLUE);
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_BLUE->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_BLUE, LE_WIDGET_BORDER_NONE);
    Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_FRET_BLUE);

    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_ORANGE = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_ORANGE->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_ORANGE, 470, 38);
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_ORANGE->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_ORANGE, 107, 64);
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_ORANGE->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_ORANGE, &SCHEME_GUITAR_FRET_ORANGE);
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_ORANGE->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_FRET_ORANGE, LE_WIDGET_BORDER_NONE);
    Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_FRET_ORANGE);

    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP, 12, 112);
    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP, 180, 71);
    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP, &SCHEME_FILL_ZINC_700);
    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP->fn->setString(Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP, (leString*)&string_figmaStr__UP);
    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP->fn->setPressedImage(Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP, (leImage*)&figmaImg_Icon_0_1);
    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP->fn->setReleasedImage(Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP, (leImage*)&figmaImg_Icon_0_1);
    Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP);

    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN, 12, 189);
    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN, 180, 71);
    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN, &SCHEME_FILL_ZINC_700);
    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN->fn->setString(Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN, (leString*)&string_figmaStr__DN);
    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN->fn->setPressedImage(Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN, (leImage*)&figmaImg_Icon_1_1);
    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN->fn->setReleasedImage(Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN, (leImage*)&figmaImg_Icon_1_1);
    Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN);

    Marvin_panel_Container_15_1 = leWidget_New();
    Marvin_panel_Container_15_1->fn->setPosition(Marvin_panel_Container_15_1, 212, 112);
    Marvin_panel_Container_15_1->fn->setSize(Marvin_panel_Container_15_1, 273, 148);
    Marvin_panel_Container_15_1->fn->setScheme(Marvin_panel_Container_15_1, &SCHEME_BACKGROUND);
    Marvin_panel_Container_15_1->fn->setBackgroundType(Marvin_panel_Container_15_1, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, (leWidget*)Marvin_panel_Container_15_1);

    Marvin_label_WHAMMY_1 = leLabelWidget_New();
    Marvin_label_WHAMMY_1->fn->setPosition(Marvin_label_WHAMMY_1, 110, 23);
    Marvin_label_WHAMMY_1->fn->setSize(Marvin_label_WHAMMY_1, 54, 20);
    Marvin_label_WHAMMY_1->fn->setScheme(Marvin_label_WHAMMY_1, &SCHEME_TEXT_ZINC_500);
    Marvin_label_WHAMMY_1->fn->setBackgroundType(Marvin_label_WHAMMY_1, LE_WIDGET_BACKGROUND_NONE);
    Marvin_label_WHAMMY_1->fn->setVAlignment(Marvin_label_WHAMMY_1, LE_VALIGN_TOP);
    Marvin_label_WHAMMY_1->fn->setMargins(Marvin_label_WHAMMY_1, 0, 0, 0, 0);
    Marvin_label_WHAMMY_1->fn->setString(Marvin_label_WHAMMY_1, (leString*)&string_figmaStr_WHAMMY);
    Marvin_panel_Container_15_1->fn->addChild(Marvin_panel_Container_15_1, (leWidget*)Marvin_label_WHAMMY_1);

    Marvin_panel_WhammySlider_1 = leWidget_New();
    Marvin_panel_WhammySlider_1->fn->setPosition(Marvin_panel_WhammySlider_1, 0, 0);
    Marvin_panel_WhammySlider_1->fn->setSize(Marvin_panel_WhammySlider_1, 297, 148);
    Marvin_panel_WhammySlider_1->fn->setScheme(Marvin_panel_WhammySlider_1, &SCHEME_BACKGROUND);
    Marvin_panel_WhammySlider_1->fn->setBackgroundType(Marvin_panel_WhammySlider_1, LE_WIDGET_BACKGROUND_NONE);
    Marvin_panel_Container_15_1->fn->addChild(Marvin_panel_Container_15_1, (leWidget*)Marvin_panel_WhammySlider_1);

    Marvin_panel_Container_16_1 = leWidget_New();
    Marvin_panel_Container_16_1->fn->setPosition(Marvin_panel_Container_16_1, 0, 42);
    Marvin_panel_Container_16_1->fn->setSize(Marvin_panel_Container_16_1, 273, 64);
    Marvin_panel_Container_16_1->fn->setScheme(Marvin_panel_Container_16_1, &SCHEME_FILL_ZINC_800);
    Marvin_panel_WhammySlider_1->fn->addChild(Marvin_panel_WhammySlider_1, (leWidget*)Marvin_panel_Container_16_1);

    Marvin_panel_Container_17_1 = leWidget_New();
    Marvin_panel_Container_17_1->fn->setPosition(Marvin_panel_Container_17_1, 137, 0);
    Marvin_panel_Container_17_1->fn->setSize(Marvin_panel_Container_17_1, 1, 64);
    Marvin_panel_Container_17_1->fn->setScheme(Marvin_panel_Container_17_1, &SCHEME_FILL_ZINC_600);
    Marvin_panel_Container_16_1->fn->addChild(Marvin_panel_Container_16_1, (leWidget*)Marvin_panel_Container_17_1);

    Marvin_panel_Container_18_1 = leWidget_New();
    Marvin_panel_Container_18_1->fn->setPosition(Marvin_panel_Container_18_1, 137, 0);
    Marvin_panel_Container_18_1->fn->setSize(Marvin_panel_Container_18_1, 1, 64);
    Marvin_panel_Container_18_1->fn->setScheme(Marvin_panel_Container_18_1, &SCHEME_FILL_PURPLE_600);
    Marvin_panel_Container_16_1->fn->addChild(Marvin_panel_Container_16_1, (leWidget*)Marvin_panel_Container_18_1);

    Marvin_panel_Container_19_1 = leWidget_New();
    Marvin_panel_Container_19_1->fn->setPosition(Marvin_panel_Container_19_1, 131, 12);
    Marvin_panel_Container_19_1->fn->setSize(Marvin_panel_Container_19_1, 16, 40);
    Marvin_panel_Container_19_1->fn->setScheme(Marvin_panel_Container_19_1, &SCHEME_FILL_PURPLE_200);
    Marvin_panel_Container_16_1->fn->addChild(Marvin_panel_Container_16_1, (leWidget*)Marvin_panel_Container_19_1);

    Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_PLUS = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_PLUS->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_PLUS, 505, 112);
    Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_PLUS->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_PLUS, 71, 71);
    Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_PLUS->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_PLUS, &SCHEME_FILL_ZINC_700);
    Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_PLUS->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_PLUS, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_PLUS->fn->setString(Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_PLUS, (leString*)&string_figmaStr___3);
    Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_PLUS);

    Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_MINUS = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_MINUS->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_MINUS, 505, 189);
    Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_MINUS->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_MINUS, 71, 71);
    Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_MINUS->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_MINUS, &SCHEME_FILL_ZINC_700);
    Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_MINUS->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_MINUS, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_MINUS->fn->setString(Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_MINUS, (leString*)&string_figmaStr___1);
    Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_MINUS);

    Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE = leWidget_New();
    Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE->fn->setPosition(Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE, 17, 337);
    Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE->fn->setSize(Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE, 588, 274);
    Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE->fn->setScheme(Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE, &SCHEME_FILL_ZINC_800);
    Marvin_PANEL_WIIMOTES_ROBOT->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT, (leWidget*)Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE);

    Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE = leLabelWidget_New();
    Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE->fn->setPosition(Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE, 12, 12);
    Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE->fn->setSize(Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE, 58, 16);
    Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE->fn->setScheme(Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE, &SCHEME_TEXT_ZINC_500);
    Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE->fn->setBackgroundType(Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE, LE_WIDGET_BACKGROUND_NONE);
    Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE->fn->setVAlignment(Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE, LE_VALIGN_TOP);
    Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE->fn->setMargins(Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE, 0, 0, 0, 0);
    Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE->fn->setString(Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE, (leString*)&string_figmaStr_WIIMOTE);
    Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE, (leWidget*)Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE);

    Marvin_BUTTON_WIIMOTES_ROBOT_UP = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_UP->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_UP, 89, 35);
    Marvin_BUTTON_WIIMOTES_ROBOT_UP->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_UP, 71, 71);
    Marvin_BUTTON_WIIMOTES_ROBOT_UP->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_UP, &SCHEME_FILL_ZINC_700);
    Marvin_BUTTON_WIIMOTES_ROBOT_UP->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_UP, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_WIIMOTES_ROBOT_UP->fn->setString(Marvin_BUTTON_WIIMOTES_ROBOT_UP, (leString*)&string_figmaStr___1_0);
    Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_UP);

    Marvin_BUTTON_WIIMOTES_ROBOT_LEFT = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_LEFT->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_LEFT, 12, 113);
    Marvin_BUTTON_WIIMOTES_ROBOT_LEFT->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_LEFT, 71, 71);
    Marvin_BUTTON_WIIMOTES_ROBOT_LEFT->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_LEFT, &SCHEME_FILL_ZINC_700);
    Marvin_BUTTON_WIIMOTES_ROBOT_LEFT->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_LEFT, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_WIIMOTES_ROBOT_LEFT->fn->setString(Marvin_BUTTON_WIIMOTES_ROBOT_LEFT, (leString*)&string_figmaStr___2_0);
    Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_LEFT);

    Marvin_BUTTON_WIIMOTES_ROBOT_RIGHT = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_RIGHT->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_RIGHT, 166, 113);
    Marvin_BUTTON_WIIMOTES_ROBOT_RIGHT->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_RIGHT, 71, 71);
    Marvin_BUTTON_WIIMOTES_ROBOT_RIGHT->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_RIGHT, &SCHEME_FILL_ZINC_700);
    Marvin_BUTTON_WIIMOTES_ROBOT_RIGHT->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_RIGHT, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_WIIMOTES_ROBOT_RIGHT->fn->setString(Marvin_BUTTON_WIIMOTES_ROBOT_RIGHT, (leString*)&string_figmaStr___3_0);
    Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_RIGHT);

    Marvin_BUTTON_WIIMOTES_ROBOT_DOWN = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_DOWN->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_DOWN, 89, 190);
    Marvin_BUTTON_WIIMOTES_ROBOT_DOWN->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_DOWN, 71, 71);
    Marvin_BUTTON_WIIMOTES_ROBOT_DOWN->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_DOWN, &SCHEME_FILL_ZINC_700);
    Marvin_BUTTON_WIIMOTES_ROBOT_DOWN->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_DOWN, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_WIIMOTES_ROBOT_DOWN->fn->setString(Marvin_BUTTON_WIIMOTES_ROBOT_DOWN, (leString*)&string_figmaStr___4);
    Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_DOWN);

    Marvin_BUTTON_WIIMOTES_ROBOT_HOME = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_HOME->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_HOME, 253, 32);
    Marvin_BUTTON_WIIMOTES_ROBOT_HOME->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_HOME, 150, 71);
    Marvin_BUTTON_WIIMOTES_ROBOT_HOME->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_HOME, &SCHEME_FILL_ZINC_700);
    Marvin_BUTTON_WIIMOTES_ROBOT_HOME->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_HOME, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_WIIMOTES_ROBOT_HOME->fn->setString(Marvin_BUTTON_WIIMOTES_ROBOT_HOME, (leString*)&string_figmaStr_HOME);
    Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_HOME);

    Marvin_BUTTON_WIIMOTES_ROBOT_A = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_A->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_A, 253, 111);
    Marvin_BUTTON_WIIMOTES_ROBOT_A->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_A, 71, 71);
    Marvin_BUTTON_WIIMOTES_ROBOT_A->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_A, &SCHEME_FILL_ZINC_700);
    Marvin_BUTTON_WIIMOTES_ROBOT_A->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_A, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_WIIMOTES_ROBOT_A->fn->setString(Marvin_BUTTON_WIIMOTES_ROBOT_A, (leString*)&string_figmaStr_A);
    Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_A);

    Marvin_BUTTON_WIIMOTES_ROBOT_B = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_B->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_B, 332, 111);
    Marvin_BUTTON_WIIMOTES_ROBOT_B->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_B, 71, 71);
    Marvin_BUTTON_WIIMOTES_ROBOT_B->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_B, &SCHEME_FILL_ZINC_700);
    Marvin_BUTTON_WIIMOTES_ROBOT_B->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_B, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_WIIMOTES_ROBOT_B->fn->setString(Marvin_BUTTON_WIIMOTES_ROBOT_B, (leString*)&string_figmaStr_B_0);
    Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_B);

    Marvin_BUTTON_WIIMOTES_ROBOT_ONE = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_ONE->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_ONE, 253, 190);
    Marvin_BUTTON_WIIMOTES_ROBOT_ONE->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_ONE, 71, 71);
    Marvin_BUTTON_WIIMOTES_ROBOT_ONE->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_ONE, &SCHEME_FILL_ZINC_700);
    Marvin_BUTTON_WIIMOTES_ROBOT_ONE->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_ONE, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_WIIMOTES_ROBOT_ONE->fn->setString(Marvin_BUTTON_WIIMOTES_ROBOT_ONE, (leString*)&string_figmaStr_1);
    Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_ONE);

    Marvin_BUTTON_WIIMOTES_ROBOT_TWO = leButtonWidget_New();
    Marvin_BUTTON_WIIMOTES_ROBOT_TWO->fn->setPosition(Marvin_BUTTON_WIIMOTES_ROBOT_TWO, 332, 190);
    Marvin_BUTTON_WIIMOTES_ROBOT_TWO->fn->setSize(Marvin_BUTTON_WIIMOTES_ROBOT_TWO, 71, 71);
    Marvin_BUTTON_WIIMOTES_ROBOT_TWO->fn->setScheme(Marvin_BUTTON_WIIMOTES_ROBOT_TWO, &SCHEME_FILL_ZINC_700);
    Marvin_BUTTON_WIIMOTES_ROBOT_TWO->fn->setBorderType(Marvin_BUTTON_WIIMOTES_ROBOT_TWO, LE_WIDGET_BORDER_NONE);
    Marvin_BUTTON_WIIMOTES_ROBOT_TWO->fn->setString(Marvin_BUTTON_WIIMOTES_ROBOT_TWO, (leString*)&string_figmaStr_2);
    Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE, (leWidget*)Marvin_BUTTON_WIIMOTES_ROBOT_TWO);

    Marvin_panel_TiltControl_1 = leWidget_New();
    Marvin_panel_TiltControl_1->fn->setPosition(Marvin_panel_TiltControl_1, 419, 68);
    Marvin_panel_TiltControl_1->fn->setSize(Marvin_panel_TiltControl_1, 157, 157);
    Marvin_panel_TiltControl_1->fn->setScheme(Marvin_panel_TiltControl_1, &SCHEME_BACKGROUND);
    Marvin_panel_TiltControl_1->fn->setBackgroundType(Marvin_panel_TiltControl_1, LE_WIDGET_BACKGROUND_NONE);
    Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE->fn->addChild(Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE, (leWidget*)Marvin_panel_TiltControl_1);

    Marvin_image_TiltControl_1 = leImageWidget_New();
    Marvin_image_TiltControl_1->fn->setPosition(Marvin_image_TiltControl_1, 0, 0);
    Marvin_image_TiltControl_1->fn->setSize(Marvin_image_TiltControl_1, 157, 157);
    Marvin_image_TiltControl_1->fn->setScheme(Marvin_image_TiltControl_1, &SCHEME_BACKGROUND);
    Marvin_image_TiltControl_1->fn->setBackgroundType(Marvin_image_TiltControl_1, LE_WIDGET_BACKGROUND_NONE);
    Marvin_image_TiltControl_1->fn->setBorderType(Marvin_image_TiltControl_1, LE_WIDGET_BORDER_NONE);
    Marvin_image_TiltControl_1->fn->setImage(Marvin_image_TiltControl_1, (leImage*)&figmaImg_TiltControl);
    Marvin_panel_TiltControl_1->fn->addChild(Marvin_panel_TiltControl_1, (leWidget*)Marvin_image_TiltControl_1);

    Marvin_label_TILT_1 = leLabelWidget_New();
    Marvin_label_TILT_1->fn->setPosition(Marvin_label_TILT_1, 4, 1);
    Marvin_label_TILT_1->fn->setSize(Marvin_label_TILT_1, 28, 16);
    Marvin_label_TILT_1->fn->setScheme(Marvin_label_TILT_1, &SCHEME_TEXT_ZINC_500);
    Marvin_label_TILT_1->fn->setBackgroundType(Marvin_label_TILT_1, LE_WIDGET_BACKGROUND_NONE);
    Marvin_label_TILT_1->fn->setVAlignment(Marvin_label_TILT_1, LE_VALIGN_TOP);
    Marvin_label_TILT_1->fn->setMargins(Marvin_label_TILT_1, 0, 0, 0, 0);
    Marvin_label_TILT_1->fn->setString(Marvin_label_TILT_1, (leString*)&string_figmaStr_TILT);
    Marvin_panel_TiltControl_1->fn->addChild(Marvin_panel_TiltControl_1, (leWidget*)Marvin_label_TILT_1);

    Marvin_PANEL_WIIMOTES_HUMAN = leWidget_New();
    Marvin_PANEL_WIIMOTES_HUMAN->fn->setPosition(Marvin_PANEL_WIIMOTES_HUMAN, 636, 12);
    Marvin_PANEL_WIIMOTES_HUMAN->fn->setSize(Marvin_PANEL_WIIMOTES_HUMAN, 620, 716);
    Marvin_PANEL_WIIMOTES_HUMAN->fn->setScheme(Marvin_PANEL_WIIMOTES_HUMAN, &SCHEME_FILL_ZINC_900);
    Marvin_PANEL_WIIMOTES_HUMAN->fn->setBorderType(Marvin_PANEL_WIIMOTES_HUMAN, LE_WIDGET_BORDER_LINE);
    Marvin_PANEL_WIIMOTES_BOTTOM->fn->addChild(Marvin_PANEL_WIIMOTES_BOTTOM, (leWidget*)Marvin_PANEL_WIIMOTES_HUMAN);

    leAddRootWidget(root4, 4);
    leSetLayerColorMode(4, LE_COLOR_MODE_RGB_565);

    // layer 5
    root5 = leWidget_New();
    root5->fn->setSize(root5, 1060, 560);
    root5->fn->setBackgroundType(root5, LE_WIDGET_BACKGROUND_NONE);
    root5->fn->setMargins(root5, 0, 0, 0, 0);
    root5->flags |= LE_WIDGET_IGNOREEVENTS;
    root5->flags |= LE_WIDGET_IGNOREPICK;

    Marvin_PANEL_KEYBOARD = leWidget_New();
    Marvin_PANEL_KEYBOARD->fn->setPosition(Marvin_PANEL_KEYBOARD, 0, 0);
    Marvin_PANEL_KEYBOARD->fn->setSize(Marvin_PANEL_KEYBOARD, 1060, 560);
    Marvin_PANEL_KEYBOARD->fn->setScheme(Marvin_PANEL_KEYBOARD, &SCHEME_FILL_ZINC_900);
    root5->fn->addChild(root5, (leWidget*)Marvin_PANEL_KEYBOARD);

    leAddRootWidget(root5, 5);
    leSetLayerColorMode(5, LE_COLOR_MODE_RGB_565);

    // layer 6
    root6 = leWidget_New();
    root6->fn->setSize(root6, LE_DEFAULT_SCREEN_WIDTH, LE_DEFAULT_SCREEN_HEIGHT);
    root6->fn->setBackgroundType(root6, LE_WIDGET_BACKGROUND_NONE);
    root6->fn->setMargins(root6, 0, 0, 0, 0);
    root6->flags |= LE_WIDGET_IGNOREEVENTS;
    root6->flags |= LE_WIDGET_IGNOREPICK;

    Marvin_PANEL_BUS = leWidget_New();
    Marvin_PANEL_BUS->fn->setPosition(Marvin_PANEL_BUS, 0, 0);
    Marvin_PANEL_BUS->fn->setSize(Marvin_PANEL_BUS, 1280, 800);
    Marvin_PANEL_BUS->fn->setScheme(Marvin_PANEL_BUS, &SCHEME_BACKGROUND);
    root6->fn->addChild(root6, (leWidget*)Marvin_PANEL_BUS);

    leAddRootWidget(root6, 6);
    leSetLayerColorMode(6, LE_COLOR_MODE_RGB_565);

    showing = LE_TRUE;

    return LE_SUCCESS;
}

void screenUpdate_Marvin(void)
{
    root0->fn->setSize(root0, root0->rect.width, root0->rect.height);
    root1->fn->setSize(root1, root1->rect.width, root1->rect.height);
    root2->fn->setSize(root2, root2->rect.width, root2->rect.height);
    root3->fn->setSize(root3, root3->rect.width, root3->rect.height);
    root4->fn->setSize(root4, root4->rect.width, root4->rect.height);
    root5->fn->setSize(root5, root5->rect.width, root5->rect.height);
    root6->fn->setSize(root6, root6->rect.width, root6->rect.height);
}

void screenHide_Marvin(void)
{

    leRemoveRootWidget(root0, 0);
    leWidget_Delete(root0);
    root0 = NULL;

    Marvin_PANEL_DASHBOARD = NULL;
    Marvin_PANEL_DASHBOARD_BOTTOM = NULL;
    Marvin_PANEL_DASHBOARD_ROBOT = NULL;
    Marvin_PANEL_DASHBOARD_GAMEPLAY = NULL;
    Marvin_PANEL_DASHBOARD_HUMAN = NULL;
    Marvin_IMAGE_DASHBOARD_ROBOT_PLAYER = NULL;
    Marvin_LABEL_DASHBOARD_ROBOT_Name = NULL;
    Marvin_LABEL_DASHBOARD_ROBOT_RobotPlayer = NULL;
    Marvin_PANEL_DASHBOARD_ROBOT_STATE = NULL;
    Marvin_LABEL_DASHBOARD_ROBOT_SCORE = NULL;
    Marvin_BUTTON_DASHBOARD_ROBOT_1X = NULL;
    Marvin_BUTTON_DASHBOARD_ROBOT_2X = NULL;
    Marvin_BUTTON_DASHBOARD_ROBOT_3X = NULL;
    Marvin_BUTTON_DASHBOARD_ROBOT_4X = NULL;
    Marvin_LABEL_DASHBOARD_ROBOT_Score = NULL;
    Marvin_LABEL_DASHBOARD_ROBOT_STREAK = NULL;
    Marvin_LABEL_DASHBOARD_ROBOT_Streak = NULL;
    Marvin_LABEL_DASHBOARD_ROBOT_ACCURACY = NULL;
    Marvin_LABEL_DASHBOARD_ROBOT_Accuracy = NULL;
    Marvin_PROGRESSBAR_DASHBOARD_ROBOT_Accuracy = NULL;
    Marvin_LABEL_DASHBOARD_ROBOT_STAR_POWER = NULL;
    Marvin_LABEL_DASHBOARD_ROBOT_StarPower = NULL;
    Marvin_PROGRESSBAR_DASHBOARD_ROBOT_StarPower = NULL;
    Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_1 = NULL;
    Marvin_LABEL_DASHBOARD_ROBOT_FRET_ACTIVITY = NULL;
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_GREEN = NULL;
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_RED = NULL;
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_YELLOW = NULL;
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_BLUE = NULL;
    Marvin_BUTTON_DASHBOARD_ROBOT_FRET_ORANGE = NULL;
    Marvin_LABEL_DASHBOARD_ROBOT_STRUM_BAR = NULL;
    Marvin_panel_Container_36_0 = NULL;
    Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_2 = NULL;
    Marvin_LABEL_DASHBOARD_ROBOT_DETECTOR = NULL;
    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_NN = NULL;
    Marvin_BUTTON_DASHBOARD_ROBOT_DETECTOR_CV = NULL;
    Marvin_PANEL_DASHBOARD_ROBOT_DIVIDER_3 = NULL;
    Marvin_PANEL_DASHBOARD_ROBOT_BORDER = NULL;
    Marvin_LABEL_DASHBOARD_ROBOT_ACTUATORS = NULL;
    Marvin_BUTTON_DASHBOARD_ROBOT_ACTUATOR_GUITAR_1 = NULL;
    Marvin_PANEL_DASHBOARD_ROBOT_STATE_LED = NULL;
    Marvin_LABEL_DASHBOARD_ROBOT_RobotState = NULL;
    Marvin_label_IDLE_0_0 = NULL;
    Marvin_PANEL_DASHBOARD_VIDEO = NULL;
    Marvin_PANEL_DASHBOARD_SONG = NULL;
    Marvin_PANEL_DASHBOARD_TEST_BAR_WHITE = NULL;
    Marvin_PANEL_DASHBOARD_TEST_BAR_YELLOW = NULL;
    Marvin_PANEL_DASHBOARD_TEST_BAR_CYAN = NULL;
    Marvin_PANEL_DASHBOARD_TEST_BAR_GREEN = NULL;
    Marvin_PANEL_DASHBOARD_TEST_BAR_MAGENTA = NULL;
    Marvin_PANEL_DASHBOARD_TEST_BAR_RED = NULL;
    Marvin_PANEL_DASHBOARD_TEST_BAR_BLUE = NULL;
    Marvin_GRADIENT_DASHBOARD_TEST_BAR_GRAY = NULL;
    Marvin_PANEL_DASHBOARD_NO_SIGNAL = NULL;
    Marvin_PANEL_DASHBOARD_TEST_PATTERN_BORDER = NULL;
    Marvin_PANEL_DASHBOARD_NO_SIGNAL_LED = NULL;
    Marvin_LABEL_DASHBOARD_NO_SIGNAL = NULL;
    Marvin_PANEL_DASHBOARD_SONG_INFO = NULL;
    Marvin_PANEL_DASHBOARD_SONG_DIVIDER = NULL;
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY = NULL;
    Marvin_PANEL_DASHBOARD_SONG_AlbumArt = NULL;
    Marvin_PANEL_DASHBOARD_SONG_ALBUM_ART_BORDER = NULL;
    Marvin_PANEL_DASHBOARD_SONG_INFO_TEXT = NULL;
    Marvin_LABEL_DASHBOARD_SONG_Status = NULL;
    Marvin_LABEL_DASHBOARD_SONG_SongTitle = NULL;
    Marvin_LABEL_DASHBOARD_SONG_SongArtist = NULL;
    Marvin_LABEL_DASHBOARD_SONG_SongAlbum = NULL;
    Marvin_LABEL_DASHBOARD_SONG_GENRE = NULL;
    Marvin_LABEL_DASHBOARD_SONG_SongGenre = NULL;
    Marvin_LABEL_DASHBOARD_SONG_DURATION = NULL;
    Marvin_LABEL_DASHBOARD_SONG_SongDuration = NULL;
    Marvin_LABEL_DASHBOARD_SONG_TIER = NULL;
    Marvin_LABEL_DASHBOARD_SONG_SongTier = NULL;
    Marvin_LABEL_DASHBOARD_SONG_START_TIME = NULL;
    Marvin_LABEL_DASHBOARD_SONG_STOP_TIME = NULL;
    Marvin_PROGRESSBAR_DASHBOARD_SONG_PLAYTIME = NULL;
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_MODE = NULL;
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_GameMode = NULL;
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_DIFFICULTY = NULL;
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_Difficulty = NULL;
    Marvin_PANEL_DASHBOARD_SONG_GAMEPLAY_DIVIDER = NULL;
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_SELECT_SONG = NULL;
    Marvin_BUTTON_DASHBOARD_GAMEPLAY_START = NULL;
    Marvin_LABEL_DASHBOARD_SONG_GAMEPLAY_Difficulty = NULL;
    Marvin_IMAGE_DASHBOARD_HUMAN_PLAYER = NULL;
    Marvin_LABEL_DASHBOARD_HUMAN_Name = NULL;
    Marvin_LABEL_DASHBOARD_HUMAN_HumanPlayer = NULL;
    Marvin_PANEL_DASHBOARD_HUMAN_STATE = NULL;
    Marvin_LABEL_DASHBOARD_HUMAN_SCORE = NULL;
    Marvin_BUTTON_DASHBOARD_HUMAN_1X = NULL;
    Marvin_BUTTON_DASHBOARD_HUMAN_2X = NULL;
    Marvin_BUTTON_DASHBOARD_HUMAN_3X = NULL;
    Marvin_BUTTON_DASHBOARD_HUMAN_4X = NULL;
    Marvin_LABEL_DASHBOARD_HUMAN_Score = NULL;
    Marvin_LABEL_DASHBOARD_HUMAN_STREAK = NULL;
    Marvin_LABEL_DASHBOARD_HUMAN_Streak = NULL;
    Marvin_LABEL_DASHBOARD_HUMAN_ACCURACY = NULL;
    Marvin_LABEL_DASHBOARD_HUMAN_Accuracy = NULL;
    Marvin_PROGRESSBAR_DASHBOARD_HUMAN_Accuracy = NULL;
    Marvin_LABEL_DASHBOARD_HUMAN_STAR_POWER = NULL;
    Marvin_LABEL_DASHBOARD_HUMAN_StarPower = NULL;
    Marvin_PROGRESSBAR_DASHBOARD_HUMAN_StarPower = NULL;
    Marvin_PANEL_DASHBOARD_HUMAN_DIVIDER_1 = NULL;
    Marvin_panel_Container_98_0 = NULL;
    Marvin_PANEL_DASHBOARD_HUMAN_BORDER = NULL;
    Marvin_PANEL_DASHBOARD_HUMAN_STATE_LED = NULL;
    Marvin_LABEL_DASHBOARD_HUMAN_HumanState = NULL;
    Marvin_panel_Paragraph_13_0 = NULL;
    Marvin_panel_Container_99_0 = NULL;
    Marvin_label_CONTROLLER_0 = NULL;
    Marvin_panel_Container_100_0 = NULL;
    Marvin_panel_Container_101_0 = NULL;
    Marvin_panel_Container_102_0 = NULL;
    Marvin_panel_Text_32_0 = NULL;
    Marvin_panel_Text_33_0 = NULL;
    Marvin_label_Wii_guitar_0 = NULL;
    Marvin_label_Connected_1 = NULL;
    Marvin_panel_Text_34_0 = NULL;
    Marvin_panel_Text_35_0 = NULL;
    Marvin_label_Wii_remote_0 = NULL;
    Marvin_label_Connected_0_0 = NULL;
    Marvin_panel_Text_36_0 = NULL;
    Marvin_panel_Text_37_0 = NULL;
    Marvin_label_Battery_0 = NULL;
    Marvin_label__68__0 = NULL;

    leRemoveRootWidget(root1, 1);
    leWidget_Delete(root1);
    root1 = NULL;

    Marvin_PANEL_NAVIGATION = NULL;
    Marvin_PANEL_NAVIGATION_TOP = NULL;
    Marvin_PANEL_NAVIGATION_MIDDLE = NULL;
    Marvin_PANEL_NAVIGATION_BOTTOM = NULL;
    Marvin_LABEL_NAVIGATION = NULL;
    Marvin_LABEL_NAV_SUB_HEADING = NULL;
    Marvin_BUTTON_NAV_DASHBOARD = NULL;
    Marvin_BUTTON_NAV_WIIMOTES = NULL;
    Marvin_BUTTON_NAV_LOGS = NULL;
    Marvin_BUTTON_NAV_PERFORMANCE = NULL;
    Marvin_BUTTON_NAV_SYSTEM_INFO = NULL;
    Marvin_BUTTON_NAV_DIAGNOSTICS = NULL;
    Marvin_BUTTON_NAV_SETTINGS = NULL;
    Marvin_panel_Container_196 = NULL;
    Marvin_panel_Container_197 = NULL;
    Marvin_panel_Container_198 = NULL;
    Marvin_panel_Container_199 = NULL;
    Marvin_panel_Container_200 = NULL;
    Marvin_label_STATUS = NULL;
    Marvin_label_Connected = NULL;

    leRemoveRootWidget(root2, 2);
    leWidget_Delete(root2);
    root2 = NULL;

    Marvin_PANEL_SONG_SELECT = NULL;
    Marvin_PANEL_SONG_SELECT_TOP = NULL;
    Marvin_PANEL_SONG_SELECT_BOTTOM = NULL;
    Marvin_LABEL_SELECT_SONG = NULL;
    Marvin_BUTTON_SONG_SELECT_CLOSE = NULL;
    Marvin_PANEL_SONG_SELECT_LEFT = NULL;
    Marvin_PANEL_SONG_SELECT_CENTER = NULL;
    Marvin_PANEL_SONG_SELECT_RIGHT = NULL;
    Marvin_PANEL_SETLIST = NULL;
    Marvin_LABEL_SETLIST = NULL;
    Marvin_PANEL_SONG_SELECT_SONG_INFO = NULL;
    Marvin_LABEL_SONG_SELECT_ALBUM = NULL;
    Marvin_LABEL_SONG_SELECT_SongAlbum = NULL;
    Marvin_LABEL_SONG_SELECT_YEAR = NULL;
    Marvin_LABEL_SONG_SELECT_SongYear = NULL;
    Marvin_LABEL_SONG_SELECT_GENRE = NULL;
    Marvin_LABEL_SONG_SELECT_SongGenre = NULL;
    Marvin_LABEL_SONG_SELECT_DURATION = NULL;
    Marvin_LABEL_SONG_SELECT_SongDuration = NULL;
    Marvin_PANEL_SONG_SELECT_DIFFICULTY = NULL;
    Marvin_PANEL_SONG_SELECT_MODE = NULL;
    Marvin_BUTTON_SONG_SELECT_SELECT = NULL;
    Marvin_LABEL_SONG_SELECT_DIFFICULTY = NULL;
    Marvin_BUTTON_SONG_SELECT_EASY = NULL;
    Marvin_BUTTON_SONG_SELECT_MEDIUM = NULL;
    Marvin_BUTTON_SONG_SELECT_HARD = NULL;
    Marvin_BUTTON_SONG_SELECT_EXPERT = NULL;
    Marvin_LABEL_SONG_SELECT_MODE = NULL;
    Marvin_BUTTON_SONG_SELECT_1P_ROBOT = NULL;
    Marvin_BUTTON_SONG_SELECT_1P_HUMAN = NULL;
    Marvin_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN = NULL;

    leRemoveRootWidget(root3, 3);
    leWidget_Delete(root3);
    root3 = NULL;

    Marvin_PANEL_SONG_SELECT_ALBUM_ART = NULL;
    Marvin_IMAGE_ALBUM_ART = NULL;
    Marvin_PANEL_ALBUM_ART_OVERLAY = NULL;
    Marvin_LABEL_SONG_SELECT_SongTier = NULL;
    Marvin_LABEL_SONG_SELECT_SongTitle = NULL;
    Marvin_LABEL_SONG_SELECT_Artist = NULL;

    leRemoveRootWidget(root4, 4);
    leWidget_Delete(root4);
    root4 = NULL;

    Marvin_PANEL_WIIMOTES = NULL;
    Marvin_PANEL_WIIMOTES_BOTTOM = NULL;
    Marvin_PANEL_WIIMOTES_ROBOT = NULL;
    Marvin_PANEL_WIIMOTES_HUMAN = NULL;
    Marvin_LABEL_WIIMOTES_ROBOT_RobotLemmy = NULL;
    Marvin_panel_Button_37 = NULL;
    Marvin_PANEL_WIIMOTES_ROBOT_GUITAR_EXTENSION = NULL;
    Marvin_PANEL_WIIMOTES_ROBOT_WIIMOTE = NULL;
    Marvin_panel_Text_1_1 = NULL;
    Marvin_label_ENABLED_0 = NULL;
    Marvin_LABEL_WIIMOTES_ROBOT_GUITAR_EXTENSION = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_GREEN = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_RED = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_YELLOW = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_BLUE = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_FRET_ORANGE = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_UP = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_STRUM_DOWN = NULL;
    Marvin_panel_Container_15_1 = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_PLUS = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_GUITAR_MINUS = NULL;
    Marvin_label_WHAMMY_1 = NULL;
    Marvin_panel_WhammySlider_1 = NULL;
    Marvin_panel_Container_16_1 = NULL;
    Marvin_panel_Container_17_1 = NULL;
    Marvin_panel_Container_18_1 = NULL;
    Marvin_panel_Container_19_1 = NULL;
    Marvin_LABEL_WIIMOTES_ROBOT_WIIMOTE = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_UP = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_LEFT = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_RIGHT = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_DOWN = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_HOME = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_A = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_B = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_ONE = NULL;
    Marvin_BUTTON_WIIMOTES_ROBOT_TWO = NULL;
    Marvin_panel_TiltControl_1 = NULL;
    Marvin_image_TiltControl_1 = NULL;
    Marvin_label_TILT_1 = NULL;

    leRemoveRootWidget(root5, 5);
    leWidget_Delete(root5);
    root5 = NULL;

    Marvin_PANEL_KEYBOARD = NULL;

    leRemoveRootWidget(root6, 6);
    leWidget_Delete(root6);
    root6 = NULL;

    Marvin_PANEL_BUS = NULL;


    showing = LE_FALSE;
}

void screenDestroy_Marvin(void)
{
    if(initialized == LE_FALSE)
        return;

    initialized = LE_FALSE;
}

leWidget* screenGetRoot_Marvin(uint32_t lyrIdx)
{
    if(lyrIdx >= LE_LAYER_COUNT)
        return NULL;

    switch(lyrIdx)
    {
        case 0:
        {
            return root0;
        }
        case 1:
        {
            return root1;
        }
        case 2:
        {
            return root2;
        }
        case 3:
        {
            return root3;
        }
        case 4:
        {
            return root4;
        }
        case 5:
        {
            return root5;
        }
        case 6:
        {
            return root6;
        }
        default:
        {
            return NULL;
        }
    }
}

