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

/* Point LCDC HEO at the capture framebuffer, centered within the 1280x800
 * panel. HEO (not BASE) because BASE has no window position/size registers —
 * always full-panel, so a 720-wide source on BASE gets read at panel-wide
 * stride and smears line-to-line.
 *
 * RGB_888_PACKED on SAM9X75 LCDC reads memory in B, G, R order per pixel
 * (3 B/pixel dense — Table 44.26), exactly matching the CSI2DC RMS=1 +
 * ISC PACKED32 byte stream that fills g_framebuffer.
 *
 * XLCDC_SetLayerWindowXYSize writes both HEOCFG3 (display size) and
 * HEOCFG4 (source memory size) to the same value, giving 1:1 with the
 * scaler at its default 0x100000 factor — no scaling needed for native
 * 720x480 → 720x480 display. */
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
                               XLCDC_RGB_COLOR_MODE_RGB_888_PACKED, false);
    XLCDC_SetLayerAddress(XLCDC_LAYER_HEO,
                          ISC_Capture_GetBufferAddress(), false);
    XLCDC_SetLayerXStride(XLCDC_LAYER_HEO, 0u, false);
    XLCDC_SetLayerWindowXYPos(XLCDC_LAYER_HEO, xpos, ypos, false);
    XLCDC_SetLayerWindowXYSize(XLCDC_LAYER_HEO, src_w, src_h, false);
    /* MCC's gfx-driver init left HEOCFG12 at SFACTC=4 (A0×As) / DFACTC=6
     * (1-A0×As). RGB_888_PACKED has no source alpha → A0×As=0 → HEO
     * invisible. Patch to SFACTC=2 / DFACTC=0 → out = src. Read-modify-
     * write preserves MCC's DMA, REP, VIDPRI, A0, A1, CRKEY, DSTKEY. */
    uint32_t heocfg12 = XLCDC_REGS->LCDC_HEOCFG12;
    heocfg12 &= ~(LCDC_HEOCFG12_SFACTC_Msk | LCDC_HEOCFG12_DFACTC_Msk);
    heocfg12 |= LCDC_HEOCFG12_SFACTC(2) | LCDC_HEOCFG12_DFACTC(0);
    XLCDC_REGS->LCDC_HEOCFG12 = heocfg12;
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
