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

/* 10.1" LVDS panel is 1280x800. MCC auto-initializes XLCDC + LVDSC + GFX2D
 * in SYS_Initialize; the timing engine is already running by the time we get
 * here. Auto-init leaves backlight off. */
#define LCD_PANEL_W  1280u
#define LCD_PANEL_H  800u

void APP_Initialize ( void )
{
    /* Place the App state machine in its initial state. */
    appData.state = APP_STATE_INIT;

    ISC_Capture_Initialize();
    TC358743_Initialize();

    XLCDC_EnableBacklight(); 
}


/******************************************************************************
  Function:
    void APP_Tasks ( void )

  Remarks:
    See prototype in app.h.
 */

/* Point LCDC overlay 1 at the capture framebuffer, pillarboxed within the
 * 800x480 panel. OVR1 (not BASE) because BASE has no window position/size
 * registers — it's always full-panel, so a 720-wide source on BASE gets
 * read at panel-wide stride and smears line-to-line.
 *
 * ARGB_8888 on SAM9X75 LCDC reads memory as {B, G, R, A} low-to-high,
 * matching our BGRX32 byte-for-byte. X lands as A=0 (transparent) so we
 * set OVR1 to use global alpha = 255 (fully opaque) via SetLayerOpts. */
static void lcd_bind_capture(uint32_t src_w, uint32_t src_h)
{
    if (src_w > LCD_PANEL_W || src_h > LCD_PANEL_H)
    {
        printf("LCD: source %lux%lu exceeds panel %ux%u; skipping bind\r\n",
               (unsigned long)src_w, (unsigned long)src_h,
               LCD_PANEL_W, LCD_PANEL_H);
        return;
    }

    uint32_t xpos = (LCD_PANEL_W - src_w) / 2u;
    uint32_t ypos = (LCD_PANEL_H - src_h) / 2u;

    /* Disable HEO and OVR2 — they sit ABOVE OVR1 in z-order (BASE -> OVR1
     * -> HEO -> OVR2) and MCC enables all four by default with the new
     * SFACTC=A0*As blend. RGB565 (no per-pixel alpha) forces As=1.0, so
     * those layers paint as fully opaque black over the entire panel and
     * hide OVR1. BASE stays enabled (below OVR1, harmless). */
    XLCDC_SetLayerEnable(XLCDC_LAYER_HEO,  false, true);
    XLCDC_SetLayerEnable(XLCDC_LAYER_OVR2, false, true);

    XLCDC_SetLayerEnable(XLCDC_LAYER_OVR1, false, true);
    XLCDC_SetLayerRGBColorMode(XLCDC_LAYER_OVR1,
                               XLCDC_RGB_COLOR_MODE_ARGB_8888, false);
    XLCDC_SetLayerAddress(XLCDC_LAYER_OVR1,
                          ISC_Capture_GetBufferAddress(), false);
    XLCDC_SetLayerXStride(XLCDC_LAYER_OVR1, 0u, false);
    XLCDC_SetLayerWindowXYPos(XLCDC_LAYER_OVR1, xpos, ypos, false);
    XLCDC_SetLayerWindowXYSize(XLCDC_LAYER_OVR1, src_w, src_h, false);

    /* Explicit alpha-blend config that ignores per-pixel source alpha.
     * Cannot use XLCDC_SetLayerOpts() — its OVR1CFG9 write uses
     * SFACTC=A0*As and DFACTC=1-(A0*As). With our BGRX32 data, As=0x00,
     * so source contribution = 0 and the layer is invisible (regression
     * from MCC regen; previous SFACTC was 5 = 1-(A0*Ad)).
     *
     * Required blend: out_color = src * 1 + dst * 0 (pure overlay).
     * SFACTC=2 (A0/255) with A0=255 gives factor 1.0; DFACTC=0 (ZERO).
     * Alpha channel doesn't matter for output but set sane values too. */
    XLCDC_REGS->LCDC_OVR1CFG9 = LCDC_OVR1CFG9_DMA(1)
                              | LCDC_OVR1CFG9_REP(1)
                              | LCDC_OVR1CFG9_CRKEY(0)
                              | LCDC_OVR1CFG9_DSTKEY(0)
                              | LCDC_OVR1CFG9_SFACTC(2)   /* A0/255 = 1.0 */
                              | LCDC_OVR1CFG9_SFACTA(0)   /* 0.0 */
                              | LCDC_OVR1CFG9_DFACTC(0)   /* 0.0 */
                              | LCDC_OVR1CFG9_DFACTA(0)   /* 0.0 */
                              | LCDC_OVR1CFG9_A0(255)
                              | LCDC_OVR1CFG9_A1(0);

    XLCDC_SetLayerEnable(XLCDC_LAYER_OVR1, true, true);

    printf("LCD: OVR1 bound to capture buf @0x%08lX, "
           "%lux%lu at (%lu,%lu)\r\n",
           (unsigned long)ISC_Capture_GetBufferAddress(),
           (unsigned long)src_w, (unsigned long)src_h,
           (unsigned long)xpos, (unsigned long)ypos);
}

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
                lcd_bind_capture(w, h);
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

void APP_Tasks ( void )
{
    TC358743_Tasks();
    app_coordinate_capture();

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
