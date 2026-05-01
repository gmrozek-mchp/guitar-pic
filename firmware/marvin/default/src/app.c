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

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include <stdint.h>
#include <stdio.h>

#include "app.h"
#include "definitions.h"
#include "tc358743.h"
#include "isc_capture.h"

// *****************************************************************************
// *****************************************************************************
// Section: Global Data Definitions
// *****************************************************************************
// *****************************************************************************

// *****************************************************************************
/* Application Data

  Summary:
    Holds application data

  Description:
    This structure holds the application's data.

  Remarks:
    This structure should be initialized by the APP_Initialize function.

    Application strings and buffers are be defined outside this structure.
*/

APP_DATA appData;

// *****************************************************************************
// *****************************************************************************
// Section: Application Callback Functions
// *****************************************************************************
// *****************************************************************************

/* TODO:  Add any necessary callback functions.
*/

// *****************************************************************************
// *****************************************************************************
// Section: Application Local Functions
// *****************************************************************************
// *****************************************************************************


/* TODO:  Add any necessary local functions.
*/


// *****************************************************************************
// *****************************************************************************
// Section: Application Initialization and State Machine Functions
// *****************************************************************************
// *****************************************************************************

/*******************************************************************************
  Function:
    void APP_Initialize ( void )

  Remarks:
    See prototype in app.h.
 */

void APP_Initialize ( void )
{
    /* Place the App state machine in its initial state. */
    appData.state = APP_STATE_INIT;

    ISC_Capture_Initialize();
    TC358743_Initialize();
}


/******************************************************************************
  Function:
    void APP_Tasks ( void )

  Remarks:
    See prototype in app.h.
 */

static void app_coordinate_capture(void)
{
    static bool capture_attempted = false;

    bool now_locked = TC358743_IsLocked();
    if (now_locked && !capture_attempted)
    {
        uint16_t w = 0, h = 0;
        if (TC358743_GetDetectedFormat(&w, &h))
        {
            /* RX side ready first, then start source transmit, then arm ISC.
             * Mirrors emirror's working CAMERA_Open + CAMERA_Start_Capture
             * ordering. CSI-RX must be configured before TC358743's stream
             * enable so the D-PHY catches the LP11->HS edge. */
            if (ISC_Capture_Configure(w, h))
            {
                (void)TC358743_EnableStream(true);
                (void)ISC_Capture_Start();
            }
            capture_attempted = true;
        }
    }
    else if (!now_locked && capture_attempted)
    {
        ISC_Capture_Stop();
        (void)TC358743_EnableStream(false);
        capture_attempted = false;
    }
}

static void app_report_fps(void)
{
    static SYS_TIME_HANDLE reportHandle = SYS_TIME_HANDLE_INVALID;
    static uint32_t        last_frames  = 0u;

    if (reportHandle != SYS_TIME_HANDLE_INVALID
        && !SYS_TIME_DelayIsComplete(reportHandle))
    {
        return;
    }

    if (ISC_Capture_IsRunning())
    {
        uint32_t now   = ISC_Capture_FrameCount();
        uint32_t delta = now - last_frames;
        last_frames = now;
        printf("ISC: %lu fps\r\n", (unsigned long)delta);
    }
    else
    {
        last_frames = 0u;
    }

    reportHandle = SYS_TIME_HANDLE_INVALID;
    (void)SYS_TIME_DelayMS(1000u, &reportHandle);
}

void APP_Tasks ( void )
{
    TC358743_Tasks();
    app_coordinate_capture();
    app_report_fps();

    /* Check the application's current state. */
    switch ( appData.state )
    {
        /* Application's initial state. */
        case APP_STATE_INIT:
        {
            bool appInitialized = true;


            if (appInitialized)
            {

                appData.state = APP_STATE_SERVICE_TASKS;
            }
            break;
        }

        case APP_STATE_SERVICE_TASKS:
        {

            break;
        }

        /* TODO: implement your application state machine.*/


        /* The default state should never be executed. */
        default:
        {
            /* TODO: Handle error in application's state machine. */
            break;
        }
    }
}


/*******************************************************************************
 End of File
 */
