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
leWidget* Marvin_PANEL_SONG_SELECT_ALBUM_ART;
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

    leRemoveRootWidget(root3, 3);
    leWidget_Delete(root3);
    root3 = NULL;

    Marvin_PANEL_SONG_SELECT_ALBUM_ART = NULL;

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

