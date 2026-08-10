#ifndef MARVIN_FAUXMOTE_POINTER_H
#define MARVIN_FAUXMOTE_POINTER_H

#include <stdint.h>
#include <stdbool.h>

/* Touch on the live video -> fauxmote IR pointer, and the routine that calibrates
 * that map.
 *
 * The map needs calibrating because nothing in this repo knows the gain: fauxmote
 * synthesizes the two sensor-bar dots from fixed camera constants, and the Wii then
 * derives its cursor from them using its own sensor-bar-position, TV-size and
 * sensitivity settings. Commanded position -> on-screen cursor is therefore affine
 * with an unknown gain and offset, and either axis may come out inverted. Two
 * commanded points plus where the operator says the cursor landed pin all of that
 * down; the solved map lives in QSPI settings (flash/settings.h ptr_cal_t).
 *
 * Uncalibrated, the map is the identity — touching the video still points roughly
 * where the finger is, so calibration is a refinement rather than a prerequisite. */

/* Arm/disarm the whole facility. The wiimotes screen calls this on its gate edges:
 * pointing is real input to the Wii, so it sits behind the same slide-to-unlock as
 * the frets and nav buttons. Disarming hides the pointer and abandons a calibration
 * in progress. */
void FauxmotePointer_SetEnabled(bool on);
bool FauxmotePointer_IsEnabled(void);

/* A touch on the live video at (fx, fy), each 0..255 as a fraction of the displayed
 * picture (0,0 = top-left). `press` distinguishes the initial press from the drag's
 * move samples: calibration takes presses only, while pointing follows both. No-op
 * while disarmed.
 *
 * The pointer LATCHES: it stays where the last touch left it so the operator can
 * then press A on the wiimote card. Only disarming (or the console) hides it. */
void FauxmotePointer_Touch(uint8_t fx, uint8_t fy, bool press);

/* Console (`fauxmote calib …`). Prompts and results go to the LOG, not to the console
 * return path, because the interesting ones are emitted from the touch handler long
 * after the command that started the routine has returned.
 *
 * The state machine is advanced by TOUCHES, not by console input: start commands the
 * pointer to u_lo and waits, the operator's touch on the cursor captures that sample
 * and commands u_hi, and their second touch solves. The solved map goes live in RAM
 * immediately and the pointer moves to the centre of the video for a visual check;
 * CalSave then persists it, or another CalStart redoes it. */
#define FX_PTR_CAL_U_LO   64u    /* 25% of pointer space */
#define FX_PTR_CAL_U_HI   192u   /* 75%                  */

bool FauxmotePointer_CalStart(uint8_t u_lo, uint8_t u_hi);  /* 0,0 = the defaults */
bool FauxmotePointer_CalSave(void);
void FauxmotePointer_CalAbort(void);
void FauxmotePointer_CalShow(void);
bool FauxmotePointer_CalActive(void);

#endif /* MARVIN_FAUXMOTE_POINTER_H */
