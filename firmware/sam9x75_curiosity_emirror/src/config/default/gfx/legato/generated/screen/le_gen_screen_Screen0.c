#include "gfx/legato/generated/screen/le_gen_screen_Screen0.h"

// screen member widget declarations
static leWidget* root0;
static leWidget* root1;

leWidget* Screen0_CamTouchPanel;
leImageWidget* Screen0_mchpLogo;
leLabelWidget* Screen0_FPSLabelWidget;
leLabelWidget* Screen0_FpsNoLabelWidget;
leButtonWidget* Screen0_zoom_in;

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

    Screen0_CamTouchPanel = leWidget_New();
    Screen0_CamTouchPanel->fn->setPosition(Screen0_CamTouchPanel, 0, 0);
    Screen0_CamTouchPanel->fn->setSize(Screen0_CamTouchPanel, 800, 480);
    Screen0_CamTouchPanel->fn->setBackgroundType(Screen0_CamTouchPanel, LE_WIDGET_BACKGROUND_NONE);
    root0->fn->addChild(root0, (leWidget*)Screen0_CamTouchPanel);

    leAddRootWidget(root0, 0);
    leSetLayerColorMode(0, LE_COLOR_MODE_RGBA_8888);

    // layer 1
    root1 = leWidget_New();
    root1->fn->setSize(root1, 800, 60);
    root1->fn->setBackgroundType(root1, LE_WIDGET_BACKGROUND_NONE);
    root1->fn->setMargins(root1, 0, 0, 0, 0);
    root1->flags |= LE_WIDGET_IGNOREEVENTS;
    root1->flags |= LE_WIDGET_IGNOREPICK;

    Screen0_mchpLogo = leImageWidget_New();
    Screen0_mchpLogo->fn->setPosition(Screen0_mchpLogo, 0, 0);
    Screen0_mchpLogo->fn->setSize(Screen0_mchpLogo, 150, 60);
    Screen0_mchpLogo->fn->setBackgroundType(Screen0_mchpLogo, LE_WIDGET_BACKGROUND_NONE);
    Screen0_mchpLogo->fn->setBorderType(Screen0_mchpLogo, LE_WIDGET_BORDER_NONE);
    Screen0_mchpLogo->fn->setImage(Screen0_mchpLogo, (leImage*)&microchip_logo_black);
    root1->fn->addChild(root1, (leWidget*)Screen0_mchpLogo);

    Screen0_FPSLabelWidget = leLabelWidget_New();
    Screen0_FPSLabelWidget->fn->setPosition(Screen0_FPSLabelWidget, 610, 0);
    Screen0_FPSLabelWidget->fn->setSize(Screen0_FPSLabelWidget, 60, 60);
    Screen0_FPSLabelWidget->fn->setBackgroundType(Screen0_FPSLabelWidget, LE_WIDGET_BACKGROUND_NONE);
    Screen0_FPSLabelWidget->fn->setString(Screen0_FPSLabelWidget, (leString*)&string_FPS);
    root1->fn->addChild(root1, (leWidget*)Screen0_FPSLabelWidget);

    Screen0_FpsNoLabelWidget = leLabelWidget_New();
    Screen0_FpsNoLabelWidget->fn->setPosition(Screen0_FpsNoLabelWidget, 670, 0);
    Screen0_FpsNoLabelWidget->fn->setSize(Screen0_FpsNoLabelWidget, 60, 60);
    Screen0_FpsNoLabelWidget->fn->setScheme(Screen0_FpsNoLabelWidget, &clearScheme);
    Screen0_FpsNoLabelWidget->fn->setString(Screen0_FpsNoLabelWidget, (leString*)&string_Nos);
    root1->fn->addChild(root1, (leWidget*)Screen0_FpsNoLabelWidget);

    Screen0_zoom_in = leButtonWidget_New();
    Screen0_zoom_in->fn->setPosition(Screen0_zoom_in, 720, 0);
    Screen0_zoom_in->fn->setSize(Screen0_zoom_in, 80, 60);
    Screen0_zoom_in->fn->setBackgroundType(Screen0_zoom_in, LE_WIDGET_BACKGROUND_NONE);
    Screen0_zoom_in->fn->setBorderType(Screen0_zoom_in, LE_WIDGET_BORDER_NONE);
    Screen0_zoom_in->fn->setPressedImage(Screen0_zoom_in, (leImage*)&zoom_in);
    Screen0_zoom_in->fn->setReleasedImage(Screen0_zoom_in, (leImage*)&zoom_in);
    Screen0_zoom_in->fn->setReleasedEventCallback(Screen0_zoom_in, event_Screen0_zoom_in_OnReleased);
    root1->fn->addChild(root1, (leWidget*)Screen0_zoom_in);

    leAddRootWidget(root1, 1);
    leSetLayerColorMode(1, LE_COLOR_MODE_RGBA_8888);

    Screen0_OnShow(); // raise event

    showing = LE_TRUE;

    return LE_SUCCESS;
}

void screenUpdate_Screen0(void)
{
    root0->fn->setSize(root0, root0->rect.width, root0->rect.height);
    root1->fn->setSize(root1, root1->rect.width, root1->rect.height);

    Screen0_OnUpdate(); // raise event
}

void screenHide_Screen0(void)
{
    Screen0_OnHide(); // raise event


    leRemoveRootWidget(root0, 0);
    leWidget_Delete(root0);
    root0 = NULL;

    Screen0_CamTouchPanel = NULL;

    leRemoveRootWidget(root1, 1);
    leWidget_Delete(root1);
    root1 = NULL;

    Screen0_mchpLogo = NULL;
    Screen0_FPSLabelWidget = NULL;
    Screen0_FpsNoLabelWidget = NULL;
    Screen0_zoom_in = NULL;


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
        case 1:
        {
            return root1;
        }
        default:
        {
            return NULL;
        }
    }
}

