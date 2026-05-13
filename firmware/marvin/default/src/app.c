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

/* Point LCDC HEO layer at the capture framebuffer, pillarboxed/letterboxed
 * within the 1280x800 panel. Using HEO (not BASE/OVR1/OVR2) because:
 *  - MCC's display config now leaves only BASE and HEO enabled.
 *  - BASE has no window position/size registers (always full panel).
 *  - HEO is the only remaining overlay; also has the scaler + CSC engine
 *    available for future range-expansion / downscale work.
 *
 * ARGB_8888 on SAM9X75 LCDC reads memory as {B, G, R, A} low-to-high,
 * matching our BGRX32 byte-for-byte. X=0 lands as A=0; the alpha-blend
 * config below uses SFACTC=A0/255 and DFACTC=ZERO so per-pixel A is
 * ignored entirely and HEO is fully opaque.
 *
 * Scaler stays disabled (HEOCFG23 = 0 from MCC's setup) so HEO renders
 * 1:1 at src_w x src_h. HEOCFG3 (window) and HEOCFG4 (memory) are set
 * equal — required when scaler is bypassed. */
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

    XLCDC_SetLayerEnable(XLCDC_LAYER_HEO, false, true);
    XLCDC_SetLayerRGBColorMode(XLCDC_LAYER_HEO,
                               XLCDC_RGB_COLOR_MODE_ARGB_8888, false);
    XLCDC_SetLayerAddress(XLCDC_LAYER_HEO,
                          ISC_Capture_GetBufferAddress(), false);
    XLCDC_SetLayerXStride(XLCDC_LAYER_HEO, 0u, false);
    XLCDC_SetLayerWindowXYPos(XLCDC_LAYER_HEO, xpos, ypos, false);
    XLCDC_SetLayerWindowXYSize(XLCDC_LAYER_HEO, src_w, src_h, false);

    /* HEOCFG4 = source memory size, must equal HEOCFG3 (window size) when
     * the scaler is bypassed. No plib helper for this register. */
    XLCDC_REGS->LCDC_HEOCFG4 = LCDC_HEOCFG4_XMEMSIZE(src_w - 1u)
                             | LCDC_HEOCFG4_YMEMSIZE(src_h - 1u);

    /* Alpha-blend identical to the OVR1 fix in 96cdb46: bypass
     * XLCDC_SetLayerOpts() because its HEOCFG12 write uses SFACTC=A0*As,
     * which zeroes the source contribution when As=0 (our X byte). Use
     * SFACTC=2 (A0/255 = 1.0 with A0=255) and DFACTC=0 (ZERO) for a pure
     * overlay independent of per-pixel alpha. */
    XLCDC_REGS->LCDC_HEOCFG12 = LCDC_HEOCFG12_DMA(1)
                              | LCDC_HEOCFG12_REP(1)
                              | LCDC_HEOCFG12_CRKEY(0)
                              | LCDC_HEOCFG12_DSTKEY(0)
                              | LCDC_HEOCFG12_VIDPRI(1)   /* elevated bus prio */
                              | LCDC_HEOCFG12_SFACTC(2)   /* A0/255 = 1.0 */
                              | LCDC_HEOCFG12_SFACTA(0)   /* 0.0 */
                              | LCDC_HEOCFG12_DFACTC(0)   /* 0.0 */
                              | LCDC_HEOCFG12_DFACTA(0)   /* 0.0 */
                              | LCDC_HEOCFG12_A0(255)
                              | LCDC_HEOCFG12_A1(0);

    XLCDC_SetLayerEnable(XLCDC_LAYER_HEO, true, true);

    printf("LCD: HEO bound to capture buf @0x%08lX, "
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
