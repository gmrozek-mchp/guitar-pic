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
#include "video/video.h"
#include "detector/detector.h"
#include "actuator/timing_pipeline.h"
#include "actuator/fretboard_link.h"
#include "actuator/manual_control.h"
#include "ui/compositor.h"
#include "game/gameplay_engine.h"
#include "console/console.h"
#include "storage/storage.h"
#include "results/results.h"
#include "game/catalog.h"
#include "perf_log/perf_log.h"

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

// *****************************************************************************
// *****************************************************************************
// Section: Application Local Functions
// *****************************************************************************
// *****************************************************************************

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

    /* UI compositor: assign marvin-owned static framebuffers to the Legato
     * canvases and advance the canvas state machine. Runs before the scheduler
     * so the buffers exist (and canvas is RUNNING) before the first render.
     * The Marvin screen's On-Show hook binds the canvases to LCDC layers. */
    Compositor_Initialize();

    /* Spawn the video task. xTaskCreate is safe before vTaskStartScheduler;
     * the task runs once the scheduler picks it up. The video module owns
     * its capture-pipeline init (ISC, TC358743, backlight, HEO unbind),
     * the capture/display state machine, and the bridge-status polling. */
    Video_Initialize();

    /* Per-frame performance log: producer-side queues + drain task.
     * Init the queues here so any producer that posts before the
     * scheduler starts will not crash; the drain task is launched
     * separately by PerfLog_Start once the scheduler is up. */
    PerfLog_Initialize();

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

    /* M2 actuator path: fretboard_link owns the submit queue + FLEXCOM1
     * USART writer and the RX parse task. The FLEXCOM1 peripheral is brought
     * up by SYS_Initialize (FLEXCOM1_USART_Initialize), so this just arms the
     * ring-buffer RX notification and starts the link tasks. */
    FretboardLink_Initialize();

    /* timing_pipeline runs the chord-window + strum scheduler against the
     * active detector and pushes the resulting 7-bit mask through
     * FretboardLink_Send. */
    TimingPipeline_Initialize();

    /* manual_control is a peer producer for direct UI-driven actuation
     * (game-menu navigation, manual test). UI buttons are authored in
     * Microchip Graphics Composer; the generated screenShow_Screen0
     * registers the event_Screen0_Button_Manual_* callbacks defined in
     * ui/manual_input.c, so no explicit bind step is needed here. */
    ManualControl_Initialize();

    /* Game-state observer (spec §4.8, M9): a video-frame consumer that
     * classifies the current GH3 screen and publishes game_state_t events on
     * xGameStateQueue. Like cv_marvin_v1 it subscribes to the video frame queue
     * from inside its task, so it follows Video_Initialize. Enable observation
     * explicitly (default off, per the §6 game_observe_enable toggle). */
    GameplayEngine_Initialize();
    GameplayEngine_SetObserveEnabled(true);

    /* SD-card storage: sets up mount state only (no I/O here — the SDMMC
     * driver hasn't analyzed the card pre-scheduler). The card is mounted on
     * demand by the `sd` console command; see storage/storage.h. */
    Storage_Initialize();

    /* Per-player results log (CSV on the card). State only here; file I/O is
     * lazy (mounts on demand) from Results_Append / the player/scores/results
     * console commands. */
    Results_Initialize();

    /* Song catalog (labels keyed by the recognizer's (setlist,index)). State
     * only here; the CSV is lazy-loaded from the card on first lookup / the
     * `catalog` console command. A missing catalog degrades to "Unknown song"
     * and never affects recognition. See game/catalog.h. */
    Catalog_Initialize();

    /* Interactive operator console on FLEXCOM2 (115200), separate from the
     * DBGU log channel. Started after the actuator/detector modules so its
     * commands can drive their setters. */
    Console_Initialize();

    /* Drain task is launched last so every producer's queue handle is
     * already valid when the first records hit the sink. Marvin creates
     * all tasks pre-scheduler (Harmony brings the scheduler up after
     * APP_Initialize returns); xTaskCreateStatic before vTaskStartScheduler
     * is the standard FreeRTOS pattern. */
    PerfLog_Start();
}


/******************************************************************************
  Function:
    void APP_Tasks ( void )

  Remarks:
    See prototype in app.h.
 */

void APP_Tasks ( void )
{
    /* All tasks have been created by now, so this is the heap_1 startup floor
     * (heap_1 never frees — free space only shrinks). Surfaces remaining
     * headroom so a future allocation walking into the wall is visible rather
     * than a silent malloc-failed spin. */
    LOG_INFO("freertos heap: %u bytes free\r\n", (unsigned)xPortGetFreeHeapSize());

    /* APP is a one-shot launcher — task creation happened in APP_Initialize.
     * Self-delete here so the (1024-word) stack and TCB are released to the
     * idle task. The MCC-generated lAPP_Tasks loop will not iterate again
     * because vTaskDelete(NULL) never returns to its caller. */
    vTaskDelete(NULL);
}


/*******************************************************************************
 End of File
 */
