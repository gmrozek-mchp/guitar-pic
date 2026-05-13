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

/* TAP coefficient encoding: 13-bit signed Q2.10 fixed point per typical
 * Microchip XLCDC. 1.0 = 0x400. If output looks dim, format may be Q1.11
 * (1.0 = 0x800) or Q3.9 (1.0 = 0x200) — try those if 0x400 doesn't work. */
#define LCD_TAP_ONE  0x400u

/* Aspect-preserving fit. Returns the largest sub-rectangle of the panel that
 * matches the source aspect, plus its top-left corner for centering.
 * "fit width": panel is taller than source aspect → letterbox top/bottom.
 * "fit height": panel is wider than source aspect → pillarbox left/right. */
static void lcd_compute_fit(uint32_t src_w, uint32_t src_h,
                            uint32_t *out_w, uint32_t *out_h,
                            uint32_t *out_x, uint32_t *out_y)
{
    uint32_t pw = LCD_PANEL_W;
    uint32_t ph = LCD_PANEL_H;

    /* If panel_w * src_h <= panel_h * src_w, source aspect is wider, fit width. */
    if (pw * src_h <= ph * src_w)
    {
        *out_w = pw;
        *out_h = (pw * src_h) / src_w;
    }
    else
    {
        *out_h = ph;
        *out_w = (ph * src_w) / src_h;
    }
    *out_x = (pw - *out_w) / 2u;
    *out_y = (ph - *out_h) / 2u;
}

/* Program HEO scaler taps for nearest-neighbor: TAP1 = 1.0, others 0. */
static void lcd_program_scaler_taps_nearest(void)
{
    for (uint32_t i = 0; i < 16u; i++)
    {
        XLCDC_REGS->LCDC_HEOVTAP[i].LCDC_HEOVTAP10P =
            LCDC_HEOVTAP10P_TAP0(0) | LCDC_HEOVTAP10P_TAP1(LCD_TAP_ONE);
        XLCDC_REGS->LCDC_HEOVTAP[i].LCDC_HEOVTAP32P =
            LCDC_HEOVTAP32P_TAP2(0) | LCDC_HEOVTAP32P_TAP3(0);
        XLCDC_REGS->LCDC_HEOHTAP[i].LCDC_HEOHTAP10P =
            LCDC_HEOHTAP10P_TAP0(0) | LCDC_HEOHTAP10P_TAP1(LCD_TAP_ONE);
        XLCDC_REGS->LCDC_HEOHTAP[i].LCDC_HEOHTAP32P =
            LCDC_HEOHTAP32P_TAP2(0) | LCDC_HEOHTAP32P_TAP3(0);
    }
}

/* Point LCDC HEO layer at the capture framebuffer, scaling with aspect
 * preservation to fill the largest pillarboxed/letterboxed area within the
 * panel. ARGB_8888 reads memory as {B, G, R, A} low-to-high, matching our
 * BGRX32 byte-for-byte. Per-pixel A is ignored via SFACTC=A0/255, DFACTC=0
 * blend (so X=0 in the framebuffer doesn't kill the layer).
 *
 * Scaler config follows datasheet Table 44.59 (Progressive ARGB):
 *   HEOCFG23: enable all four scaler bits (VXSY/VXSC/HXSY/HXSC)
 *   HEOCFG24/25: VFACTOR for vertical Y/alpha and chroma channels
 *   HEOCFG26/27: HFACTOR for horizontal Y/alpha and chroma channels
 *     (chroma factors set equal to luma since this is RGB, not YCbCr)
 *   HEOCFG28/29: phase offsets all zero
 *   HEOCFG30/31: VXSYCFG/HXSYCFG = 1 (default polyphase mode), TAP2=0,
 *                BICU=0 — Table 44.59 ARGB row.
 *   HEOVTAP/HEOHTAP: nearest-neighbor coefficients (TAP1=1.0). Hard-edged
 *                    but unambiguous; can refine to programmed bilinear
 *                    later once the encoding is verified on hardware. */
static void lcd_bind_capture(uint32_t src_w, uint32_t src_h)
{
    if (src_w > LCD_PANEL_W || src_h > LCD_PANEL_H)
    {
        printf("LCD: source %lux%lu exceeds panel %ux%u; skipping bind\r\n",
               (unsigned long)src_w, (unsigned long)src_h,
               LCD_PANEL_W, LCD_PANEL_H);
        return;
    }

    uint32_t win_w, win_h, xpos, ypos;
    lcd_compute_fit(src_w, src_h, &win_w, &win_h, &xpos, &ypos);

    bool needs_scaler = (win_w != src_w) || (win_h != src_h);

    XLCDC_SetLayerEnable(XLCDC_LAYER_HEO, false, true);
    XLCDC_SetLayerRGBColorMode(XLCDC_LAYER_HEO,
                               XLCDC_RGB_COLOR_MODE_ARGB_8888, false);
    XLCDC_SetLayerAddress(XLCDC_LAYER_HEO,
                          ISC_Capture_GetBufferAddress(), false);
    XLCDC_SetLayerXStride(XLCDC_LAYER_HEO, 0u, false);
    XLCDC_SetLayerWindowXYPos(XLCDC_LAYER_HEO, xpos, ypos, false);
    XLCDC_SetLayerWindowXYSize(XLCDC_LAYER_HEO, win_w, win_h, false);

    /* HEOCFG4 = source memory size. With the scaler engaged this differs
     * from HEOCFG3 (display window). With it bypassed, the two are equal. */
    XLCDC_REGS->LCDC_HEOCFG4 = LCDC_HEOCFG4_XMEMSIZE(src_w - 1u)
                             | LCDC_HEOCFG4_YMEMSIZE(src_h - 1u);

    if (needs_scaler)
    {
        /* HFACTOR/VFACTOR per datasheet 44.6.13.2:
         *   FACTOR = round( 2^20 × (memsize) / (winsize) )
         * Operate in mem/win-1 form (XMEMSIZE/XSIZE conventions). */
        uint32_t hfactor = (uint32_t)(((uint64_t)(src_w - 1u) * (1u << 20)
                                       + ((win_w - 1u) >> 1)) / (win_w - 1u));
        uint32_t vfactor = (uint32_t)(((uint64_t)(src_h - 1u) * (1u << 20)
                                       + ((win_h - 1u) >> 1)) / (win_h - 1u));

        XLCDC_REGS->LCDC_HEOCFG24 = LCDC_HEOCFG24_VXSYFACT(vfactor);
        XLCDC_REGS->LCDC_HEOCFG25 = LCDC_HEOCFG25_VXSCFACT(vfactor);
        XLCDC_REGS->LCDC_HEOCFG26 = LCDC_HEOCFG26_HXSYFACT(hfactor);
        XLCDC_REGS->LCDC_HEOCFG27 = LCDC_HEOCFG27_HXSCFACT(hfactor);

        XLCDC_REGS->LCDC_HEOCFG28 = LCDC_HEOCFG28_VXSYOFF(0)
                                  | LCDC_HEOCFG28_VXSYOFF1(0)
                                  | LCDC_HEOCFG28_VXSCOFF(0)
                                  | LCDC_HEOCFG28_VXSCOFF1(0);
        XLCDC_REGS->LCDC_HEOCFG29 = LCDC_HEOCFG29_HXSYOFF(0)
                                  | LCDC_HEOCFG29_HXSCOFF(0);

        /* HEOCFG30/31 keep MCC defaults (VXSYCFG=1, TAP2=0, BICU=0) which
         * matches Table 44.59. Set them explicitly anyway in case anything
         * changed underneath us. */
        XLCDC_REGS->LCDC_HEOCFG30 = LCDC_HEOCFG30_VXSYCFG(1)
                                  | LCDC_HEOCFG30_VXSCCFG(1);
        XLCDC_REGS->LCDC_HEOCFG31 = LCDC_HEOCFG31_HXSYCFG(1)
                                  | LCDC_HEOCFG31_HXSCCFG(1);

        lcd_program_scaler_taps_nearest();

        XLCDC_REGS->LCDC_HEOCFG23 = LCDC_HEOCFG23_VXSYEN(1)
                                  | LCDC_HEOCFG23_VXSCEN(1)
                                  | LCDC_HEOCFG23_HXSYEN(1)
                                  | LCDC_HEOCFG23_HXSCEN(1);
    }
    else
    {
        XLCDC_REGS->LCDC_HEOCFG23 = 0u;  /* scaler bypassed, 1:1 path */
    }

    /* Alpha-blend: bypass XLCDC_SetLayerOpts() because its HEOCFG12 write
     * uses SFACTC=A0*As, which zeroes the source when As=0 (our X byte).
     * SFACTC=2 (A0/255=1.0) + DFACTC=0 = pure overlay independent of A. */
    XLCDC_REGS->LCDC_HEOCFG12 = LCDC_HEOCFG12_DMA(1)
                              | LCDC_HEOCFG12_REP(1)
                              | LCDC_HEOCFG12_CRKEY(0)
                              | LCDC_HEOCFG12_DSTKEY(0)
                              | LCDC_HEOCFG12_VIDPRI(1)
                              | LCDC_HEOCFG12_SFACTC(2)
                              | LCDC_HEOCFG12_SFACTA(0)
                              | LCDC_HEOCFG12_DFACTC(0)
                              | LCDC_HEOCFG12_DFACTA(0)
                              | LCDC_HEOCFG12_A0(255)
                              | LCDC_HEOCFG12_A1(0);

    XLCDC_SetLayerEnable(XLCDC_LAYER_HEO, true, true);

    printf("LCD: HEO src=%lux%lu -> win=%lux%lu @ (%lu,%lu) scaler=%s\r\n",
           (unsigned long)src_w, (unsigned long)src_h,
           (unsigned long)win_w, (unsigned long)win_h,
           (unsigned long)xpos, (unsigned long)ypos,
           needs_scaler ? "on" : "off");
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
