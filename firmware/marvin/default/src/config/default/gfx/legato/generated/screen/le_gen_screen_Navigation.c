#include "gfx/legato/generated/screen/le_gen_screen_Navigation.h"

// screen member widget declarations
static leWidget* root0;

leWidget* Navigation_PANEL_NAVIGATION;
leWidget* Navigation_PANEL_NAVIGATION_TOP;
leWidget* Navigation_PANEL_NAVIGATION_MIDDLE;
leWidget* Navigation_PANEL_NAVIGATION_BOTTOM;
leLabelWidget* Navigation_LABEL_NAVIGATION;
leLabelWidget* Navigation_LABEL_NAV_SUB_HEADING;
leButtonWidget* Navigation_BUTTON_NAV_DASHBOARD;
leButtonWidget* Navigation_BUTTON_NAV_LOGS;
leButtonWidget* Navigation_BUTTON_NAV_PERFORMANCE;
leButtonWidget* Navigation_BUTTON_NAV_SYSTEM_INFO;
leButtonWidget* Navigation_BUTTON_NAV_DIAGNOSTICS;
leButtonWidget* Navigation_BUTTON_NAV_SETTINGS;
leWidget* Navigation_panel_Container_196;
leWidget* Navigation_panel_Container_197;
leWidget* Navigation_panel_Container_198;
leWidget* Navigation_panel_Container_199;
leWidget* Navigation_panel_Container_200;
leLabelWidget* Navigation_label_STATUS;
leLabelWidget* Navigation_label_Connected;

static leBool initialized = LE_FALSE;
static leBool showing = LE_FALSE;

leResult screenInit_Navigation(void)
{
    if(initialized == LE_TRUE)
        return LE_FAILURE;

    // layer 0
    root0 = leWidget_New();
    root0->fn->setSize(root0, 320, 800);
    root0->fn->setBackgroundType(root0, LE_WIDGET_BACKGROUND_NONE);
    root0->fn->setMargins(root0, 0, 0, 0, 0);
    root0->flags |= LE_WIDGET_IGNOREEVENTS;
    root0->flags |= LE_WIDGET_IGNOREPICK;

    Navigation_PANEL_NAVIGATION = leWidget_New();
    Navigation_PANEL_NAVIGATION->fn->setPosition(Navigation_PANEL_NAVIGATION, 0, 0);
    Navigation_PANEL_NAVIGATION->fn->setSize(Navigation_PANEL_NAVIGATION, 320, 800);
    Navigation_PANEL_NAVIGATION->fn->setEnabled(Navigation_PANEL_NAVIGATION, LE_FALSE);
    Navigation_PANEL_NAVIGATION->fn->setVisible(Navigation_PANEL_NAVIGATION, LE_FALSE);
    Navigation_PANEL_NAVIGATION->fn->setScheme(Navigation_PANEL_NAVIGATION, &SCHEME_NAV_BUTTON_UNSELECTED);
    Navigation_PANEL_NAVIGATION->fn->setBorderType(Navigation_PANEL_NAVIGATION, LE_WIDGET_BORDER_LINE);
    root0->fn->addChild(root0, (leWidget*)Navigation_PANEL_NAVIGATION);

    Navigation_PANEL_NAVIGATION_TOP = leWidget_New();
    Navigation_PANEL_NAVIGATION_TOP->fn->setPosition(Navigation_PANEL_NAVIGATION_TOP, 0, 0);
    Navigation_PANEL_NAVIGATION_TOP->fn->setSize(Navigation_PANEL_NAVIGATION_TOP, 319, 97);
    Navigation_PANEL_NAVIGATION_TOP->fn->setScheme(Navigation_PANEL_NAVIGATION_TOP, &SCHEME_PANEL);
    Navigation_PANEL_NAVIGATION_TOP->fn->setBackgroundType(Navigation_PANEL_NAVIGATION_TOP, LE_WIDGET_BACKGROUND_NONE);
    Navigation_PANEL_NAVIGATION_TOP->fn->setBorderType(Navigation_PANEL_NAVIGATION_TOP, LE_WIDGET_BORDER_LINE);
    Navigation_PANEL_NAVIGATION->fn->addChild(Navigation_PANEL_NAVIGATION, (leWidget*)Navigation_PANEL_NAVIGATION_TOP);

    Navigation_LABEL_NAVIGATION = leLabelWidget_New();
    Navigation_LABEL_NAVIGATION->fn->setPosition(Navigation_LABEL_NAVIGATION, 24, 24);
    Navigation_LABEL_NAVIGATION->fn->setSize(Navigation_LABEL_NAVIGATION, 271, 28);
    Navigation_LABEL_NAVIGATION->fn->setScheme(Navigation_LABEL_NAVIGATION, &SCHEME_TEXT_WHITE);
    Navigation_LABEL_NAVIGATION->fn->setBackgroundType(Navigation_LABEL_NAVIGATION, LE_WIDGET_BACKGROUND_NONE);
    Navigation_LABEL_NAVIGATION->fn->setVAlignment(Navigation_LABEL_NAVIGATION, LE_VALIGN_TOP);
    Navigation_LABEL_NAVIGATION->fn->setMargins(Navigation_LABEL_NAVIGATION, 0, 0, 0, 0);
    Navigation_LABEL_NAVIGATION->fn->setString(Navigation_LABEL_NAVIGATION, (leString*)&string_figmaStr_NAVIGATION);
    Navigation_PANEL_NAVIGATION_TOP->fn->addChild(Navigation_PANEL_NAVIGATION_TOP, (leWidget*)Navigation_LABEL_NAVIGATION);

    Navigation_LABEL_NAV_SUB_HEADING = leLabelWidget_New();
    Navigation_LABEL_NAV_SUB_HEADING->fn->setPosition(Navigation_LABEL_NAV_SUB_HEADING, 24, 56);
    Navigation_LABEL_NAV_SUB_HEADING->fn->setSize(Navigation_LABEL_NAV_SUB_HEADING, 294, 16);
    Navigation_LABEL_NAV_SUB_HEADING->fn->setScheme(Navigation_LABEL_NAV_SUB_HEADING, &SCHEME_TEXT_GRAY_E4E4E7);
    Navigation_LABEL_NAV_SUB_HEADING->fn->setBackgroundType(Navigation_LABEL_NAV_SUB_HEADING, LE_WIDGET_BACKGROUND_NONE);
    Navigation_LABEL_NAV_SUB_HEADING->fn->setVAlignment(Navigation_LABEL_NAV_SUB_HEADING, LE_VALIGN_TOP);
    Navigation_LABEL_NAV_SUB_HEADING->fn->setMargins(Navigation_LABEL_NAV_SUB_HEADING, 0, 0, 0, 0);
    Navigation_LABEL_NAV_SUB_HEADING->fn->setString(Navigation_LABEL_NAV_SUB_HEADING, (leString*)&string_figmaStr_Robot_Controller_v2_1_4);
    Navigation_PANEL_NAVIGATION_TOP->fn->addChild(Navigation_PANEL_NAVIGATION_TOP, (leWidget*)Navigation_LABEL_NAV_SUB_HEADING);

    Navigation_PANEL_NAVIGATION_MIDDLE = leWidget_New();
    Navigation_PANEL_NAVIGATION_MIDDLE->fn->setPosition(Navigation_PANEL_NAVIGATION_MIDDLE, 0, 97);
    Navigation_PANEL_NAVIGATION_MIDDLE->fn->setSize(Navigation_PANEL_NAVIGATION_MIDDLE, 319, 617);
    Navigation_PANEL_NAVIGATION_MIDDLE->fn->setScheme(Navigation_PANEL_NAVIGATION_MIDDLE, &SCHEME_PANEL);
    Navigation_PANEL_NAVIGATION_MIDDLE->fn->setBackgroundType(Navigation_PANEL_NAVIGATION_MIDDLE, LE_WIDGET_BACKGROUND_NONE);
    Navigation_PANEL_NAVIGATION->fn->addChild(Navigation_PANEL_NAVIGATION, (leWidget*)Navigation_PANEL_NAVIGATION_MIDDLE);

    Navigation_BUTTON_NAV_DASHBOARD = leButtonWidget_New();
    Navigation_BUTTON_NAV_DASHBOARD->fn->setPosition(Navigation_BUTTON_NAV_DASHBOARD, 16, 16);
    Navigation_BUTTON_NAV_DASHBOARD->fn->setSize(Navigation_BUTTON_NAV_DASHBOARD, 287, 56);
    Navigation_BUTTON_NAV_DASHBOARD->fn->setScheme(Navigation_BUTTON_NAV_DASHBOARD, &SCHEME_NAV_BUTTON_UNSELECTED);
    Navigation_BUTTON_NAV_DASHBOARD->fn->setBorderType(Navigation_BUTTON_NAV_DASHBOARD, LE_WIDGET_BORDER_NONE);
    Navigation_BUTTON_NAV_DASHBOARD->fn->setHAlignment(Navigation_BUTTON_NAV_DASHBOARD, LE_HALIGN_LEFT);
    Navigation_BUTTON_NAV_DASHBOARD->fn->setMargins(Navigation_BUTTON_NAV_DASHBOARD, 16, 4, 4, 4);
    Navigation_BUTTON_NAV_DASHBOARD->fn->setString(Navigation_BUTTON_NAV_DASHBOARD, (leString*)&string_NAV_BUTTON_Dashboard);
    Navigation_BUTTON_NAV_DASHBOARD->fn->setPressedImage(Navigation_BUTTON_NAV_DASHBOARD, (leImage*)&figmaImg_Icon_11);
    Navigation_BUTTON_NAV_DASHBOARD->fn->setReleasedImage(Navigation_BUTTON_NAV_DASHBOARD, (leImage*)&figmaImg_Icon_11);
    Navigation_BUTTON_NAV_DASHBOARD->fn->setImageMargin(Navigation_BUTTON_NAV_DASHBOARD, 16);
    Navigation_BUTTON_NAV_DASHBOARD->fn->setPressedOffset(Navigation_BUTTON_NAV_DASHBOARD, 0);
    Navigation_PANEL_NAVIGATION_MIDDLE->fn->addChild(Navigation_PANEL_NAVIGATION_MIDDLE, (leWidget*)Navigation_BUTTON_NAV_DASHBOARD);

    Navigation_BUTTON_NAV_LOGS = leButtonWidget_New();
    Navigation_BUTTON_NAV_LOGS->fn->setPosition(Navigation_BUTTON_NAV_LOGS, 16, 80);
    Navigation_BUTTON_NAV_LOGS->fn->setSize(Navigation_BUTTON_NAV_LOGS, 287, 56);
    Navigation_BUTTON_NAV_LOGS->fn->setScheme(Navigation_BUTTON_NAV_LOGS, &SCHEME_NAV_BUTTON_UNSELECTED);
    Navigation_BUTTON_NAV_LOGS->fn->setBorderType(Navigation_BUTTON_NAV_LOGS, LE_WIDGET_BORDER_NONE);
    Navigation_BUTTON_NAV_LOGS->fn->setHAlignment(Navigation_BUTTON_NAV_LOGS, LE_HALIGN_LEFT);
    Navigation_BUTTON_NAV_LOGS->fn->setMargins(Navigation_BUTTON_NAV_LOGS, 16, 4, 4, 4);
    Navigation_BUTTON_NAV_LOGS->fn->setString(Navigation_BUTTON_NAV_LOGS, (leString*)&string_NAV_BUTTON_Activity_Logs);
    Navigation_BUTTON_NAV_LOGS->fn->setPressedImage(Navigation_BUTTON_NAV_LOGS, (leImage*)&figmaImg_Icon_12);
    Navigation_BUTTON_NAV_LOGS->fn->setReleasedImage(Navigation_BUTTON_NAV_LOGS, (leImage*)&figmaImg_Icon_12);
    Navigation_BUTTON_NAV_LOGS->fn->setImageMargin(Navigation_BUTTON_NAV_LOGS, 16);
    Navigation_BUTTON_NAV_LOGS->fn->setPressedOffset(Navigation_BUTTON_NAV_LOGS, 0);
    Navigation_PANEL_NAVIGATION_MIDDLE->fn->addChild(Navigation_PANEL_NAVIGATION_MIDDLE, (leWidget*)Navigation_BUTTON_NAV_LOGS);

    Navigation_BUTTON_NAV_PERFORMANCE = leButtonWidget_New();
    Navigation_BUTTON_NAV_PERFORMANCE->fn->setPosition(Navigation_BUTTON_NAV_PERFORMANCE, 16, 144);
    Navigation_BUTTON_NAV_PERFORMANCE->fn->setSize(Navigation_BUTTON_NAV_PERFORMANCE, 287, 56);
    Navigation_BUTTON_NAV_PERFORMANCE->fn->setScheme(Navigation_BUTTON_NAV_PERFORMANCE, &SCHEME_NAV_BUTTON_UNSELECTED);
    Navigation_BUTTON_NAV_PERFORMANCE->fn->setBorderType(Navigation_BUTTON_NAV_PERFORMANCE, LE_WIDGET_BORDER_NONE);
    Navigation_BUTTON_NAV_PERFORMANCE->fn->setHAlignment(Navigation_BUTTON_NAV_PERFORMANCE, LE_HALIGN_LEFT);
    Navigation_BUTTON_NAV_PERFORMANCE->fn->setMargins(Navigation_BUTTON_NAV_PERFORMANCE, 16, 4, 4, 4);
    Navigation_BUTTON_NAV_PERFORMANCE->fn->setString(Navigation_BUTTON_NAV_PERFORMANCE, (leString*)&string_NAV_BUTTON_Performance);
    Navigation_BUTTON_NAV_PERFORMANCE->fn->setPressedImage(Navigation_BUTTON_NAV_PERFORMANCE, (leImage*)&figmaImg_Icon_13);
    Navigation_BUTTON_NAV_PERFORMANCE->fn->setReleasedImage(Navigation_BUTTON_NAV_PERFORMANCE, (leImage*)&figmaImg_Icon_13);
    Navigation_BUTTON_NAV_PERFORMANCE->fn->setImageMargin(Navigation_BUTTON_NAV_PERFORMANCE, 16);
    Navigation_BUTTON_NAV_PERFORMANCE->fn->setPressedOffset(Navigation_BUTTON_NAV_PERFORMANCE, 0);
    Navigation_PANEL_NAVIGATION_MIDDLE->fn->addChild(Navigation_PANEL_NAVIGATION_MIDDLE, (leWidget*)Navigation_BUTTON_NAV_PERFORMANCE);

    Navigation_BUTTON_NAV_SYSTEM_INFO = leButtonWidget_New();
    Navigation_BUTTON_NAV_SYSTEM_INFO->fn->setPosition(Navigation_BUTTON_NAV_SYSTEM_INFO, 16, 208);
    Navigation_BUTTON_NAV_SYSTEM_INFO->fn->setSize(Navigation_BUTTON_NAV_SYSTEM_INFO, 287, 56);
    Navigation_BUTTON_NAV_SYSTEM_INFO->fn->setScheme(Navigation_BUTTON_NAV_SYSTEM_INFO, &SCHEME_NAV_BUTTON_UNSELECTED);
    Navigation_BUTTON_NAV_SYSTEM_INFO->fn->setBorderType(Navigation_BUTTON_NAV_SYSTEM_INFO, LE_WIDGET_BORDER_NONE);
    Navigation_BUTTON_NAV_SYSTEM_INFO->fn->setHAlignment(Navigation_BUTTON_NAV_SYSTEM_INFO, LE_HALIGN_LEFT);
    Navigation_BUTTON_NAV_SYSTEM_INFO->fn->setMargins(Navigation_BUTTON_NAV_SYSTEM_INFO, 16, 4, 4, 4);
    Navigation_BUTTON_NAV_SYSTEM_INFO->fn->setString(Navigation_BUTTON_NAV_SYSTEM_INFO, (leString*)&string_NAV_BUTTON_System_Info);
    Navigation_BUTTON_NAV_SYSTEM_INFO->fn->setPressedImage(Navigation_BUTTON_NAV_SYSTEM_INFO, (leImage*)&figmaImg_Icon_14);
    Navigation_BUTTON_NAV_SYSTEM_INFO->fn->setReleasedImage(Navigation_BUTTON_NAV_SYSTEM_INFO, (leImage*)&figmaImg_Icon_14);
    Navigation_BUTTON_NAV_SYSTEM_INFO->fn->setImageMargin(Navigation_BUTTON_NAV_SYSTEM_INFO, 16);
    Navigation_BUTTON_NAV_SYSTEM_INFO->fn->setPressedOffset(Navigation_BUTTON_NAV_SYSTEM_INFO, 0);
    Navigation_PANEL_NAVIGATION_MIDDLE->fn->addChild(Navigation_PANEL_NAVIGATION_MIDDLE, (leWidget*)Navigation_BUTTON_NAV_SYSTEM_INFO);

    Navigation_BUTTON_NAV_DIAGNOSTICS = leButtonWidget_New();
    Navigation_BUTTON_NAV_DIAGNOSTICS->fn->setPosition(Navigation_BUTTON_NAV_DIAGNOSTICS, 16, 272);
    Navigation_BUTTON_NAV_DIAGNOSTICS->fn->setSize(Navigation_BUTTON_NAV_DIAGNOSTICS, 287, 56);
    Navigation_BUTTON_NAV_DIAGNOSTICS->fn->setScheme(Navigation_BUTTON_NAV_DIAGNOSTICS, &SCHEME_NAV_BUTTON_UNSELECTED);
    Navigation_BUTTON_NAV_DIAGNOSTICS->fn->setBorderType(Navigation_BUTTON_NAV_DIAGNOSTICS, LE_WIDGET_BORDER_NONE);
    Navigation_BUTTON_NAV_DIAGNOSTICS->fn->setHAlignment(Navigation_BUTTON_NAV_DIAGNOSTICS, LE_HALIGN_LEFT);
    Navigation_BUTTON_NAV_DIAGNOSTICS->fn->setMargins(Navigation_BUTTON_NAV_DIAGNOSTICS, 16, 4, 4, 4);
    Navigation_BUTTON_NAV_DIAGNOSTICS->fn->setString(Navigation_BUTTON_NAV_DIAGNOSTICS, (leString*)&string_NAV_BUTTON_Diagnostics);
    Navigation_BUTTON_NAV_DIAGNOSTICS->fn->setPressedImage(Navigation_BUTTON_NAV_DIAGNOSTICS, (leImage*)&figmaImg_Icon_15);
    Navigation_BUTTON_NAV_DIAGNOSTICS->fn->setReleasedImage(Navigation_BUTTON_NAV_DIAGNOSTICS, (leImage*)&figmaImg_Icon_15);
    Navigation_BUTTON_NAV_DIAGNOSTICS->fn->setImageMargin(Navigation_BUTTON_NAV_DIAGNOSTICS, 16);
    Navigation_BUTTON_NAV_DIAGNOSTICS->fn->setPressedOffset(Navigation_BUTTON_NAV_DIAGNOSTICS, 0);
    Navigation_PANEL_NAVIGATION_MIDDLE->fn->addChild(Navigation_PANEL_NAVIGATION_MIDDLE, (leWidget*)Navigation_BUTTON_NAV_DIAGNOSTICS);

    Navigation_BUTTON_NAV_SETTINGS = leButtonWidget_New();
    Navigation_BUTTON_NAV_SETTINGS->fn->setPosition(Navigation_BUTTON_NAV_SETTINGS, 16, 336);
    Navigation_BUTTON_NAV_SETTINGS->fn->setSize(Navigation_BUTTON_NAV_SETTINGS, 287, 56);
    Navigation_BUTTON_NAV_SETTINGS->fn->setScheme(Navigation_BUTTON_NAV_SETTINGS, &SCHEME_NAV_BUTTON_UNSELECTED);
    Navigation_BUTTON_NAV_SETTINGS->fn->setBorderType(Navigation_BUTTON_NAV_SETTINGS, LE_WIDGET_BORDER_NONE);
    Navigation_BUTTON_NAV_SETTINGS->fn->setHAlignment(Navigation_BUTTON_NAV_SETTINGS, LE_HALIGN_LEFT);
    Navigation_BUTTON_NAV_SETTINGS->fn->setMargins(Navigation_BUTTON_NAV_SETTINGS, 16, 4, 4, 4);
    Navigation_BUTTON_NAV_SETTINGS->fn->setString(Navigation_BUTTON_NAV_SETTINGS, (leString*)&string_NAV_BUTTON_Settings);
    Navigation_BUTTON_NAV_SETTINGS->fn->setPressedImage(Navigation_BUTTON_NAV_SETTINGS, (leImage*)&figmaImg_Icon_16);
    Navigation_BUTTON_NAV_SETTINGS->fn->setReleasedImage(Navigation_BUTTON_NAV_SETTINGS, (leImage*)&figmaImg_Icon_16);
    Navigation_BUTTON_NAV_SETTINGS->fn->setImageMargin(Navigation_BUTTON_NAV_SETTINGS, 16);
    Navigation_BUTTON_NAV_SETTINGS->fn->setPressedOffset(Navigation_BUTTON_NAV_SETTINGS, 0);
    Navigation_PANEL_NAVIGATION_MIDDLE->fn->addChild(Navigation_PANEL_NAVIGATION_MIDDLE, (leWidget*)Navigation_BUTTON_NAV_SETTINGS);

    Navigation_PANEL_NAVIGATION_BOTTOM = leWidget_New();
    Navigation_PANEL_NAVIGATION_BOTTOM->fn->setPosition(Navigation_PANEL_NAVIGATION_BOTTOM, 0, 715);
    Navigation_PANEL_NAVIGATION_BOTTOM->fn->setSize(Navigation_PANEL_NAVIGATION_BOTTOM, 319, 85);
    Navigation_PANEL_NAVIGATION_BOTTOM->fn->setScheme(Navigation_PANEL_NAVIGATION_BOTTOM, &SCHEME_PANEL);
    Navigation_PANEL_NAVIGATION_BOTTOM->fn->setBackgroundType(Navigation_PANEL_NAVIGATION_BOTTOM, LE_WIDGET_BACKGROUND_NONE);
    Navigation_PANEL_NAVIGATION_BOTTOM->fn->setBorderType(Navigation_PANEL_NAVIGATION_BOTTOM, LE_WIDGET_BORDER_LINE);
    Navigation_PANEL_NAVIGATION->fn->addChild(Navigation_PANEL_NAVIGATION, (leWidget*)Navigation_PANEL_NAVIGATION_BOTTOM);

    Navigation_panel_Container_196 = leWidget_New();
    Navigation_panel_Container_196->fn->setPosition(Navigation_panel_Container_196, 24, 25);
    Navigation_panel_Container_196->fn->setSize(Navigation_panel_Container_196, 271, 36);
    Navigation_panel_Container_196->fn->setBackgroundType(Navigation_panel_Container_196, LE_WIDGET_BACKGROUND_NONE);
    Navigation_PANEL_NAVIGATION_BOTTOM->fn->addChild(Navigation_PANEL_NAVIGATION_BOTTOM, (leWidget*)Navigation_panel_Container_196);

    Navigation_panel_Container_197 = leWidget_New();
    Navigation_panel_Container_197->fn->setPosition(Navigation_panel_Container_197, 0, 12);
    Navigation_panel_Container_197->fn->setSize(Navigation_panel_Container_197, 12, 12);
    Navigation_panel_Container_197->fn->setScheme(Navigation_panel_Container_197, &panel_limegreen);
    Navigation_panel_Container_196->fn->addChild(Navigation_panel_Container_196, (leWidget*)Navigation_panel_Container_197);

    Navigation_panel_Container_198 = leWidget_New();
    Navigation_panel_Container_198->fn->setPosition(Navigation_panel_Container_198, 24, 0);
    Navigation_panel_Container_198->fn->setSize(Navigation_panel_Container_198, 76, 36);
    Navigation_panel_Container_198->fn->setBackgroundType(Navigation_panel_Container_198, LE_WIDGET_BACKGROUND_NONE);
    Navigation_panel_Container_196->fn->addChild(Navigation_panel_Container_196, (leWidget*)Navigation_panel_Container_198);

    Navigation_panel_Container_199 = leWidget_New();
    Navigation_panel_Container_199->fn->setPosition(Navigation_panel_Container_199, 0, 0);
    Navigation_panel_Container_199->fn->setSize(Navigation_panel_Container_199, 76, 16);
    Navigation_panel_Container_199->fn->setBackgroundType(Navigation_panel_Container_199, LE_WIDGET_BACKGROUND_NONE);
    Navigation_panel_Container_198->fn->addChild(Navigation_panel_Container_198, (leWidget*)Navigation_panel_Container_199);

    Navigation_label_STATUS = leLabelWidget_New();
    Navigation_label_STATUS->fn->setPosition(Navigation_label_STATUS, 0, 0);
    Navigation_label_STATUS->fn->setSize(Navigation_label_STATUS, 44, 16);
    Navigation_label_STATUS->fn->setScheme(Navigation_label_STATUS, &SCHEME_TEXT_GRAY_E4E4E7);
    Navigation_label_STATUS->fn->setBackgroundType(Navigation_label_STATUS, LE_WIDGET_BACKGROUND_NONE);
    Navigation_label_STATUS->fn->setVAlignment(Navigation_label_STATUS, LE_VALIGN_TOP);
    Navigation_label_STATUS->fn->setMargins(Navigation_label_STATUS, 0, 0, 0, 0);
    Navigation_label_STATUS->fn->setString(Navigation_label_STATUS, (leString*)&string_figmaStr_STATUS);
    Navigation_panel_Container_199->fn->addChild(Navigation_panel_Container_199, (leWidget*)Navigation_label_STATUS);

    Navigation_panel_Container_200 = leWidget_New();
    Navigation_panel_Container_200->fn->setPosition(Navigation_panel_Container_200, 0, 16);
    Navigation_panel_Container_200->fn->setSize(Navigation_panel_Container_200, 76, 20);
    Navigation_panel_Container_200->fn->setBackgroundType(Navigation_panel_Container_200, LE_WIDGET_BACKGROUND_NONE);
    Navigation_panel_Container_198->fn->addChild(Navigation_panel_Container_198, (leWidget*)Navigation_panel_Container_200);

    Navigation_label_Connected = leLabelWidget_New();
    Navigation_label_Connected->fn->setPosition(Navigation_label_Connected, 0, 0);
    Navigation_label_Connected->fn->setSize(Navigation_label_Connected, 76, 20);
    Navigation_label_Connected->fn->setScheme(Navigation_label_Connected, &SCHEME_TEXT_WHITE);
    Navigation_label_Connected->fn->setBackgroundType(Navigation_label_Connected, LE_WIDGET_BACKGROUND_NONE);
    Navigation_label_Connected->fn->setVAlignment(Navigation_label_Connected, LE_VALIGN_TOP);
    Navigation_label_Connected->fn->setMargins(Navigation_label_Connected, 0, 0, 0, 0);
    Navigation_label_Connected->fn->setString(Navigation_label_Connected, (leString*)&string_figmaStr_Connected);
    Navigation_panel_Container_200->fn->addChild(Navigation_panel_Container_200, (leWidget*)Navigation_label_Connected);

    leAddRootWidget(root0, 0);
    leSetLayerColorMode(0, LE_COLOR_MODE_RGB_565);

    initialized = LE_TRUE;

    return LE_SUCCESS;
}

leResult screenShow_Navigation(void)
{
    if(showing == LE_TRUE)
        return LE_FAILURE;

    Navigation_OnShow(); // raise event

    showing = LE_TRUE;

    return LE_SUCCESS;
}

void screenUpdate_Navigation(void)
{
    root0->fn->setSize(root0, root0->rect.width, root0->rect.height);
}

void screenHide_Navigation(void)
{
    showing = LE_FALSE;
}

void screenDestroy_Navigation(void)
{
    if(initialized == LE_FALSE)
        return;

    leRemoveRootWidget(root0, 0);
    leWidget_Delete(root0);
    root0 = NULL;

    Navigation_PANEL_NAVIGATION = NULL;
    Navigation_PANEL_NAVIGATION_TOP = NULL;
    Navigation_PANEL_NAVIGATION_MIDDLE = NULL;
    Navigation_PANEL_NAVIGATION_BOTTOM = NULL;
    Navigation_LABEL_NAVIGATION = NULL;
    Navigation_LABEL_NAV_SUB_HEADING = NULL;
    Navigation_BUTTON_NAV_DASHBOARD = NULL;
    Navigation_BUTTON_NAV_LOGS = NULL;
    Navigation_BUTTON_NAV_PERFORMANCE = NULL;
    Navigation_BUTTON_NAV_SYSTEM_INFO = NULL;
    Navigation_BUTTON_NAV_DIAGNOSTICS = NULL;
    Navigation_BUTTON_NAV_SETTINGS = NULL;
    Navigation_panel_Container_196 = NULL;
    Navigation_panel_Container_197 = NULL;
    Navigation_panel_Container_198 = NULL;
    Navigation_panel_Container_199 = NULL;
    Navigation_panel_Container_200 = NULL;
    Navigation_label_STATUS = NULL;
    Navigation_label_Connected = NULL;

    initialized = LE_FALSE;
}

leWidget* screenGetRoot_Navigation(uint32_t lyrIdx)
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

