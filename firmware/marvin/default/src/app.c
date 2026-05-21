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
#include "log.h"
#include "usb/usb_host.h"
#include "video/video.h"
#include "detector/detector.h"
#include "actuator/timing_pipeline.h"
#include "actuator/fretboard_link.h"

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

static USB_HOST_EVENT_RESPONSE app_usb_host_event_handler(USB_HOST_EVENT event,
                                                         void *eventData,
                                                         uintptr_t context)
{
    (void)eventData;
    (void)context;
    LOG_INFO("USB host event: %d\r\n", (int)event);
    return USB_HOST_EVENT_RESPONSE_NONE;
}


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

    /* Logging shim: thin wrapper over libc printf with severity filter
     * and FreeRTOS-aware locking. Initialized first so subsequent code
     * (including video task startup) can use LOG_*. Default level is
     * INFO; flip to DEBUG via log_set_level() to enable verbose. */
    log_init(LOG_LEVEL_INFO);

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

    /* Reference detector (cv_marvin_v1) + detector-state bus. M1 stub
     * publisher; real detection logic lands incrementally. Must follow
     * Video_Initialize since cv_marvin_v1 subscribes to the video frame
     * queue from inside its task. Detectors default disabled; explicitly
     * enable + select the active one for the timing pipeline. */
    Detector_Initialize();
    Detector_Enable(DETECTOR_CV_MARVIN_V1);
    Detector_SetActive(DETECTOR_CV_MARVIN_V1);

    /* USB host bring-up — shared across all USB consumers (fretboard CDC
     * link today; future modules may add HID, MSC, etc.). VBUS_AH_PC27/PC31
     * gate external power switches on the SAM9X75 Curiosity. The Harmony
     * driver's portPowerEnable callback is wired but never invoked, so we
     * assert these GPIOs directly. Class-driver attach listeners (CDC etc.)
     * must be registered before USB_HOST_BusEnable, so consumer module
     * init runs first. */
    VBUS_AH_PC27_PowerEnable_Set();
    VBUS_AH_PC31_PowerEnable_Set();
    (void)USB_HOST_EventHandlerSet(app_usb_host_event_handler, 0u);

    /* M2 actuator path: fretboard_link owns the submit queue + USB CDC
     * writer and registers the CDC attach listener. */
    FretboardLink_Initialize();

    USB_HOST_RESULT be = USB_HOST_BusEnable(USB_HOST_BUS_ALL);
    LOG_INFO("USB_HOST_BusEnable -> %d\r\n", (int)be);

    /* timing_pipeline runs the chord-window + strum scheduler against the
     * active detector and pushes the resulting 7-bit mask through
     * FretboardLink_Send. */
    TimingPipeline_Initialize();
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
