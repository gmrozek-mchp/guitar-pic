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
#include "log_ring.h"
#include "video/video.h"
#include "detector/detector.h"
#include "game/game_timing.h"
#include "actuator/fretboard_link.h"
#include "actuator/manual_control.h"
#include "net/fauxmote/fauxmote_link.h"
#include "game/game_controller.h"
#include "ui/ui_manager.h"
#include "game/game_engine.h"
#include "console/console.h"
#include "health/health_monitor.h"
#include "health/nocache_guard.h"
#include "storage/storage.h"
#include "results/results.h"
#include "game/game_catalog.h"
#include "game/game_art.h"
#include "ui/node_art.h"
#include "ui/qr_art.h"
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

/* Fired by the UI boot task the instant the splash is on screen (registered via
 * UiManager_SetSplashShownCallback). Brings up the runtime services in parallel with
 * the behind-the-splash screen painting, so the system is warm by the time the
 * dashboard is revealed. Video capture is deliberately NOT armed here: the CSI-2
 * D-PHY must be brought up against settled display clocks and away from the boot-time
 * contention, or it locks marginally and the lanes sit in stop-state (~0 fps). The
 * compositor arms capture at the end of the boot sequence instead (see ui_manager). */
static void app_on_splash_shown(void)
{
    App_StartServices();
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

    /* Keep the last lines in RAM as well as sending them to DBGU, so the operator UI's
     * activity log can show them. Installed here rather than later so the boot lines are
     * captured too. */
    log_set_sink(log_ring_vwrite);

    /* UI manager: the UI orchestrator + compositor. Assigns the per-screen canvas
     * surfaces (pre-scheduler) and creates the boot task that runs the bring-up
     * sequence (splash → screens → reveal) once the scheduler is up. The callback
     * is registered first so the boot task can fire it the moment the splash is on
     * screen — that's our cue to start everything else (see app_on_splash_shown). */
    /* Lay the nocache guards before anything paints into the region, so a stray
     * write is reported as memory corruption rather than chased through whatever
     * it happens to break (see health/nocache_guard.h). */
    NocacheGuard_Initialize();

    UiManager_SetSplashShownCallback(app_on_splash_shown);
    UiManager_Initialize();

    /* Per-frame performance log: producer-side queues only. Init here so any
     * producer that posts has a valid queue; the drain task spawns later in
     * App_StartServices (it sits above the SD tasks in priority). */
    PerfLog_Initialize();

    /* SD-card storage: sets up mount state only (no I/O here — the SDMMC driver
     * hasn't analyzed the card pre-scheduler). The splash module mounts it. */
    Storage_Initialize();

    /* Per-player results log (CSV on the card). State only here; file I/O is
     * lazy (mounts on demand) from Results_Append / the player/scores/results
     * console commands. */
    Results_Initialize();

    /* Song catalog (labels keyed by the recognizer's (setlist,index)). State
     * only here; the CSV is lazy-loaded from the card on first lookup / the
     * `catalog` console command. A missing catalog degrades to "Unknown song"
     * and never affects recognition. See game/game_catalog.h. */
    GameCatalog_Initialize();

    /* Album-artwork cache (cover art keyed by (setlist,index)). State only here;
     * the covers are decoded from the card into static RGB888 caches by
     * GameArt_LoadAll(), called from the UI boot task during the splash. See
     * game/game_art.h. */
    GameArt_Initialize();

    /* Board photos for the system-info screen, keyed by node id. State only here;
     * decoded from the card by NodeArt_LoadAll() alongside the album art. See
     * ui/node_art.h. */
    NodeArt_Initialize();

    /* QR tiles for the system-info screen, keyed by URL. State only here; the tiles are
     * encoded from the NODE table's URLs by ScreenSystem_Setup — no card, no decoder,
     * nothing to load. See ui/qr_art.h. */
    QrArt_Initialize();

    /* The video pipeline, detector, actuator links, gameplay observer, console,
     * and perf-log drain are NOT started here — they spawn tasks at priorities
     * above the SDMMC/filesystem tasks, so starting them during boot would starve
     * the card mount and splash load. App_StartServices brings them up, fired from
     * app_on_splash_shown the instant the splash is displayed. */
}


/* Bring up the runtime subsystems deferred out of APP_Initialize. Called by the
 * loader once the splash is up and the dashboard revealed, so none of these
 * higher-priority tasks preempt the SDMMC/filesystem tasks during the card mount
 * + splash render. Order honors the dependencies: Video first (it owns the frame
 * queue the detector + gameplay observer subscribe to from their tasks). */
void App_StartServices(void)
{
    /* Video capture pipeline (ISC, TC358743, HEO unbind) + capture/display
     * state machine + bridge-status polling. Spawns VideoTask. */
    Video_Initialize();

    /* Reference detector (cv_marvin_v1) + detector-state bus. Must follow
     * Video_Initialize since cv_marvin_v1 subscribes to the video frame queue
     * from inside its task. Detectors default disabled; enable + select the
     * active one for the timing pipeline. */
    Detector_Initialize();
    Detector_Enable(DETECTOR_CV_MARVIN_V1);
    Detector_SetActive(DETECTOR_CV_MARVIN_V1);

    /* M2 actuator path: fretboard_link owns the submit queue + FLEXCOM1 USART
     * writer and the RX parse task (and brings up the T1S link). */
    FretboardLink_Initialize();

    /* fauxmote command link (ESP32 Wiimote emulator) on its own FLEXCOM5. Mirrors
     * every gameplay mask (FretboardLink_Send taps Fauxmote_SendGuitarMask). Must
     * precede the mask producers below. */
    Fauxmote_Initialize();

    /* timing_pipeline runs the chord-window + strum scheduler against the
     * active detector and pushes the resulting 7-bit mask through
     * FretboardLink_Send. */
    GameTiming_Initialize();

    /* manual_control is a peer producer for direct UI-driven actuation. UI
     * buttons are authored in Microchip Graphics Composer; the generated
     * screenShow registers the callbacks defined in ui/manual_input.c, so no
     * explicit bind step is needed here. */
    ManualControl_Initialize();

    /* Game-state observer (spec §4.8, M9): a video-frame consumer that classifies
     * the current GH3 screen. It subscribes to the video frame queue from its task
     * (so it follows Video_Initialize) but observation is synchronous and on-demand —
     * it does NOTHING until GameEngine_Observe() blocks for a fresh classification
     * (called by the game controller). There is no free-running scan: the song_select
     * match is ~tens of ms of soft-float on this FPU-less core, and running it unasked
     * pegged prio-4 and froze the UI. The dashboard uses the touch Selection; the bus
     * has no other consumer. */
    GameEngine_Initialize();

    /* M10 game-state controller: START (dashboard button / `play` console command)
     * navigates GH3 to the selected song+difficulty, then hands off to the CV
     * detector. Follows the gameplay engine + actuator producers it drives. */
    GameController_Initialize();

    /* Interactive operator console on FLEXCOM2 (115200), separate from the DBGU
     * log channel. Started after the actuator/detector modules so its commands
     * can drive their setters. */
    Console_Initialize();

    /* Per-frame perf-log drain task, launched last so every producer's queue
     * handle is already valid when the first records hit the sink. */
    PerfLog_Start();

    /* Liveness + resource monitor: a just-above-idle task that heartbeats to
     * SD + DBGU every minute and dumps the per-task stack table every 10 min,
     * so an unattended freeze can be located in time and inspected after the
     * fact. Started last — it only observes. See health/health_monitor.h. */
    HealthMonitor_Initialize();
}


/******************************************************************************
  Function:
    void APP_Tasks ( void )

  Remarks:
    See prototype in app.h.
 */

void APP_Tasks ( void )
{
    /* Heap_1 free space at the end of APP_Initialize (heap_1 never frees — free
     * space only shrinks). The runtime subsystems spawn later via
     * App_StartServices, so this is the pre-services floor, not the final one.
     * Surfaces remaining headroom so an allocation walking into the wall is
     * visible rather than a silent malloc-failed spin. */
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
