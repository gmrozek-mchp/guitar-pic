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

#include "FreeRTOS.h"
#include "task.h"

#include "app.h"
#include "definitions.h"
#include "video/video.h"

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
    appData.state = APP_STATE_INIT;

    /* Spawn the video task. xTaskCreate is safe before vTaskStartScheduler;
     * the task runs once the scheduler picks it up. The video module owns
     * its capture-pipeline init (ISC, TC358743, backlight, HEO unbind),
     * the capture/display state machine, and the bridge-status polling. */
    Video_Initialize();

    /* App-side video layout: 720×480 video at (280, 76) on the 1280×800
     * panel — 1:1 with the bridge's typical 480p source, leaves a UI
     * strip below. Set before DisplayShow; the video module has no
     * default window of its own. */
    Video_SetWindow(280u, 76u, 720u, 480u);

    /* Default: arm the capture chain when source locks, and show video. */
    Video_CaptureEnable();
    Video_DisplayShow();
}


/******************************************************************************
  Function:
    void APP_Tasks ( void )

  Remarks:
    See prototype in app.h.
 */

void APP_Tasks ( void )
{
    /* APP is a one-shot launcher — task creation happened in APP_Initialize.
     * Self-delete here so the (1024-word) stack and TCB are released to the
     * idle task. The MCC-generated lAPP_Tasks loop will not iterate again
     * because vTaskDelete(NULL) never returns to its caller. */
    vTaskDelete(NULL);
}


/*******************************************************************************
 End of File
 */
