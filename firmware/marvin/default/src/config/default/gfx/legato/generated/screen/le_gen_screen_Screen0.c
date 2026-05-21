#include "gfx/legato/generated/screen/le_gen_screen_Screen0.h"

// screen member widget declarations
static leWidget* root0;

leWidget* Screen0_BackgroundPanel;
leImageWidget* Screen0_ImageWidget_0;
leButtonWidget* Screen0_Button_Manual_Green;
leButtonWidget* Screen0_Button_Manual_Red;
leButtonWidget* Screen0_Button_Manual_Yellow;
leButtonWidget* Screen0_Button_Manual_Blue;
leButtonWidget* Screen0_Button_Manual_Orange;
leButtonWidget* Screen0_Button_Manual_StrumDown;
leButtonWidget* Screen0_Button_Manual_StrumUp;
leButtonWidget* Screen0_Button_Manual_Enable;

static leBool initialized = LE_FALSE;
static leBool showing = LE_FALSE;

leResult screenInit_Screen0(void)
{
    if(initialized == LE_TRUE)
        return LE_FAILURE;

    initialized = LE_TRUE;

    return LE_SUCCESS;
}

leResult screenShow_Screen0(void)
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

    Screen0_BackgroundPanel = leWidget_New();
    Screen0_BackgroundPanel->fn->setPosition(Screen0_BackgroundPanel, 0, 0);
    Screen0_BackgroundPanel->fn->setSize(Screen0_BackgroundPanel, 1280, 800);
    Screen0_BackgroundPanel->fn->setScheme(Screen0_BackgroundPanel, &WhiteScheme);
    root0->fn->addChild(root0, (leWidget*)Screen0_BackgroundPanel);

    Screen0_ImageWidget_0 = leImageWidget_New();
    Screen0_ImageWidget_0->fn->setPosition(Screen0_ImageWidget_0, -1, 0);
    Screen0_ImageWidget_0->fn->setSize(Screen0_ImageWidget_0, 1280, 800);
    Screen0_ImageWidget_0->fn->setBorderType(Screen0_ImageWidget_0, LE_WIDGET_BORDER_NONE);
    Screen0_ImageWidget_0->fn->setImage(Screen0_ImageWidget_0, (leImage*)&Body);
    root0->fn->addChild(root0, (leWidget*)Screen0_ImageWidget_0);

    Screen0_Button_Manual_Green = leButtonWidget_New();
    Screen0_Button_Manual_Green->fn->setPosition(Screen0_Button_Manual_Green, 325, 646);
    Screen0_Button_Manual_Green->fn->setSize(Screen0_Button_Manual_Green, 100, 50);
    Screen0_Button_Manual_Green->fn->setBorderType(Screen0_Button_Manual_Green, LE_WIDGET_BORDER_LINE);
    Screen0_Button_Manual_Green->fn->setPressedEventCallback(Screen0_Button_Manual_Green, event_Screen0_Button_Manual_Green_OnPressed);
    Screen0_Button_Manual_Green->fn->setReleasedEventCallback(Screen0_Button_Manual_Green, event_Screen0_Button_Manual_Green_OnReleased);
    root0->fn->addChild(root0, (leWidget*)Screen0_Button_Manual_Green);

    Screen0_Button_Manual_Red = leButtonWidget_New();
    Screen0_Button_Manual_Red->fn->setPosition(Screen0_Button_Manual_Red, 452, 645);
    Screen0_Button_Manual_Red->fn->setSize(Screen0_Button_Manual_Red, 100, 50);
    Screen0_Button_Manual_Red->fn->setBorderType(Screen0_Button_Manual_Red, LE_WIDGET_BORDER_LINE);
    Screen0_Button_Manual_Red->fn->setPressedEventCallback(Screen0_Button_Manual_Red, event_Screen0_Button_Manual_Red_OnPressed);
    Screen0_Button_Manual_Red->fn->setReleasedEventCallback(Screen0_Button_Manual_Red, event_Screen0_Button_Manual_Red_OnReleased);
    root0->fn->addChild(root0, (leWidget*)Screen0_Button_Manual_Red);

    Screen0_Button_Manual_Yellow = leButtonWidget_New();
    Screen0_Button_Manual_Yellow->fn->setPosition(Screen0_Button_Manual_Yellow, 587, 644);
    Screen0_Button_Manual_Yellow->fn->setSize(Screen0_Button_Manual_Yellow, 100, 50);
    Screen0_Button_Manual_Yellow->fn->setBorderType(Screen0_Button_Manual_Yellow, LE_WIDGET_BORDER_LINE);
    Screen0_Button_Manual_Yellow->fn->setPressedEventCallback(Screen0_Button_Manual_Yellow, event_Screen0_Button_Manual_Yellow_OnPressed);
    Screen0_Button_Manual_Yellow->fn->setReleasedEventCallback(Screen0_Button_Manual_Yellow, event_Screen0_Button_Manual_Yellow_OnReleased);
    root0->fn->addChild(root0, (leWidget*)Screen0_Button_Manual_Yellow);

    Screen0_Button_Manual_Blue = leButtonWidget_New();
    Screen0_Button_Manual_Blue->fn->setPosition(Screen0_Button_Manual_Blue, 726, 642);
    Screen0_Button_Manual_Blue->fn->setSize(Screen0_Button_Manual_Blue, 100, 50);
    Screen0_Button_Manual_Blue->fn->setBorderType(Screen0_Button_Manual_Blue, LE_WIDGET_BORDER_LINE);
    Screen0_Button_Manual_Blue->fn->setPressedEventCallback(Screen0_Button_Manual_Blue, event_Screen0_Button_Manual_Blue_OnPressed);
    Screen0_Button_Manual_Blue->fn->setReleasedEventCallback(Screen0_Button_Manual_Blue, event_Screen0_Button_Manual_Blue_OnReleased);
    root0->fn->addChild(root0, (leWidget*)Screen0_Button_Manual_Blue);

    Screen0_Button_Manual_Orange = leButtonWidget_New();
    Screen0_Button_Manual_Orange->fn->setPosition(Screen0_Button_Manual_Orange, 861, 642);
    Screen0_Button_Manual_Orange->fn->setSize(Screen0_Button_Manual_Orange, 100, 50);
    Screen0_Button_Manual_Orange->fn->setBorderType(Screen0_Button_Manual_Orange, LE_WIDGET_BORDER_LINE);
    Screen0_Button_Manual_Orange->fn->setPressedEventCallback(Screen0_Button_Manual_Orange, event_Screen0_Button_Manual_Orange_OnPressed);
    Screen0_Button_Manual_Orange->fn->setReleasedEventCallback(Screen0_Button_Manual_Orange, event_Screen0_Button_Manual_Orange_OnReleased);
    root0->fn->addChild(root0, (leWidget*)Screen0_Button_Manual_Orange);

    Screen0_Button_Manual_StrumDown = leButtonWidget_New();
    Screen0_Button_Manual_StrumDown->fn->setPosition(Screen0_Button_Manual_StrumDown, 437, 732);
    Screen0_Button_Manual_StrumDown->fn->setSize(Screen0_Button_Manual_StrumDown, 100, 50);
    Screen0_Button_Manual_StrumDown->fn->setBorderType(Screen0_Button_Manual_StrumDown, LE_WIDGET_BORDER_LINE);
    Screen0_Button_Manual_StrumDown->fn->setPressedEventCallback(Screen0_Button_Manual_StrumDown, event_Screen0_Button_Manual_StrumDown_OnPressed);
    Screen0_Button_Manual_StrumDown->fn->setReleasedEventCallback(Screen0_Button_Manual_StrumDown, event_Screen0_Button_Manual_StrumDown_OnReleased);
    root0->fn->addChild(root0, (leWidget*)Screen0_Button_Manual_StrumDown);

    Screen0_Button_Manual_StrumUp = leButtonWidget_New();
    Screen0_Button_Manual_StrumUp->fn->setPosition(Screen0_Button_Manual_StrumUp, 687, 732);
    Screen0_Button_Manual_StrumUp->fn->setSize(Screen0_Button_Manual_StrumUp, 100, 50);
    Screen0_Button_Manual_StrumUp->fn->setBorderType(Screen0_Button_Manual_StrumUp, LE_WIDGET_BORDER_LINE);
    Screen0_Button_Manual_StrumUp->fn->setPressedEventCallback(Screen0_Button_Manual_StrumUp, event_Screen0_Button_Manual_StrumUp_OnPressed);
    Screen0_Button_Manual_StrumUp->fn->setReleasedEventCallback(Screen0_Button_Manual_StrumUp, event_Screen0_Button_Manual_StrumUp_OnReleased);
    root0->fn->addChild(root0, (leWidget*)Screen0_Button_Manual_StrumUp);

    Screen0_Button_Manual_Enable = leButtonWidget_New();
    Screen0_Button_Manual_Enable->fn->setPosition(Screen0_Button_Manual_Enable, 887, 567);
    Screen0_Button_Manual_Enable->fn->setSize(Screen0_Button_Manual_Enable, 100, 50);
    Screen0_Button_Manual_Enable->fn->setBorderType(Screen0_Button_Manual_Enable, LE_WIDGET_BORDER_LINE);
    Screen0_Button_Manual_Enable->fn->setToggleable(Screen0_Button_Manual_Enable, LE_TRUE);
    Screen0_Button_Manual_Enable->fn->setReleasedEventCallback(Screen0_Button_Manual_Enable, event_Screen0_Button_Manual_Enable_OnReleased);
    root0->fn->addChild(root0, (leWidget*)Screen0_Button_Manual_Enable);

    leAddRootWidget(root0, 0);
    leSetLayerColorMode(0, LE_COLOR_MODE_RGB_565);

    showing = LE_TRUE;

    return LE_SUCCESS;
}

void screenUpdate_Screen0(void)
{
    root0->fn->setSize(root0, root0->rect.width, root0->rect.height);
}

void screenHide_Screen0(void)
{

    leRemoveRootWidget(root0, 0);
    leWidget_Delete(root0);
    root0 = NULL;

    Screen0_BackgroundPanel = NULL;
    Screen0_ImageWidget_0 = NULL;
    Screen0_Button_Manual_Green = NULL;
    Screen0_Button_Manual_Red = NULL;
    Screen0_Button_Manual_Yellow = NULL;
    Screen0_Button_Manual_Blue = NULL;
    Screen0_Button_Manual_Orange = NULL;
    Screen0_Button_Manual_StrumDown = NULL;
    Screen0_Button_Manual_StrumUp = NULL;
    Screen0_Button_Manual_Enable = NULL;


    showing = LE_FALSE;
}

void screenDestroy_Screen0(void)
{
    if(initialized == LE_FALSE)
        return;

    initialized = LE_FALSE;
}

leWidget* screenGetRoot_Screen0(uint32_t lyrIdx)
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

