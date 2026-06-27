#include "gfx/legato/generated/screen/le_gen_screen_LayerBudget.h"

// screen member widget declarations
static leWidget* root0;
static leWidget* root1;
static leWidget* root2;


static leBool initialized = LE_FALSE;
static leBool showing = LE_FALSE;

leResult screenInit_LayerBudget(void)
{
    if(initialized == LE_TRUE)
        return LE_FAILURE;

    // layer 0
    root0 = leWidget_New();
    root0->fn->setSize(root0, LE_DEFAULT_SCREEN_WIDTH, LE_DEFAULT_SCREEN_HEIGHT);
    root0->fn->setBackgroundType(root0, LE_WIDGET_BACKGROUND_NONE);
    root0->fn->setMargins(root0, 0, 0, 0, 0);
    root0->flags |= LE_WIDGET_IGNOREEVENTS;
    root0->flags |= LE_WIDGET_IGNOREPICK;

    leAddRootWidget(root0, 0);
    leSetLayerColorMode(0, LE_COLOR_MODE_RGB_565);

    // layer 1
    root1 = leWidget_New();
    root1->fn->setSize(root1, LE_DEFAULT_SCREEN_WIDTH, LE_DEFAULT_SCREEN_HEIGHT);
    root1->fn->setBackgroundType(root1, LE_WIDGET_BACKGROUND_NONE);
    root1->fn->setMargins(root1, 0, 0, 0, 0);
    root1->flags |= LE_WIDGET_IGNOREEVENTS;
    root1->flags |= LE_WIDGET_IGNOREPICK;

    leAddRootWidget(root1, 1);
    leSetLayerColorMode(1, LE_COLOR_MODE_RGB_565);

    // layer 2
    root2 = leWidget_New();
    root2->fn->setSize(root2, LE_DEFAULT_SCREEN_WIDTH, LE_DEFAULT_SCREEN_HEIGHT);
    root2->fn->setBackgroundType(root2, LE_WIDGET_BACKGROUND_NONE);
    root2->fn->setMargins(root2, 0, 0, 0, 0);
    root2->flags |= LE_WIDGET_IGNOREEVENTS;
    root2->flags |= LE_WIDGET_IGNOREPICK;

    leAddRootWidget(root2, 2);
    leSetLayerColorMode(2, LE_COLOR_MODE_RGB_565);

    initialized = LE_TRUE;

    return LE_SUCCESS;
}

leResult screenShow_LayerBudget(void)
{
    if(showing == LE_TRUE)
        return LE_FAILURE;

    showing = LE_TRUE;

    return LE_SUCCESS;
}

void screenUpdate_LayerBudget(void)
{
    root0->fn->setSize(root0, root0->rect.width, root0->rect.height);
    root1->fn->setSize(root1, root1->rect.width, root1->rect.height);
    root2->fn->setSize(root2, root2->rect.width, root2->rect.height);
}

void screenHide_LayerBudget(void)
{
    showing = LE_FALSE;
}

void screenDestroy_LayerBudget(void)
{
    if(initialized == LE_FALSE)
        return;

    leRemoveRootWidget(root0, 0);
    leWidget_Delete(root0);
    root0 = NULL;

    leRemoveRootWidget(root1, 1);
    leWidget_Delete(root1);
    root1 = NULL;

    leRemoveRootWidget(root2, 2);
    leWidget_Delete(root2);
    root2 = NULL;

    initialized = LE_FALSE;
}

leWidget* screenGetRoot_LayerBudget(uint32_t lyrIdx)
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
        default:
        {
            return NULL;
        }
    }
}

