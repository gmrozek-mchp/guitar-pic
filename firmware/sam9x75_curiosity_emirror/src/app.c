/*******************************************************************************
  MPLAB Harmony Application Source File

  Company:
    Microchip Technology Inc.

  File Name:
    app.c

  Summary:
    This file contains the source code for the MPLAB Harmony application.

  Description:
    This file contains the source code for the MPLAB Harmony application.  It
    implements the logic of the application's state machine and it may call
    API routines of other MPLAB Harmony modules in the system, such as drivers,
    system services, and middleware.  However, it does not call any of the
    system interfaces (such as the "Initialize" and "Tasks" functions) of any of
    the modules in the system or make any assumptions about when those functions
    are called.  That is the responsibility of the configuration-specific system
    files.
 *******************************************************************************/

#include "app.h"

#define ISC_CANVAS_ID 0
#define ISC_CANVAS_LAYER 1

#define ISC_GUI_ID 1
#define ISC_GUI_LAYER 2

APP_DATA appData;
static leDynamicString * appDynamicString;
SYS_TIME_HANDLE MoveTimer = SYS_TIME_HANDLE_INVALID;

static void HEO_Layer_Scale(int width, int height, int cwidth, int cheight);

void camera_callback(uintptr_t context) {
    uint32_t addr = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    if (context == SYS_MODULE_OBJ_INVALID)
        return;

    SYS_MODULE_OBJ object = (SYS_MODULE_OBJ) context;

    CAMERA_Get_Frame(object, &addr, &width, &height);
    if (addr != 0) {
        gfxcSetPixelBuffer(ISC_CANVAS_ID, width, height, GFX_COLOR_MODE_RGB_565, (void *) addr);
        gfxcCanvasUpdate(ISC_CANVAS_ID);
    }
}

void event_Screen0_zoom_in_OnReleased(leButtonWidget* btn) {
    static bool zoomFlag = true;
    if(zoomFlag) {
        HEO_Layer_Scale(1920, 1080, 1280, 720);
        Screen0_zoom_in->fn->setReleasedImage(Screen0_zoom_in, (leImage*)&zoom_out);
    } else {
        HEO_Layer_Scale(1280, 720, 1280, 720);
        Screen0_zoom_in->fn->setReleasedImage(Screen0_zoom_in, (leImage*)&zoom_in);
    }
    zoomFlag = (!zoomFlag);
}

void event_Screen0_zoom_out_OnReleased(leButtonWidget* btn) {
    static int flip = 0;
    if (sysObj.devCamera != SYS_MODULE_OBJ_INVALID) {
        if (flip == 0)
            CAMERA_Flip(sysObj.devCamera, 1);
        else if(flip == 1)
            CAMERA_Flip(sysObj.devCamera, 2);
        else if(flip == 2)
            CAMERA_Flip(sysObj.devCamera, 3);
        else if(flip == 3)
            CAMERA_Flip(sysObj.devCamera, 0);
     
        flip++;
        if(flip > 3)
           flip = 0;
    }
}

/* Event filter for manual gauge control */
static leBool CamWindowMove_filterEvent(leWidget* target, leWidgetEvent* evt, void* data) {
    static lePoint lastPnt;
    leBool retval = LE_FALSE;
    lePoint pnt;

    pnt.x = ((leWidgetEvent_TouchDown *) evt)->x;
    pnt.y = ((leWidgetEvent_TouchDown *) evt)->y;

    switch (evt->id) {
            /* touch down and move events */
        case LE_EVENT_TOUCH_MOVE:
        case LE_EVENT_TOUCH_DOWN:
        {
            evt->accepted = LE_TRUE;
            evt->owner = target;
            /* touch in map area */
            if (pnt.y > 60 && pnt.y <= 480) {
                int xpos, ypos;

                gfxcGetWindowPosition(ISC_CANVAS_ID, &xpos, &ypos);

                xpos += (pnt.x - lastPnt.x);
                ypos += (pnt.y - lastPnt.y);

                if ((xpos >= -480 && xpos <= 0) &&
                    (ypos >= -240 && ypos <= 0)) {
                    gfxcSetWindowPosition(ISC_CANVAS_ID, xpos, ypos);
                    gfxcCanvasUpdate(ISC_CANVAS_ID);
                }
                lastPnt.x = pnt.x;
                lastPnt.y = pnt.y;
            }
            retval = LE_TRUE;
            break;
        }
            /* Nothing to do on touch up events */
        case LE_EVENT_TOUCH_UP:
        {
            evt->accepted = LE_TRUE;
            retval = LE_TRUE;
            break;
        }
        default:
            break;
    }
    return retval;
}

static leWidgetEventFilter CamWindowMove_eventFilter =
{
    CamWindowMove_filterEvent,
    NULL
};

void Screen0_OnHide(void) {
    /* Remove event filter when hiding screen */
    Screen0_CamTouchPanel->fn->removeEventFilter(Screen0_CamTouchPanel, CamWindowMove_eventFilter);
}

void Screen0_OnUpdate(void) {
}

void Screen0_OnShow(void) {
    appDynamicString = leDynamicString_New(); //Allocate dynamic string object, must be freed with leString_Delete
    appDynamicString->fn->setFont(appDynamicString, (leFont*) & NotoSans_Regular); //Set Font
    
    /* Install event filter to the event filter panel (pnlEventFilter) */
    Screen0_CamTouchPanel->fn->installEventFilter(Screen0_CamTouchPanel, CamWindowMove_eventFilter);
}

static void HEO_Layer_Scale(int width, int height, int cwidth, int cheight) {
    XLCDC_HEO_RGB_SURFACE surface;
    int xpos, ypos;
                    
    gfxcGetWindowPosition(ISC_CANVAS_ID, &xpos, &ypos);
    
    surface.colorMode = GFX_COLOR_MODE_RGB_565;
    surface.imageSizeX = cwidth;
    surface.imageSizeY = cheight;
    surface.windowSizeX = width;
    surface.windowSizeY = height;
    surface.windowStartX = xpos;
    surface.windowStartY = ypos;
    surface.imageAddress = NULL;
    surface.scaleToWindow = true;

    XLCDC_DisplayHEORGBSurface(&surface);    
}

static void user_button_callback(PIO_PIN pin, uintptr_t context)
{
    if (context == SYS_MODULE_OBJ_INVALID)
        return;
    
    PIO_PinInterruptDisable(USER_BTN_PIN);

    PIO_PinInterruptEnable(USER_BTN_PIN);
}

void update_fps(uintptr_t context){
    char cStrBuff[8];
    static uint32_t fps = 0;
    static uint32_t curr_fps = 0;
    
    curr_fps = CAMERA_Get_FPS();
    if ( curr_fps != fps)
    {
        fps = curr_fps;
        sprintf(cStrBuff, "%ld", fps);
        appDynamicString->fn->setFromCStr(appDynamicString, cStrBuff);  //Set string data from C-string
        Screen0_FpsNoLabelWidget->fn->setString(Screen0_FpsNoLabelWidget, (leString *) appDynamicString); //Update label widget string
    }
}

void APP_Initialize(void) {
    /* Place the App state machine in its initial state. */
    appData.state = APP_STATE_INIT;
    uint32_t addr = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    CAMERA_Get_Frame(sysObj.devCamera, &addr, &width, &height);
    gfxcSetPixelBuffer(ISC_CANVAS_ID, width, height, GFX_COLOR_MODE_RGB_565, (void *) addr);
    gfxcSetLayer(ISC_CANVAS_ID, ISC_CANVAS_LAYER);
    gfxcSetWindowPosition(ISC_CANVAS_ID, 0, 0);
    gfxcSetWindowSize(ISC_CANVAS_ID, 800, 480);
    gfxcShowCanvas(ISC_CANVAS_ID);
    gfxcCanvasUpdate(ISC_CANVAS_ID);

    gfxcSetLayer(ISC_GUI_ID, ISC_GUI_LAYER);
    gfxcShowCanvas(ISC_GUI_ID);
    gfxcCanvasUpdate(ISC_GUI_ID);
}

void APP_Tasks(void) {
    switch (appData.state) {
            /* Application's initial state. */
        case APP_STATE_INIT:
        {
            bool appInitialized = true;
            if (appInitialized) {
#ifndef USER_BTN_PIN
#error "Configure USER_BTN_PIN is not configured in pin configuration"
#endif            
                printf("\n\r USER_BTN_PIN() : %d \n\r", USER_BTN_PIN);
                PIO_PinInterruptEnable(USER_BTN_PIN);
                PIO_PinInterruptCallbackRegister(USER_BTN_PIN, user_button_callback, (uintptr_t) sysObj.devCamera);

                if (MoveTimer != SYS_TIME_HANDLE_INVALID)
                    SYS_TIME_TimerDestroy(MoveTimer);

                MoveTimer = SYS_TIME_CallbackRegisterMS(update_fps,
                        (uintptr_t) NULL,
                        3000,
                        SYS_TIME_PERIODIC);

                CAMERA_Register_CallBack(camera_callback, sysObj.devCamera);
                appData.state = APP_STATE_CAMERA_START;
            }
            break;
        }
        case APP_STATE_CAMERA_OPEN:
        {
            if (sysObj.devCamera != SYS_MODULE_OBJ_INVALID) {
                if (CAMERA_Open(sysObj.devCamera)) {
                    appData.state = APP_STATE_CAMERA_START;
                    SYS_DEBUG_MESSAGE(SYS_ERROR_INFO, "\n\r CAMERA_Open : success \n\r");
                } else {
                    appData.state = APP_STATE_ERROR;
                }
            }
            break;
        }
        case APP_STATE_CAMERA_START:
        {
            if (sysObj.devCamera != SYS_MODULE_OBJ_INVALID) {
                if(CAMERA_Start_Capture(sysObj.devCamera)) {
                    appData.state = APP_STATE_IDLE;
                    printf("\r\t CAMERA_Start_Capture : success \n\r");
                }
            }
            break;
        }
        case APP_STATE_IDLE:
        {
            appData.state = APP_STATE_IDLE;
            break;
        }
        case APP_STATE_ERROR:
        default:
        {
            printf("\r\t APP_STATE_ERROR \n\r");
            break;
        }
    }
}
/*******************************************************************************
 End of File
 */