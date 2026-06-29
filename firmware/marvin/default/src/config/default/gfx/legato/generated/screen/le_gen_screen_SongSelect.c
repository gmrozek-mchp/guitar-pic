#include "gfx/legato/generated/screen/le_gen_screen_SongSelect.h"

// screen member widget declarations
static leWidget* root0;

leWidget* SongSelect_PANEL_SONG_SELECT;
leWidget* SongSelect_PANEL_SONG_SELECT_TOP;
leWidget* SongSelect_PANEL_SONG_SELECT_BOTTOM;
leLabelWidget* SongSelect_LABEL_SELECT_SONG;
leButtonWidget* SongSelect_BUTTON_SONG_SELECT_CLOSE;
leWidget* SongSelect_PANEL_SONG_SELECT_LEFT;
leWidget* SongSelect_PANEL_SONG_SELECT_CENTER;
leWidget* SongSelect_PANEL_SONG_SELECT_RIGHT;
leWidget* SongSelect_PANEL_SETLIST;
leLabelWidget* SongSelect_LABEL_SETLIST;
leImageWidget* SongSelect_IMAGE_ALBUM_ART;
leWidget* SongSelect_PANEL_ALBUM_ART_OVERLAY;
leLabelWidget* SongSelect_LABEL_SONG_SELECT_SONG_LEVEL;
leLabelWidget* SongSelect_LABEL_SONG_SELECT_SONG_TITLE;
leLabelWidget* SongSelect_LABEL_SONG_SELECT_SONG_ARTIST;
leWidget* SongSelect_PANEL_SONG_SELECT_SONG_INFO;
leLabelWidget* SongSelect_LABEL_SONG_SELECT_ALBUM;
leLabelWidget* SongSelect_LABEL_SONG_SELECT_SongAlbum;
leLabelWidget* SongSelect_LABEL_SONG_SELECT_YEAR;
leLabelWidget* SongSelect_LABEL_SONG_SELECT_SongYear;
leLabelWidget* SongSelect_LABEL_SONG_SELECT_GENRE;
leLabelWidget* SongSelect_LABEL_SONG_SELECT_SongGenre;
leLabelWidget* SongSelect_LABEL_SONG_SELECT_DURATION;
leLabelWidget* SongSelect_LABEL_SONG_SELECT_SongDuration;
leWidget* SongSelect_PANEL_SONG_SELECT_DIFFICULTY;
leWidget* SongSelect_PANEL_SONG_SELECT_MODE;
leButtonWidget* SongSelect_BUTTON_SONG_SELECT_SELECT;
leLabelWidget* SongSelect_LABEL_SONG_SELECT_DIFFICULTY;
leButtonWidget* SongSelect_BUTTON_SONG_SELECT_EASY;
leButtonWidget* SongSelect_BUTTON_SONG_SELECT_MEDIUM;
leButtonWidget* SongSelect_BUTTON_SONG_SELECT_HARD;
leButtonWidget* SongSelect_BUTTON_SONG_SELECT_EXPERT;
leLabelWidget* SongSelect_LABEL_SONG_SELECT_MODE;
leButtonWidget* SongSelect_BUTTON_SONG_SELECT_1P_ROBOT;
leButtonWidget* SongSelect_BUTTON_SONG_SELECT_1P_HUMAN;
leButtonWidget* SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN;

static leBool initialized = LE_FALSE;
static leBool showing = LE_FALSE;

leResult screenInit_SongSelect(void)
{
    if(initialized == LE_TRUE)
        return LE_FAILURE;

    // layer 0
    root0 = leWidget_New();
    root0->fn->setSize(root0, 1100, 660);
    root0->fn->setBackgroundType(root0, LE_WIDGET_BACKGROUND_NONE);
    root0->fn->setMargins(root0, 0, 0, 0, 0);
    root0->flags |= LE_WIDGET_IGNOREEVENTS;
    root0->flags |= LE_WIDGET_IGNOREPICK;

    SongSelect_PANEL_SONG_SELECT = leWidget_New();
    SongSelect_PANEL_SONG_SELECT->fn->setPosition(SongSelect_PANEL_SONG_SELECT, 0, 0);
    SongSelect_PANEL_SONG_SELECT->fn->setSize(SongSelect_PANEL_SONG_SELECT, 1100, 660);
    SongSelect_PANEL_SONG_SELECT->fn->setScheme(SongSelect_PANEL_SONG_SELECT, &SCHEME_PANEL_GRAY_18181B);
    root0->fn->addChild(root0, (leWidget*)SongSelect_PANEL_SONG_SELECT);

    SongSelect_PANEL_SONG_SELECT_TOP = leWidget_New();
    SongSelect_PANEL_SONG_SELECT_TOP->fn->setPosition(SongSelect_PANEL_SONG_SELECT_TOP, 0, 0);
    SongSelect_PANEL_SONG_SELECT_TOP->fn->setSize(SongSelect_PANEL_SONG_SELECT_TOP, 1100, 67);
    SongSelect_PANEL_SONG_SELECT_TOP->fn->setBackgroundType(SongSelect_PANEL_SONG_SELECT_TOP, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_PANEL_SONG_SELECT_TOP->fn->setBorderType(SongSelect_PANEL_SONG_SELECT_TOP, LE_WIDGET_BORDER_LINE);
    SongSelect_PANEL_SONG_SELECT->fn->addChild(SongSelect_PANEL_SONG_SELECT, (leWidget*)SongSelect_PANEL_SONG_SELECT_TOP);

    SongSelect_LABEL_SELECT_SONG = leLabelWidget_New();
    SongSelect_LABEL_SELECT_SONG->fn->setPosition(SongSelect_LABEL_SELECT_SONG, 24, 18);
    SongSelect_LABEL_SELECT_SONG->fn->setSize(SongSelect_LABEL_SELECT_SONG, 1000, 28);
    SongSelect_LABEL_SELECT_SONG->fn->setScheme(SongSelect_LABEL_SELECT_SONG, &SCHEME_TEXT_GRAY_E4E4E7);
    SongSelect_LABEL_SELECT_SONG->fn->setBackgroundType(SongSelect_LABEL_SELECT_SONG, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_LABEL_SELECT_SONG->fn->setMargins(SongSelect_LABEL_SELECT_SONG, 0, 0, 0, 0);
    SongSelect_LABEL_SELECT_SONG->fn->setString(SongSelect_LABEL_SELECT_SONG, (leString*)&string_SONG_SELECT_SELECT_SONG);
    SongSelect_PANEL_SONG_SELECT_TOP->fn->addChild(SongSelect_PANEL_SONG_SELECT_TOP, (leWidget*)SongSelect_LABEL_SELECT_SONG);

    SongSelect_BUTTON_SONG_SELECT_CLOSE = leButtonWidget_New();
    SongSelect_BUTTON_SONG_SELECT_CLOSE->fn->setPosition(SongSelect_BUTTON_SONG_SELECT_CLOSE, 1033, 8);
    SongSelect_BUTTON_SONG_SELECT_CLOSE->fn->setSize(SongSelect_BUTTON_SONG_SELECT_CLOSE, 50, 50);
    SongSelect_BUTTON_SONG_SELECT_CLOSE->fn->setBackgroundType(SongSelect_BUTTON_SONG_SELECT_CLOSE, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_BUTTON_SONG_SELECT_CLOSE->fn->setBorderType(SongSelect_BUTTON_SONG_SELECT_CLOSE, LE_WIDGET_BORDER_NONE);
    SongSelect_BUTTON_SONG_SELECT_CLOSE->fn->setPressedImage(SongSelect_BUTTON_SONG_SELECT_CLOSE, (leImage*)&figmaImg_Icon_17);
    SongSelect_BUTTON_SONG_SELECT_CLOSE->fn->setReleasedImage(SongSelect_BUTTON_SONG_SELECT_CLOSE, (leImage*)&figmaImg_Icon_17);
    SongSelect_PANEL_SONG_SELECT_TOP->fn->addChild(SongSelect_PANEL_SONG_SELECT_TOP, (leWidget*)SongSelect_BUTTON_SONG_SELECT_CLOSE);

    SongSelect_PANEL_SONG_SELECT_BOTTOM = leWidget_New();
    SongSelect_PANEL_SONG_SELECT_BOTTOM->fn->setPosition(SongSelect_PANEL_SONG_SELECT_BOTTOM, 0, 66);
    SongSelect_PANEL_SONG_SELECT_BOTTOM->fn->setSize(SongSelect_PANEL_SONG_SELECT_BOTTOM, 1100, 594);
    SongSelect_PANEL_SONG_SELECT_BOTTOM->fn->setBackgroundType(SongSelect_PANEL_SONG_SELECT_BOTTOM, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_PANEL_SONG_SELECT_BOTTOM->fn->setBorderType(SongSelect_PANEL_SONG_SELECT_BOTTOM, LE_WIDGET_BORDER_LINE);
    SongSelect_PANEL_SONG_SELECT->fn->addChild(SongSelect_PANEL_SONG_SELECT, (leWidget*)SongSelect_PANEL_SONG_SELECT_BOTTOM);

    SongSelect_PANEL_SONG_SELECT_LEFT = leWidget_New();
    SongSelect_PANEL_SONG_SELECT_LEFT->fn->setPosition(SongSelect_PANEL_SONG_SELECT_LEFT, 0, 0);
    SongSelect_PANEL_SONG_SELECT_LEFT->fn->setSize(SongSelect_PANEL_SONG_SELECT_LEFT, 320, 594);
    SongSelect_PANEL_SONG_SELECT_LEFT->fn->setBackgroundType(SongSelect_PANEL_SONG_SELECT_LEFT, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_PANEL_SONG_SELECT_LEFT->fn->setBorderType(SongSelect_PANEL_SONG_SELECT_LEFT, LE_WIDGET_BORDER_LINE);
    SongSelect_PANEL_SONG_SELECT_BOTTOM->fn->addChild(SongSelect_PANEL_SONG_SELECT_BOTTOM, (leWidget*)SongSelect_PANEL_SONG_SELECT_LEFT);

    SongSelect_PANEL_SETLIST = leWidget_New();
    SongSelect_PANEL_SETLIST->fn->setPosition(SongSelect_PANEL_SETLIST, 0, 0);
    SongSelect_PANEL_SETLIST->fn->setSize(SongSelect_PANEL_SETLIST, 320, 33);
    SongSelect_PANEL_SETLIST->fn->setBackgroundType(SongSelect_PANEL_SETLIST, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_PANEL_SONG_SELECT_LEFT->fn->addChild(SongSelect_PANEL_SONG_SELECT_LEFT, (leWidget*)SongSelect_PANEL_SETLIST);

    SongSelect_LABEL_SETLIST = leLabelWidget_New();
    SongSelect_LABEL_SETLIST->fn->setPosition(SongSelect_LABEL_SETLIST, 16, 8);
    SongSelect_LABEL_SETLIST->fn->setSize(SongSelect_LABEL_SETLIST, 285, 16);
    SongSelect_LABEL_SETLIST->fn->setScheme(SongSelect_LABEL_SETLIST, &SCHEME_TEXT_GRAY_71717B);
    SongSelect_LABEL_SETLIST->fn->setBackgroundType(SongSelect_LABEL_SETLIST, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_LABEL_SETLIST->fn->setVAlignment(SongSelect_LABEL_SETLIST, LE_VALIGN_TOP);
    SongSelect_LABEL_SETLIST->fn->setMargins(SongSelect_LABEL_SETLIST, 0, 0, 0, 0);
    SongSelect_LABEL_SETLIST->fn->setString(SongSelect_LABEL_SETLIST, (leString*)&string_SONG_SELECT_SETLIST);
    SongSelect_PANEL_SETLIST->fn->addChild(SongSelect_PANEL_SETLIST, (leWidget*)SongSelect_LABEL_SETLIST);

    SongSelect_PANEL_SONG_SELECT_CENTER = leWidget_New();
    SongSelect_PANEL_SONG_SELECT_CENTER->fn->setPosition(SongSelect_PANEL_SONG_SELECT_CENTER, 319, 0);
    SongSelect_PANEL_SONG_SELECT_CENTER->fn->setSize(SongSelect_PANEL_SONG_SELECT_CENTER, 558, 594);
    SongSelect_PANEL_SONG_SELECT_CENTER->fn->setBackgroundType(SongSelect_PANEL_SONG_SELECT_CENTER, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_PANEL_SONG_SELECT_CENTER->fn->setBorderType(SongSelect_PANEL_SONG_SELECT_CENTER, LE_WIDGET_BORDER_LINE);
    SongSelect_PANEL_SONG_SELECT_BOTTOM->fn->addChild(SongSelect_PANEL_SONG_SELECT_BOTTOM, (leWidget*)SongSelect_PANEL_SONG_SELECT_CENTER);

    SongSelect_IMAGE_ALBUM_ART = leImageWidget_New();
    SongSelect_IMAGE_ALBUM_ART->fn->setPosition(SongSelect_IMAGE_ALBUM_ART, 24, 24);
    SongSelect_IMAGE_ALBUM_ART->fn->setSize(SongSelect_IMAGE_ALBUM_ART, 505, 208);
    SongSelect_IMAGE_ALBUM_ART->fn->setScheme(SongSelect_IMAGE_ALBUM_ART, &SCHEME_BACKGROUND);
    SongSelect_IMAGE_ALBUM_ART->fn->setBorderType(SongSelect_IMAGE_ALBUM_ART, LE_WIDGET_BORDER_NONE);
    SongSelect_PANEL_SONG_SELECT_CENTER->fn->addChild(SongSelect_PANEL_SONG_SELECT_CENTER, (leWidget*)SongSelect_IMAGE_ALBUM_ART);

    SongSelect_PANEL_ALBUM_ART_OVERLAY = leWidget_New();
    SongSelect_PANEL_ALBUM_ART_OVERLAY->fn->setPosition(SongSelect_PANEL_ALBUM_ART_OVERLAY, 24, 24);
    SongSelect_PANEL_ALBUM_ART_OVERLAY->fn->setSize(SongSelect_PANEL_ALBUM_ART_OVERLAY, 505, 208);
    SongSelect_PANEL_ALBUM_ART_OVERLAY->fn->setAlphaEnabled(SongSelect_PANEL_ALBUM_ART_OVERLAY, LE_TRUE);
    SongSelect_PANEL_ALBUM_ART_OVERLAY->fn->setAlphaAmount(SongSelect_PANEL_ALBUM_ART_OVERLAY, 50);
    SongSelect_PANEL_ALBUM_ART_OVERLAY->fn->setScheme(SongSelect_PANEL_ALBUM_ART_OVERLAY, &SCHEME_MAROON);
    SongSelect_PANEL_SONG_SELECT_CENTER->fn->addChild(SongSelect_PANEL_SONG_SELECT_CENTER, (leWidget*)SongSelect_PANEL_ALBUM_ART_OVERLAY);

    SongSelect_LABEL_SONG_SELECT_SONG_LEVEL = leLabelWidget_New();
    SongSelect_LABEL_SONG_SELECT_SONG_LEVEL->fn->setPosition(SongSelect_LABEL_SONG_SELECT_SONG_LEVEL, 40, 146);
    SongSelect_LABEL_SONG_SELECT_SONG_LEVEL->fn->setSize(SongSelect_LABEL_SONG_SELECT_SONG_LEVEL, 87, 16);
    SongSelect_LABEL_SONG_SELECT_SONG_LEVEL->fn->setScheme(SongSelect_LABEL_SONG_SELECT_SONG_LEVEL, &SCHEME_TEXT_RED);
    SongSelect_LABEL_SONG_SELECT_SONG_LEVEL->fn->setBackgroundType(SongSelect_LABEL_SONG_SELECT_SONG_LEVEL, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_LABEL_SONG_SELECT_SONG_LEVEL->fn->setVAlignment(SongSelect_LABEL_SONG_SELECT_SONG_LEVEL, LE_VALIGN_TOP);
    SongSelect_LABEL_SONG_SELECT_SONG_LEVEL->fn->setMargins(SongSelect_LABEL_SONG_SELECT_SONG_LEVEL, 0, 0, 0, 0);
    SongSelect_LABEL_SONG_SELECT_SONG_LEVEL->fn->setString(SongSelect_LABEL_SONG_SELECT_SONG_LEVEL, (leString*)&string_SONG_SELECT_SongTier);
    SongSelect_PANEL_SONG_SELECT_CENTER->fn->addChild(SongSelect_PANEL_SONG_SELECT_CENTER, (leWidget*)SongSelect_LABEL_SONG_SELECT_SONG_LEVEL);

    SongSelect_LABEL_SONG_SELECT_SONG_TITLE = leLabelWidget_New();
    SongSelect_LABEL_SONG_SELECT_SONG_TITLE->fn->setPosition(SongSelect_LABEL_SONG_SELECT_SONG_TITLE, 40, 167);
    SongSelect_LABEL_SONG_SELECT_SONG_TITLE->fn->setSize(SongSelect_LABEL_SONG_SELECT_SONG_TITLE, 391, 30);
    SongSelect_LABEL_SONG_SELECT_SONG_TITLE->fn->setScheme(SongSelect_LABEL_SONG_SELECT_SONG_TITLE, &SCHEME_TEXT_WHITE);
    SongSelect_LABEL_SONG_SELECT_SONG_TITLE->fn->setBackgroundType(SongSelect_LABEL_SONG_SELECT_SONG_TITLE, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_LABEL_SONG_SELECT_SONG_TITLE->fn->setVAlignment(SongSelect_LABEL_SONG_SELECT_SONG_TITLE, LE_VALIGN_TOP);
    SongSelect_LABEL_SONG_SELECT_SONG_TITLE->fn->setMargins(SongSelect_LABEL_SONG_SELECT_SONG_TITLE, 0, 0, 0, 0);
    SongSelect_LABEL_SONG_SELECT_SONG_TITLE->fn->setString(SongSelect_LABEL_SONG_SELECT_SONG_TITLE, (leString*)&string_SONG_SELECT_SongTitle);
    SongSelect_PANEL_SONG_SELECT_CENTER->fn->addChild(SongSelect_PANEL_SONG_SELECT_CENTER, (leWidget*)SongSelect_LABEL_SONG_SELECT_SONG_TITLE);

    SongSelect_LABEL_SONG_SELECT_SONG_ARTIST = leLabelWidget_New();
    SongSelect_LABEL_SONG_SELECT_SONG_ARTIST->fn->setPosition(SongSelect_LABEL_SONG_SELECT_SONG_ARTIST, 40, 196);
    SongSelect_LABEL_SONG_SELECT_SONG_ARTIST->fn->setSize(SongSelect_LABEL_SONG_SELECT_SONG_ARTIST, 93, 20);
    SongSelect_LABEL_SONG_SELECT_SONG_ARTIST->fn->setScheme(SongSelect_LABEL_SONG_SELECT_SONG_ARTIST, &SCHEME_TEXT_GRAY_D4D4D8);
    SongSelect_LABEL_SONG_SELECT_SONG_ARTIST->fn->setBackgroundType(SongSelect_LABEL_SONG_SELECT_SONG_ARTIST, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_LABEL_SONG_SELECT_SONG_ARTIST->fn->setVAlignment(SongSelect_LABEL_SONG_SELECT_SONG_ARTIST, LE_VALIGN_TOP);
    SongSelect_LABEL_SONG_SELECT_SONG_ARTIST->fn->setMargins(SongSelect_LABEL_SONG_SELECT_SONG_ARTIST, 0, 0, 0, 0);
    SongSelect_LABEL_SONG_SELECT_SONG_ARTIST->fn->setString(SongSelect_LABEL_SONG_SELECT_SONG_ARTIST, (leString*)&string_SONG_SELECT_SongArtist);
    SongSelect_PANEL_SONG_SELECT_CENTER->fn->addChild(SongSelect_PANEL_SONG_SELECT_CENTER, (leWidget*)SongSelect_LABEL_SONG_SELECT_SONG_ARTIST);

    SongSelect_PANEL_SONG_SELECT_SONG_INFO = leWidget_New();
    SongSelect_PANEL_SONG_SELECT_SONG_INFO->fn->setPosition(SongSelect_PANEL_SONG_SELECT_SONG_INFO, 24, 248);
    SongSelect_PANEL_SONG_SELECT_SONG_INFO->fn->setSize(SongSelect_PANEL_SONG_SELECT_SONG_INFO, 505, 88);
    SongSelect_PANEL_SONG_SELECT_SONG_INFO->fn->setScheme(SongSelect_PANEL_SONG_SELECT_SONG_INFO, &SCHEME_BACKGROUND);
    SongSelect_PANEL_SONG_SELECT_SONG_INFO->fn->setBackgroundType(SongSelect_PANEL_SONG_SELECT_SONG_INFO, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_PANEL_SONG_SELECT_CENTER->fn->addChild(SongSelect_PANEL_SONG_SELECT_CENTER, (leWidget*)SongSelect_PANEL_SONG_SELECT_SONG_INFO);

    SongSelect_LABEL_SONG_SELECT_ALBUM = leLabelWidget_New();
    SongSelect_LABEL_SONG_SELECT_ALBUM->fn->setPosition(SongSelect_LABEL_SONG_SELECT_ALBUM, 0, 0);
    SongSelect_LABEL_SONG_SELECT_ALBUM->fn->setSize(SongSelect_LABEL_SONG_SELECT_ALBUM, 240, 16);
    SongSelect_LABEL_SONG_SELECT_ALBUM->fn->setScheme(SongSelect_LABEL_SONG_SELECT_ALBUM, &SCHEME_TEXT_GRAY_52525C);
    SongSelect_LABEL_SONG_SELECT_ALBUM->fn->setBackgroundType(SongSelect_LABEL_SONG_SELECT_ALBUM, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_LABEL_SONG_SELECT_ALBUM->fn->setVAlignment(SongSelect_LABEL_SONG_SELECT_ALBUM, LE_VALIGN_TOP);
    SongSelect_LABEL_SONG_SELECT_ALBUM->fn->setMargins(SongSelect_LABEL_SONG_SELECT_ALBUM, 0, 0, 0, 0);
    SongSelect_LABEL_SONG_SELECT_ALBUM->fn->setString(SongSelect_LABEL_SONG_SELECT_ALBUM, (leString*)&string_SONG_SELECT_ALBUM);
    SongSelect_PANEL_SONG_SELECT_SONG_INFO->fn->addChild(SongSelect_PANEL_SONG_SELECT_SONG_INFO, (leWidget*)SongSelect_LABEL_SONG_SELECT_ALBUM);

    SongSelect_LABEL_SONG_SELECT_SongAlbum = leLabelWidget_New();
    SongSelect_LABEL_SONG_SELECT_SongAlbum->fn->setPosition(SongSelect_LABEL_SONG_SELECT_SongAlbum, 0, 16);
    SongSelect_LABEL_SONG_SELECT_SongAlbum->fn->setSize(SongSelect_LABEL_SONG_SELECT_SongAlbum, 240, 20);
    SongSelect_LABEL_SONG_SELECT_SongAlbum->fn->setScheme(SongSelect_LABEL_SONG_SELECT_SongAlbum, &SCHEME_TEXT_GRAY_E4E4E7);
    SongSelect_LABEL_SONG_SELECT_SongAlbum->fn->setBackgroundType(SongSelect_LABEL_SONG_SELECT_SongAlbum, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_LABEL_SONG_SELECT_SongAlbum->fn->setVAlignment(SongSelect_LABEL_SONG_SELECT_SongAlbum, LE_VALIGN_TOP);
    SongSelect_LABEL_SONG_SELECT_SongAlbum->fn->setMargins(SongSelect_LABEL_SONG_SELECT_SongAlbum, 0, 0, 0, 0);
    SongSelect_LABEL_SONG_SELECT_SongAlbum->fn->setString(SongSelect_LABEL_SONG_SELECT_SongAlbum, (leString*)&string_SONG_SELECT_SongAlbum);
    SongSelect_PANEL_SONG_SELECT_SONG_INFO->fn->addChild(SongSelect_PANEL_SONG_SELECT_SONG_INFO, (leWidget*)SongSelect_LABEL_SONG_SELECT_SongAlbum);

    SongSelect_LABEL_SONG_SELECT_YEAR = leLabelWidget_New();
    SongSelect_LABEL_SONG_SELECT_YEAR->fn->setPosition(SongSelect_LABEL_SONG_SELECT_YEAR, 259, 0);
    SongSelect_LABEL_SONG_SELECT_YEAR->fn->setSize(SongSelect_LABEL_SONG_SELECT_YEAR, 240, 16);
    SongSelect_LABEL_SONG_SELECT_YEAR->fn->setScheme(SongSelect_LABEL_SONG_SELECT_YEAR, &SCHEME_TEXT_GRAY_52525C);
    SongSelect_LABEL_SONG_SELECT_YEAR->fn->setBackgroundType(SongSelect_LABEL_SONG_SELECT_YEAR, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_LABEL_SONG_SELECT_YEAR->fn->setVAlignment(SongSelect_LABEL_SONG_SELECT_YEAR, LE_VALIGN_TOP);
    SongSelect_LABEL_SONG_SELECT_YEAR->fn->setMargins(SongSelect_LABEL_SONG_SELECT_YEAR, 0, 0, 0, 0);
    SongSelect_LABEL_SONG_SELECT_YEAR->fn->setString(SongSelect_LABEL_SONG_SELECT_YEAR, (leString*)&string_SONG_SELECT_YEAR);
    SongSelect_PANEL_SONG_SELECT_SONG_INFO->fn->addChild(SongSelect_PANEL_SONG_SELECT_SONG_INFO, (leWidget*)SongSelect_LABEL_SONG_SELECT_YEAR);

    SongSelect_LABEL_SONG_SELECT_SongYear = leLabelWidget_New();
    SongSelect_LABEL_SONG_SELECT_SongYear->fn->setPosition(SongSelect_LABEL_SONG_SELECT_SongYear, 259, 16);
    SongSelect_LABEL_SONG_SELECT_SongYear->fn->setSize(SongSelect_LABEL_SONG_SELECT_SongYear, 240, 20);
    SongSelect_LABEL_SONG_SELECT_SongYear->fn->setScheme(SongSelect_LABEL_SONG_SELECT_SongYear, &SCHEME_TEXT_GRAY_E4E4E7);
    SongSelect_LABEL_SONG_SELECT_SongYear->fn->setBackgroundType(SongSelect_LABEL_SONG_SELECT_SongYear, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_LABEL_SONG_SELECT_SongYear->fn->setVAlignment(SongSelect_LABEL_SONG_SELECT_SongYear, LE_VALIGN_TOP);
    SongSelect_LABEL_SONG_SELECT_SongYear->fn->setMargins(SongSelect_LABEL_SONG_SELECT_SongYear, 0, 0, 0, 0);
    SongSelect_LABEL_SONG_SELECT_SongYear->fn->setString(SongSelect_LABEL_SONG_SELECT_SongYear, (leString*)&string_SONG_SELECT_SongYear);
    SongSelect_PANEL_SONG_SELECT_SONG_INFO->fn->addChild(SongSelect_PANEL_SONG_SELECT_SONG_INFO, (leWidget*)SongSelect_LABEL_SONG_SELECT_SongYear);

    SongSelect_LABEL_SONG_SELECT_GENRE = leLabelWidget_New();
    SongSelect_LABEL_SONG_SELECT_GENRE->fn->setPosition(SongSelect_LABEL_SONG_SELECT_GENRE, 0, 48);
    SongSelect_LABEL_SONG_SELECT_GENRE->fn->setSize(SongSelect_LABEL_SONG_SELECT_GENRE, 240, 16);
    SongSelect_LABEL_SONG_SELECT_GENRE->fn->setScheme(SongSelect_LABEL_SONG_SELECT_GENRE, &SCHEME_TEXT_GRAY_52525C);
    SongSelect_LABEL_SONG_SELECT_GENRE->fn->setBackgroundType(SongSelect_LABEL_SONG_SELECT_GENRE, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_LABEL_SONG_SELECT_GENRE->fn->setVAlignment(SongSelect_LABEL_SONG_SELECT_GENRE, LE_VALIGN_TOP);
    SongSelect_LABEL_SONG_SELECT_GENRE->fn->setMargins(SongSelect_LABEL_SONG_SELECT_GENRE, 0, 0, 0, 0);
    SongSelect_LABEL_SONG_SELECT_GENRE->fn->setString(SongSelect_LABEL_SONG_SELECT_GENRE, (leString*)&string_SONG_SELECT_GENRE);
    SongSelect_PANEL_SONG_SELECT_SONG_INFO->fn->addChild(SongSelect_PANEL_SONG_SELECT_SONG_INFO, (leWidget*)SongSelect_LABEL_SONG_SELECT_GENRE);

    SongSelect_LABEL_SONG_SELECT_SongGenre = leLabelWidget_New();
    SongSelect_LABEL_SONG_SELECT_SongGenre->fn->setPosition(SongSelect_LABEL_SONG_SELECT_SongGenre, 0, 64);
    SongSelect_LABEL_SONG_SELECT_SongGenre->fn->setSize(SongSelect_LABEL_SONG_SELECT_SongGenre, 240, 20);
    SongSelect_LABEL_SONG_SELECT_SongGenre->fn->setScheme(SongSelect_LABEL_SONG_SELECT_SongGenre, &SCHEME_TEXT_GRAY_E4E4E7);
    SongSelect_LABEL_SONG_SELECT_SongGenre->fn->setBackgroundType(SongSelect_LABEL_SONG_SELECT_SongGenre, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_LABEL_SONG_SELECT_SongGenre->fn->setVAlignment(SongSelect_LABEL_SONG_SELECT_SongGenre, LE_VALIGN_TOP);
    SongSelect_LABEL_SONG_SELECT_SongGenre->fn->setMargins(SongSelect_LABEL_SONG_SELECT_SongGenre, 0, 0, 0, 0);
    SongSelect_LABEL_SONG_SELECT_SongGenre->fn->setString(SongSelect_LABEL_SONG_SELECT_SongGenre, (leString*)&string_SONG_SELECT_SongGenre);
    SongSelect_PANEL_SONG_SELECT_SONG_INFO->fn->addChild(SongSelect_PANEL_SONG_SELECT_SONG_INFO, (leWidget*)SongSelect_LABEL_SONG_SELECT_SongGenre);

    SongSelect_LABEL_SONG_SELECT_DURATION = leLabelWidget_New();
    SongSelect_LABEL_SONG_SELECT_DURATION->fn->setPosition(SongSelect_LABEL_SONG_SELECT_DURATION, 259, 48);
    SongSelect_LABEL_SONG_SELECT_DURATION->fn->setSize(SongSelect_LABEL_SONG_SELECT_DURATION, 240, 16);
    SongSelect_LABEL_SONG_SELECT_DURATION->fn->setScheme(SongSelect_LABEL_SONG_SELECT_DURATION, &SCHEME_TEXT_GRAY_52525C);
    SongSelect_LABEL_SONG_SELECT_DURATION->fn->setBackgroundType(SongSelect_LABEL_SONG_SELECT_DURATION, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_LABEL_SONG_SELECT_DURATION->fn->setVAlignment(SongSelect_LABEL_SONG_SELECT_DURATION, LE_VALIGN_TOP);
    SongSelect_LABEL_SONG_SELECT_DURATION->fn->setMargins(SongSelect_LABEL_SONG_SELECT_DURATION, 0, 0, 0, 0);
    SongSelect_LABEL_SONG_SELECT_DURATION->fn->setString(SongSelect_LABEL_SONG_SELECT_DURATION, (leString*)&string_SONG_SELECT_DURATION);
    SongSelect_PANEL_SONG_SELECT_SONG_INFO->fn->addChild(SongSelect_PANEL_SONG_SELECT_SONG_INFO, (leWidget*)SongSelect_LABEL_SONG_SELECT_DURATION);

    SongSelect_LABEL_SONG_SELECT_SongDuration = leLabelWidget_New();
    SongSelect_LABEL_SONG_SELECT_SongDuration->fn->setPosition(SongSelect_LABEL_SONG_SELECT_SongDuration, 259, 64);
    SongSelect_LABEL_SONG_SELECT_SongDuration->fn->setSize(SongSelect_LABEL_SONG_SELECT_SongDuration, 240, 20);
    SongSelect_LABEL_SONG_SELECT_SongDuration->fn->setScheme(SongSelect_LABEL_SONG_SELECT_SongDuration, &SCHEME_TEXT_GRAY_E4E4E7);
    SongSelect_LABEL_SONG_SELECT_SongDuration->fn->setBackgroundType(SongSelect_LABEL_SONG_SELECT_SongDuration, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_LABEL_SONG_SELECT_SongDuration->fn->setVAlignment(SongSelect_LABEL_SONG_SELECT_SongDuration, LE_VALIGN_TOP);
    SongSelect_LABEL_SONG_SELECT_SongDuration->fn->setMargins(SongSelect_LABEL_SONG_SELECT_SongDuration, 0, 0, 0, 0);
    SongSelect_LABEL_SONG_SELECT_SongDuration->fn->setString(SongSelect_LABEL_SONG_SELECT_SongDuration, (leString*)&string_SONG_SELECT_SongDuration);
    SongSelect_PANEL_SONG_SELECT_SONG_INFO->fn->addChild(SongSelect_PANEL_SONG_SELECT_SONG_INFO, (leWidget*)SongSelect_LABEL_SONG_SELECT_SongDuration);

    SongSelect_PANEL_SONG_SELECT_RIGHT = leWidget_New();
    SongSelect_PANEL_SONG_SELECT_RIGHT->fn->setPosition(SongSelect_PANEL_SONG_SELECT_RIGHT, 876, 0);
    SongSelect_PANEL_SONG_SELECT_RIGHT->fn->setSize(SongSelect_PANEL_SONG_SELECT_RIGHT, 224, 594);
    SongSelect_PANEL_SONG_SELECT_RIGHT->fn->setScheme(SongSelect_PANEL_SONG_SELECT_RIGHT, &SCHEME_BACKGROUND);
    SongSelect_PANEL_SONG_SELECT_RIGHT->fn->setBackgroundType(SongSelect_PANEL_SONG_SELECT_RIGHT, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_PANEL_SONG_SELECT_RIGHT->fn->setBorderType(SongSelect_PANEL_SONG_SELECT_RIGHT, LE_WIDGET_BORDER_LINE);
    SongSelect_PANEL_SONG_SELECT_BOTTOM->fn->addChild(SongSelect_PANEL_SONG_SELECT_BOTTOM, (leWidget*)SongSelect_PANEL_SONG_SELECT_RIGHT);

    SongSelect_PANEL_SONG_SELECT_DIFFICULTY = leWidget_New();
    SongSelect_PANEL_SONG_SELECT_DIFFICULTY->fn->setPosition(SongSelect_PANEL_SONG_SELECT_DIFFICULTY, 16, 16);
    SongSelect_PANEL_SONG_SELECT_DIFFICULTY->fn->setSize(SongSelect_PANEL_SONG_SELECT_DIFFICULTY, 192, 224);
    SongSelect_PANEL_SONG_SELECT_DIFFICULTY->fn->setBackgroundType(SongSelect_PANEL_SONG_SELECT_DIFFICULTY, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_PANEL_SONG_SELECT_RIGHT->fn->addChild(SongSelect_PANEL_SONG_SELECT_RIGHT, (leWidget*)SongSelect_PANEL_SONG_SELECT_DIFFICULTY);

    SongSelect_LABEL_SONG_SELECT_DIFFICULTY = leLabelWidget_New();
    SongSelect_LABEL_SONG_SELECT_DIFFICULTY->fn->setPosition(SongSelect_LABEL_SONG_SELECT_DIFFICULTY, 0, 4);
    SongSelect_LABEL_SONG_SELECT_DIFFICULTY->fn->setSize(SongSelect_LABEL_SONG_SELECT_DIFFICULTY, 192, 16);
    SongSelect_LABEL_SONG_SELECT_DIFFICULTY->fn->setScheme(SongSelect_LABEL_SONG_SELECT_DIFFICULTY, &SCHEME_TEXT_GRAY_71717B);
    SongSelect_LABEL_SONG_SELECT_DIFFICULTY->fn->setBackgroundType(SongSelect_LABEL_SONG_SELECT_DIFFICULTY, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_LABEL_SONG_SELECT_DIFFICULTY->fn->setVAlignment(SongSelect_LABEL_SONG_SELECT_DIFFICULTY, LE_VALIGN_TOP);
    SongSelect_LABEL_SONG_SELECT_DIFFICULTY->fn->setMargins(SongSelect_LABEL_SONG_SELECT_DIFFICULTY, 0, 0, 0, 0);
    SongSelect_LABEL_SONG_SELECT_DIFFICULTY->fn->setString(SongSelect_LABEL_SONG_SELECT_DIFFICULTY, (leString*)&string_SONG_SELECT_DIFFICULTY);
    SongSelect_PANEL_SONG_SELECT_DIFFICULTY->fn->addChild(SongSelect_PANEL_SONG_SELECT_DIFFICULTY, (leWidget*)SongSelect_LABEL_SONG_SELECT_DIFFICULTY);

    SongSelect_BUTTON_SONG_SELECT_EASY = leButtonWidget_New();
    SongSelect_BUTTON_SONG_SELECT_EASY->fn->setPosition(SongSelect_BUTTON_SONG_SELECT_EASY, 0, 32);
    SongSelect_BUTTON_SONG_SELECT_EASY->fn->setSize(SongSelect_BUTTON_SONG_SELECT_EASY, 192, 42);
    SongSelect_BUTTON_SONG_SELECT_EASY->fn->setScheme(SongSelect_BUTTON_SONG_SELECT_EASY, &SCHEME_BUTTON_DIFFICULTY);
    SongSelect_BUTTON_SONG_SELECT_EASY->fn->setBorderType(SongSelect_BUTTON_SONG_SELECT_EASY, LE_WIDGET_BORDER_LINE);
    SongSelect_BUTTON_SONG_SELECT_EASY->fn->setVAlignment(SongSelect_BUTTON_SONG_SELECT_EASY, LE_VALIGN_TOP);
    SongSelect_BUTTON_SONG_SELECT_EASY->fn->setMargins(SongSelect_BUTTON_SONG_SELECT_EASY, 4, 11, 4, 4);
    SongSelect_BUTTON_SONG_SELECT_EASY->fn->setString(SongSelect_BUTTON_SONG_SELECT_EASY, (leString*)&string_SONG_SELECT_EASY);
    SongSelect_BUTTON_SONG_SELECT_EASY->fn->setPressedOffset(SongSelect_BUTTON_SONG_SELECT_EASY, 0);
    SongSelect_PANEL_SONG_SELECT_DIFFICULTY->fn->addChild(SongSelect_PANEL_SONG_SELECT_DIFFICULTY, (leWidget*)SongSelect_BUTTON_SONG_SELECT_EASY);

    SongSelect_BUTTON_SONG_SELECT_MEDIUM = leButtonWidget_New();
    SongSelect_BUTTON_SONG_SELECT_MEDIUM->fn->setPosition(SongSelect_BUTTON_SONG_SELECT_MEDIUM, 0, 82);
    SongSelect_BUTTON_SONG_SELECT_MEDIUM->fn->setSize(SongSelect_BUTTON_SONG_SELECT_MEDIUM, 192, 42);
    SongSelect_BUTTON_SONG_SELECT_MEDIUM->fn->setScheme(SongSelect_BUTTON_SONG_SELECT_MEDIUM, &SCHEME_BUTTON_DIFFICULTY);
    SongSelect_BUTTON_SONG_SELECT_MEDIUM->fn->setBorderType(SongSelect_BUTTON_SONG_SELECT_MEDIUM, LE_WIDGET_BORDER_LINE);
    SongSelect_BUTTON_SONG_SELECT_MEDIUM->fn->setVAlignment(SongSelect_BUTTON_SONG_SELECT_MEDIUM, LE_VALIGN_TOP);
    SongSelect_BUTTON_SONG_SELECT_MEDIUM->fn->setMargins(SongSelect_BUTTON_SONG_SELECT_MEDIUM, 4, 11, 4, 4);
    SongSelect_BUTTON_SONG_SELECT_MEDIUM->fn->setString(SongSelect_BUTTON_SONG_SELECT_MEDIUM, (leString*)&string_SONG_SELECT_MEDIUM);
    SongSelect_BUTTON_SONG_SELECT_MEDIUM->fn->setPressedOffset(SongSelect_BUTTON_SONG_SELECT_MEDIUM, 0);
    SongSelect_PANEL_SONG_SELECT_DIFFICULTY->fn->addChild(SongSelect_PANEL_SONG_SELECT_DIFFICULTY, (leWidget*)SongSelect_BUTTON_SONG_SELECT_MEDIUM);

    SongSelect_BUTTON_SONG_SELECT_HARD = leButtonWidget_New();
    SongSelect_BUTTON_SONG_SELECT_HARD->fn->setPosition(SongSelect_BUTTON_SONG_SELECT_HARD, 0, 132);
    SongSelect_BUTTON_SONG_SELECT_HARD->fn->setSize(SongSelect_BUTTON_SONG_SELECT_HARD, 192, 42);
    SongSelect_BUTTON_SONG_SELECT_HARD->fn->setScheme(SongSelect_BUTTON_SONG_SELECT_HARD, &SCHEME_BUTTON_DIFFICULTY);
    SongSelect_BUTTON_SONG_SELECT_HARD->fn->setBorderType(SongSelect_BUTTON_SONG_SELECT_HARD, LE_WIDGET_BORDER_LINE);
    SongSelect_BUTTON_SONG_SELECT_HARD->fn->setVAlignment(SongSelect_BUTTON_SONG_SELECT_HARD, LE_VALIGN_TOP);
    SongSelect_BUTTON_SONG_SELECT_HARD->fn->setMargins(SongSelect_BUTTON_SONG_SELECT_HARD, 4, 11, 4, 4);
    SongSelect_BUTTON_SONG_SELECT_HARD->fn->setString(SongSelect_BUTTON_SONG_SELECT_HARD, (leString*)&string_SONG_SELECT_HARD);
    SongSelect_BUTTON_SONG_SELECT_HARD->fn->setPressedOffset(SongSelect_BUTTON_SONG_SELECT_HARD, 0);
    SongSelect_PANEL_SONG_SELECT_DIFFICULTY->fn->addChild(SongSelect_PANEL_SONG_SELECT_DIFFICULTY, (leWidget*)SongSelect_BUTTON_SONG_SELECT_HARD);

    SongSelect_BUTTON_SONG_SELECT_EXPERT = leButtonWidget_New();
    SongSelect_BUTTON_SONG_SELECT_EXPERT->fn->setPosition(SongSelect_BUTTON_SONG_SELECT_EXPERT, 0, 182);
    SongSelect_BUTTON_SONG_SELECT_EXPERT->fn->setSize(SongSelect_BUTTON_SONG_SELECT_EXPERT, 192, 42);
    SongSelect_BUTTON_SONG_SELECT_EXPERT->fn->setScheme(SongSelect_BUTTON_SONG_SELECT_EXPERT, &SCHEME_BUTTON_DIFFICULTY);
    SongSelect_BUTTON_SONG_SELECT_EXPERT->fn->setBorderType(SongSelect_BUTTON_SONG_SELECT_EXPERT, LE_WIDGET_BORDER_LINE);
    SongSelect_BUTTON_SONG_SELECT_EXPERT->fn->setVAlignment(SongSelect_BUTTON_SONG_SELECT_EXPERT, LE_VALIGN_TOP);
    SongSelect_BUTTON_SONG_SELECT_EXPERT->fn->setMargins(SongSelect_BUTTON_SONG_SELECT_EXPERT, 4, 11, 4, 4);
    SongSelect_BUTTON_SONG_SELECT_EXPERT->fn->setString(SongSelect_BUTTON_SONG_SELECT_EXPERT, (leString*)&string_SONG_SELECT_EXPERT);
    SongSelect_BUTTON_SONG_SELECT_EXPERT->fn->setPressedOffset(SongSelect_BUTTON_SONG_SELECT_EXPERT, 0);
    SongSelect_PANEL_SONG_SELECT_DIFFICULTY->fn->addChild(SongSelect_PANEL_SONG_SELECT_DIFFICULTY, (leWidget*)SongSelect_BUTTON_SONG_SELECT_EXPERT);

    SongSelect_PANEL_SONG_SELECT_MODE = leWidget_New();
    SongSelect_PANEL_SONG_SELECT_MODE->fn->setPosition(SongSelect_PANEL_SONG_SELECT_MODE, 16, 252);
    SongSelect_PANEL_SONG_SELECT_MODE->fn->setSize(SongSelect_PANEL_SONG_SELECT_MODE, 192, 158);
    SongSelect_PANEL_SONG_SELECT_MODE->fn->setScheme(SongSelect_PANEL_SONG_SELECT_MODE, &SCHEME_BACKGROUND);
    SongSelect_PANEL_SONG_SELECT_MODE->fn->setBackgroundType(SongSelect_PANEL_SONG_SELECT_MODE, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_PANEL_SONG_SELECT_RIGHT->fn->addChild(SongSelect_PANEL_SONG_SELECT_RIGHT, (leWidget*)SongSelect_PANEL_SONG_SELECT_MODE);

    SongSelect_LABEL_SONG_SELECT_MODE = leLabelWidget_New();
    SongSelect_LABEL_SONG_SELECT_MODE->fn->setPosition(SongSelect_LABEL_SONG_SELECT_MODE, 0, 12);
    SongSelect_LABEL_SONG_SELECT_MODE->fn->setSize(SongSelect_LABEL_SONG_SELECT_MODE, 192, 16);
    SongSelect_LABEL_SONG_SELECT_MODE->fn->setScheme(SongSelect_LABEL_SONG_SELECT_MODE, &SCHEME_TEXT_GRAY_71717B);
    SongSelect_LABEL_SONG_SELECT_MODE->fn->setBackgroundType(SongSelect_LABEL_SONG_SELECT_MODE, LE_WIDGET_BACKGROUND_NONE);
    SongSelect_LABEL_SONG_SELECT_MODE->fn->setVAlignment(SongSelect_LABEL_SONG_SELECT_MODE, LE_VALIGN_TOP);
    SongSelect_LABEL_SONG_SELECT_MODE->fn->setMargins(SongSelect_LABEL_SONG_SELECT_MODE, 0, 0, 0, 0);
    SongSelect_LABEL_SONG_SELECT_MODE->fn->setString(SongSelect_LABEL_SONG_SELECT_MODE, (leString*)&string_LABEL_SONG_SELECT_MODE);
    SongSelect_PANEL_SONG_SELECT_MODE->fn->addChild(SongSelect_PANEL_SONG_SELECT_MODE, (leWidget*)SongSelect_LABEL_SONG_SELECT_MODE);

    SongSelect_BUTTON_SONG_SELECT_1P_ROBOT = leButtonWidget_New();
    SongSelect_BUTTON_SONG_SELECT_1P_ROBOT->fn->setPosition(SongSelect_BUTTON_SONG_SELECT_1P_ROBOT, 0, 40);
    SongSelect_BUTTON_SONG_SELECT_1P_ROBOT->fn->setSize(SongSelect_BUTTON_SONG_SELECT_1P_ROBOT, 192, 34);
    SongSelect_BUTTON_SONG_SELECT_1P_ROBOT->fn->setScheme(SongSelect_BUTTON_SONG_SELECT_1P_ROBOT, &SCHEME_BUTTON_MODE);
    SongSelect_BUTTON_SONG_SELECT_1P_ROBOT->fn->setBorderType(SongSelect_BUTTON_SONG_SELECT_1P_ROBOT, LE_WIDGET_BORDER_LINE);
    SongSelect_BUTTON_SONG_SELECT_1P_ROBOT->fn->setVAlignment(SongSelect_BUTTON_SONG_SELECT_1P_ROBOT, LE_VALIGN_TOP);
    SongSelect_BUTTON_SONG_SELECT_1P_ROBOT->fn->setMargins(SongSelect_BUTTON_SONG_SELECT_1P_ROBOT, 4, 9, 4, 4);
    SongSelect_BUTTON_SONG_SELECT_1P_ROBOT->fn->setString(SongSelect_BUTTON_SONG_SELECT_1P_ROBOT, (leString*)&string_SONG_SELECT_1P_ROBOT);
    SongSelect_BUTTON_SONG_SELECT_1P_ROBOT->fn->setPressedOffset(SongSelect_BUTTON_SONG_SELECT_1P_ROBOT, 0);
    SongSelect_PANEL_SONG_SELECT_MODE->fn->addChild(SongSelect_PANEL_SONG_SELECT_MODE, (leWidget*)SongSelect_BUTTON_SONG_SELECT_1P_ROBOT);

    SongSelect_BUTTON_SONG_SELECT_1P_HUMAN = leButtonWidget_New();
    SongSelect_BUTTON_SONG_SELECT_1P_HUMAN->fn->setPosition(SongSelect_BUTTON_SONG_SELECT_1P_HUMAN, 0, 82);
    SongSelect_BUTTON_SONG_SELECT_1P_HUMAN->fn->setSize(SongSelect_BUTTON_SONG_SELECT_1P_HUMAN, 192, 34);
    SongSelect_BUTTON_SONG_SELECT_1P_HUMAN->fn->setScheme(SongSelect_BUTTON_SONG_SELECT_1P_HUMAN, &SCHEME_BUTTON_MODE);
    SongSelect_BUTTON_SONG_SELECT_1P_HUMAN->fn->setBorderType(SongSelect_BUTTON_SONG_SELECT_1P_HUMAN, LE_WIDGET_BORDER_LINE);
    SongSelect_BUTTON_SONG_SELECT_1P_HUMAN->fn->setVAlignment(SongSelect_BUTTON_SONG_SELECT_1P_HUMAN, LE_VALIGN_TOP);
    SongSelect_BUTTON_SONG_SELECT_1P_HUMAN->fn->setMargins(SongSelect_BUTTON_SONG_SELECT_1P_HUMAN, 4, 9, 4, 4);
    SongSelect_BUTTON_SONG_SELECT_1P_HUMAN->fn->setString(SongSelect_BUTTON_SONG_SELECT_1P_HUMAN, (leString*)&string_SONG_SELECT_1P_HUMAN);
    SongSelect_BUTTON_SONG_SELECT_1P_HUMAN->fn->setPressedOffset(SongSelect_BUTTON_SONG_SELECT_1P_HUMAN, 0);
    SongSelect_PANEL_SONG_SELECT_MODE->fn->addChild(SongSelect_PANEL_SONG_SELECT_MODE, (leWidget*)SongSelect_BUTTON_SONG_SELECT_1P_HUMAN);

    SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN = leButtonWidget_New();
    SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN->fn->setPosition(SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN, 0, 124);
    SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN->fn->setSize(SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN, 192, 34);
    SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN->fn->setScheme(SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN, &SCHEME_BUTTON_MODE);
    SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN->fn->setBorderType(SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN, LE_WIDGET_BORDER_LINE);
    SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN->fn->setVAlignment(SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN, LE_VALIGN_TOP);
    SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN->fn->setMargins(SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN, 4, 9, 4, 4);
    SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN->fn->setString(SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN, (leString*)&string_SONG_SELECT_2P_ROBOT_vs_HUMAN);
    SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN->fn->setPressedOffset(SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN, 0);
    SongSelect_PANEL_SONG_SELECT_MODE->fn->addChild(SongSelect_PANEL_SONG_SELECT_MODE, (leWidget*)SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN);

    SongSelect_BUTTON_SONG_SELECT_SELECT = leButtonWidget_New();
    SongSelect_BUTTON_SONG_SELECT_SELECT->fn->setPosition(SongSelect_BUTTON_SONG_SELECT_SELECT, 16, 521);
    SongSelect_BUTTON_SONG_SELECT_SELECT->fn->setSize(SongSelect_BUTTON_SONG_SELECT_SELECT, 192, 56);
    SongSelect_BUTTON_SONG_SELECT_SELECT->fn->setScheme(SongSelect_BUTTON_SONG_SELECT_SELECT, &SCHEME_BUTTON_SELECT);
    SongSelect_BUTTON_SONG_SELECT_SELECT->fn->setBorderType(SongSelect_BUTTON_SONG_SELECT_SELECT, LE_WIDGET_BORDER_NONE);
    SongSelect_BUTTON_SONG_SELECT_SELECT->fn->setVAlignment(SongSelect_BUTTON_SONG_SELECT_SELECT, LE_VALIGN_TOP);
    SongSelect_BUTTON_SONG_SELECT_SELECT->fn->setMargins(SongSelect_BUTTON_SONG_SELECT_SELECT, 4, 16, 4, 4);
    SongSelect_BUTTON_SONG_SELECT_SELECT->fn->setString(SongSelect_BUTTON_SONG_SELECT_SELECT, (leString*)&string_SONG_SELECT_SELECT);
    SongSelect_BUTTON_SONG_SELECT_SELECT->fn->setPressedImage(SongSelect_BUTTON_SONG_SELECT_SELECT, (leImage*)&BUTTON_ICON_CHECK);
    SongSelect_BUTTON_SONG_SELECT_SELECT->fn->setReleasedImage(SongSelect_BUTTON_SONG_SELECT_SELECT, (leImage*)&BUTTON_ICON_CHECK);
    SongSelect_BUTTON_SONG_SELECT_SELECT->fn->setImageMargin(SongSelect_BUTTON_SONG_SELECT_SELECT, 9);
    SongSelect_BUTTON_SONG_SELECT_SELECT->fn->setPressedOffset(SongSelect_BUTTON_SONG_SELECT_SELECT, 0);
    SongSelect_PANEL_SONG_SELECT_RIGHT->fn->addChild(SongSelect_PANEL_SONG_SELECT_RIGHT, (leWidget*)SongSelect_BUTTON_SONG_SELECT_SELECT);

    leAddRootWidget(root0, 0);
    leSetLayerColorMode(0, LE_COLOR_MODE_RGB_565);

    initialized = LE_TRUE;

    return LE_SUCCESS;
}

leResult screenShow_SongSelect(void)
{
    if(showing == LE_TRUE)
        return LE_FAILURE;

    SongSelect_OnShow(); // raise event

    showing = LE_TRUE;

    return LE_SUCCESS;
}

void screenUpdate_SongSelect(void)
{
    root0->fn->setSize(root0, root0->rect.width, root0->rect.height);
}

void screenHide_SongSelect(void)
{
    showing = LE_FALSE;
}

void screenDestroy_SongSelect(void)
{
    if(initialized == LE_FALSE)
        return;

    leRemoveRootWidget(root0, 0);
    leWidget_Delete(root0);
    root0 = NULL;

    SongSelect_PANEL_SONG_SELECT = NULL;
    SongSelect_PANEL_SONG_SELECT_TOP = NULL;
    SongSelect_PANEL_SONG_SELECT_BOTTOM = NULL;
    SongSelect_LABEL_SELECT_SONG = NULL;
    SongSelect_BUTTON_SONG_SELECT_CLOSE = NULL;
    SongSelect_PANEL_SONG_SELECT_LEFT = NULL;
    SongSelect_PANEL_SONG_SELECT_CENTER = NULL;
    SongSelect_PANEL_SONG_SELECT_RIGHT = NULL;
    SongSelect_PANEL_SETLIST = NULL;
    SongSelect_LABEL_SETLIST = NULL;
    SongSelect_IMAGE_ALBUM_ART = NULL;
    SongSelect_PANEL_ALBUM_ART_OVERLAY = NULL;
    SongSelect_LABEL_SONG_SELECT_SONG_LEVEL = NULL;
    SongSelect_LABEL_SONG_SELECT_SONG_TITLE = NULL;
    SongSelect_LABEL_SONG_SELECT_SONG_ARTIST = NULL;
    SongSelect_PANEL_SONG_SELECT_SONG_INFO = NULL;
    SongSelect_LABEL_SONG_SELECT_ALBUM = NULL;
    SongSelect_LABEL_SONG_SELECT_SongAlbum = NULL;
    SongSelect_LABEL_SONG_SELECT_YEAR = NULL;
    SongSelect_LABEL_SONG_SELECT_SongYear = NULL;
    SongSelect_LABEL_SONG_SELECT_GENRE = NULL;
    SongSelect_LABEL_SONG_SELECT_SongGenre = NULL;
    SongSelect_LABEL_SONG_SELECT_DURATION = NULL;
    SongSelect_LABEL_SONG_SELECT_SongDuration = NULL;
    SongSelect_PANEL_SONG_SELECT_DIFFICULTY = NULL;
    SongSelect_PANEL_SONG_SELECT_MODE = NULL;
    SongSelect_BUTTON_SONG_SELECT_SELECT = NULL;
    SongSelect_LABEL_SONG_SELECT_DIFFICULTY = NULL;
    SongSelect_BUTTON_SONG_SELECT_EASY = NULL;
    SongSelect_BUTTON_SONG_SELECT_MEDIUM = NULL;
    SongSelect_BUTTON_SONG_SELECT_HARD = NULL;
    SongSelect_BUTTON_SONG_SELECT_EXPERT = NULL;
    SongSelect_LABEL_SONG_SELECT_MODE = NULL;
    SongSelect_BUTTON_SONG_SELECT_1P_ROBOT = NULL;
    SongSelect_BUTTON_SONG_SELECT_1P_HUMAN = NULL;
    SongSelect_BUTTON_SONG_SELECT_2P_ROBOT_vs_HUMAN = NULL;

    initialized = LE_FALSE;
}

leWidget* screenGetRoot_SongSelect(uint32_t lyrIdx)
{
    if(lyrIdx >= LE_LAYER_COUNT)
        return NULL;

    switch(lyrIdx)
    {
        case 0:
        {
            return root0;
        }
        default:
        {
            return NULL;
        }
    }
}

