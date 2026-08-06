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
leWidget* Marvin_PANEL_NAVIGATION;
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
    Marvin_PANEL_NAVIGATION->fn->setScheme(Marvin_PANEL_NAVIGATION, &SCHEME_FILL_ZINC_900);
    root1->fn->addChild(root1, (leWidget*)Marvin_PANEL_NAVIGATION);

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
    Marvin_BUTTON_SONG_SELECT_CLOSE->fn->setPressedImage(Marvin_BUTTON_SONG_SELECT_CLOSE, (leImage*)&BUTTON_ICON_CLOSE);
    Marvin_BUTTON_SONG_SELECT_CLOSE->fn->setReleasedImage(Marvin_BUTTON_SONG_SELECT_CLOSE, (leImage*)&BUTTON_ICON_CLOSE);
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

    leRemoveRootWidget(root1, 1);
    leWidget_Delete(root1);
    root1 = NULL;

    Marvin_PANEL_NAVIGATION = NULL;

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

