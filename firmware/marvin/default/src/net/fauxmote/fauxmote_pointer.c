#include "net/fauxmote/fauxmote_pointer.h"

#include "net/fauxmote/fauxmote_link.h"
#include "net/fauxmote/mf_proto.h"
#include "flash/settings.h"
#include "log.h"

#include <stdio.h>

/* How long the tap's A press is held. Long enough for the Wii to see a press and a
 * release across several of its input reports, short enough to read as a click. The
 * link's floor wake bounds the release, so the effective hold is this plus up to one
 * refresh period. */
#define TAP_A_MS   120u

/* Gain is Q8: 256 is 1:1. The clamp is wide enough for any plausible Wii pointer
 * sensitivity in either direction, and narrow enough that a sample taken by touching
 * something other than the cursor cannot produce a map that pins the pointer to an
 * edge for the whole of its travel. */
#define GAIN_ONE      256
#define GAIN_MAX     1024
#define GAIN_MIN       64

/* Minimum separation between the two calibration touches, in fraction units. Below
 * this the divide amplifies a couple of pixels of touch slop into a wild gain. */
#define CAL_MIN_SPAN   16

typedef enum
{
    CAL_IDLE = 0,
    CAL_WAIT_LO,      /* pointer commanded to u_lo, waiting for the operator's touch */
    CAL_WAIT_HI,
    CAL_VERIFY        /* solved and live in RAM, waiting for save or a redo */
} cal_state_t;

static bool        s_enabled;
static ptr_cal_t   s_cal;              /* live map; loaded from settings on first use */
static bool        s_cal_loaded;

static cal_state_t s_state;
static uint8_t     s_u_lo, s_u_hi;     /* the commanded pointer values this run uses */
static uint8_t     s_f_lo_x, s_f_lo_y; /* where the operator said the cursor was      */
static ptr_cal_t   s_saved;            /* the map to restore if the run is abandoned  */

/* Last values handed to the link, so a drag that maps to the same byte pair does not
 * re-arm the tx task on every touch-move sample. */
static bool        s_latched;
static uint8_t     s_last_x, s_last_y;

static void cal_load(void)
{
    if (s_cal_loaded) { return; }

    s_cal        = Settings_Get()->ptr_cal;
    s_cal_loaded = true;
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

/* One axis of the map. A zero gain is the identity, which is what an uncalibrated
 * record (all-zero, see settings.c set_defaults) yields — so `valid` is only ever
 * reported, never consulted here. */
static uint8_t map_axis(uint8_t f, int16_t gain, int16_t off)
{
    if (gain == 0) { return f; }

    return (uint8_t)clampi((int32_t)off + (((int32_t)gain * (int32_t)f) >> 8), 0, 255);
}

/* Command the pointer, skipping the link call when nothing moved. */
static void point_at(uint8_t x, uint8_t y)
{
    if (s_latched && x == s_last_x && y == s_last_y) { return; }

    Fauxmote_SendPointer(x, y, true);
    s_latched = true;
    s_last_x  = x;
    s_last_y  = y;
}

static void point_hide(void)
{
    if (!s_latched) { return; }

    Fauxmote_SendPointer(0u, 0u, false);
    s_latched = false;
}

static void cal_end(void)
{
    s_state = CAL_IDLE;
}

/* Solve one axis from the two commanded values and the two touched fractions.
 * A negative gain is a valid answer: it means the axis reads inverted. */
static bool solve_axis(uint8_t f_lo, uint8_t f_hi, int16_t *gain, int16_t *off)
{
    int32_t span = (int32_t)f_hi - (int32_t)f_lo;
    int32_t g;

    if (span > -CAL_MIN_SPAN && span < CAL_MIN_SPAN) { return false; }

    g = (((int32_t)s_u_hi - (int32_t)s_u_lo) << 8) / span;
    g = (g < 0) ? clampi(g, -GAIN_MAX, -GAIN_MIN) : clampi(g, GAIN_MIN, GAIN_MAX);

    *gain = (int16_t)g;
    *off  = (int16_t)((int32_t)s_u_lo - ((g * (int32_t)f_lo) >> 8));
    return true;
}

/* Q8 gain as a signed decimal, so the log is readable without doing Q8 in your head.
 * The sign is printed separately because a gain of -0.39 truncates to an integer part
 * of 0 and would otherwise read as positive. Two buffers so both axes can appear in
 * one log line. */
static const char *gain_str(int16_t gain, unsigned which)
{
    static char buf[2][12];
    int32_t     mag = (gain < 0) ? -(int32_t)gain : (int32_t)gain;

    (void)snprintf(buf[which & 1u], sizeof buf[0], "%s%ld.%02ld",
                   (gain < 0) ? "-" : "",
                   (long)(mag / GAIN_ONE), (long)((mag % GAIN_ONE) * 100 / GAIN_ONE));
    return buf[which & 1u];
}

static void log_cal(const char *what, const ptr_cal_t *c)
{
    if (!c->valid)
    {
        LOG_INFO("FXP: %s uncalibrated (identity map)\r\n", what);
        return;
    }
    LOG_INFO("FXP: %s x gain %s off %d, y gain %s off %d\r\n", what,
             gain_str(c->x_gain, 0u), (int)c->x_off,
             gain_str(c->y_gain, 1u), (int)c->y_off);
}

void FauxmotePointer_SetEnabled(bool on)
{
    if (on == s_enabled) { return; }

    s_enabled = on;

    if (on)
    {
        /* Here rather than on first touch: Settings_Get can scan QSPI, and the arm edge
         * is a better place for that than the middle of a drag. */
        cal_load();
    }
    else
    {
        if (s_state != CAL_IDLE)
        {
            LOG_WARN("FXP: calibration abandoned (pointer disarmed)\r\n");
            s_cal = s_saved;
            cal_end();
        }
        point_hide();
        /* A tap's A press must not outlive the screen — otherwise letting go and
         * navigating away inside the pulse confirms whatever the cursor was over. */
        Fauxmote_CancelNavPulse();
    }
}

bool FauxmotePointer_IsEnabled(void)
{
    return s_enabled;
}

/* Take one calibration point. First press records where the cursor was and commands
 * the second position; second press solves. */
static void cal_sample(uint8_t fx, uint8_t fy)
{
    if (s_state == CAL_WAIT_LO)
    {
        s_f_lo_x = fx;
        s_f_lo_y = fy;
        s_state  = CAL_WAIT_HI;
        LOG_INFO("FXP: calib 1/2 captured at %u,%u\r\n", (unsigned)fx, (unsigned)fy);
        /* Raw, not through the map: it is not solved yet. */
        point_at(s_u_hi, s_u_hi);
        LOG_INFO("FXP: calib 2/2 - pointer commanded to %u,%u; touch the Wii cursor\r\n",
                 (unsigned)s_u_hi, (unsigned)s_u_hi);
        return;
    }

    ptr_cal_t solved = { 0, 0, 0, 0, 0u };

    if (!solve_axis(s_f_lo_x, fx, &solved.x_gain, &solved.x_off) ||
        !solve_axis(s_f_lo_y, fy, &solved.y_gain, &solved.y_off))
    {
        LOG_WARN("FXP: calib FAILED - the two touches are too close "
                 "(%u,%u then %u,%u); saved map kept, run `fauxmote calib start` again\r\n",
                 (unsigned)s_f_lo_x, (unsigned)s_f_lo_y, (unsigned)fx, (unsigned)fy);
        s_cal = s_saved;
        cal_end();
        point_hide();
        return;
    }

    solved.valid = 1u;
    s_cal        = solved;
    s_state      = CAL_VERIFY;

    LOG_INFO("FXP: calib 2/2 captured at %u,%u\r\n", (unsigned)fx, (unsigned)fy);
    log_cal("solved", &s_cal);

    /* Through the new map, so a cursor landing at the centre of the picture is itself
     * the verification. */
    point_at(map_axis(128u, s_cal.x_gain, s_cal.x_off),
             map_axis(128u, s_cal.y_gain, s_cal.y_off));
    LOG_INFO("FXP: verify - the cursor should be at the centre of the video. "
             "`fauxmote calib save` to keep it, `fauxmote calib start` to redo\r\n");
}

void FauxmotePointer_Touch(uint8_t fx, uint8_t fy, bool press)
{
    if (!s_enabled) { return; }

    cal_load();

    if (s_state == CAL_WAIT_LO || s_state == CAL_WAIT_HI)
    {
        /* Presses only, and nothing else at all. A finger always emits move samples,
         * and treating one as the second point would solve from two nearly identical
         * touches; letting one *point* would be worse still, since it would move the
         * very cursor the operator is being asked to touch. */
        if (press) { cal_sample(fx, fy); }
        return;
    }

    point_at(map_axis(fx, s_cal.x_gain, s_cal.x_off),
             map_axis(fy, s_cal.y_gain, s_cal.y_off));
}

void FauxmotePointer_Release(void)
{
    if (!s_enabled) { return; }

    /* A calibration touch is a measurement of where the cursor already is — clicking
     * would activate whatever it happens to be over, mid-routine. */
    if (s_state != CAL_IDLE) { return; }

    /* Nothing aimed yet (a release with no pointer behind it) has nothing to click. */
    if (!s_latched) { return; }

    Fauxmote_PulseNav(MF_W_A, (uint16_t)TAP_A_MS);
}

bool FauxmotePointer_CalStart(uint8_t u_lo, uint8_t u_hi)
{
    if (!s_enabled)
    {
        LOG_WARN("FXP: calib needs the pointer armed - nav to Wiimotes and slide to unlock\r\n");
        return false;
    }

    cal_load();

    if (u_lo == 0u && u_hi == 0u)
    {
        u_lo = (uint8_t)FX_PTR_CAL_U_LO;
        u_hi = (uint8_t)FX_PTR_CAL_U_HI;
    }
    if (u_lo == u_hi)
    {
        LOG_WARN("FXP: calib needs two different commanded values\r\n");
        return false;
    }

    /* Keep the map that is in force, so an abandoned or failed run restores it rather
     * than leaving the operator with a half-solved one. */
    if (s_state == CAL_IDLE) { s_saved = s_cal; }

    s_u_lo  = u_lo;
    s_u_hi  = u_hi;
    s_state = CAL_WAIT_LO;

    point_at(s_u_lo, s_u_lo);
    LOG_INFO("FXP: calib 1/2 - pointer commanded to %u,%u; touch the Wii cursor on the video\r\n",
             (unsigned)s_u_lo, (unsigned)s_u_lo);
    return true;
}

bool FauxmotePointer_CalSave(void)
{
    if (s_state != CAL_VERIFY)
    {
        LOG_WARN("FXP: nothing to save - run `fauxmote calib start` and touch both points\r\n");
        return false;
    }

    if (!Settings_SetPointerCal(&s_cal))
    {
        LOG_ERROR("FXP: calib save FAILED (settings write); map stays live in RAM only\r\n");
        return false;
    }

    s_saved = s_cal;
    cal_end();
    log_cal("saved", &s_cal);
    return true;
}

void FauxmotePointer_CalAbort(void)
{
    if (s_state == CAL_IDLE)
    {
        LOG_INFO("FXP: no calibration in progress\r\n");
        return;
    }

    s_cal = s_saved;
    cal_end();
    point_hide();
    log_cal("calib abandoned, back to", &s_cal);
}

void FauxmotePointer_CalShow(void)
{
    cal_load();

    LOG_INFO("FXP: pointer %s, calib %s\r\n",
             s_enabled ? "armed" : "disarmed",
             (s_state == CAL_IDLE) ? "idle"
                                   : ((s_state == CAL_VERIFY) ? "solved, unsaved" : "in progress"));
    log_cal("map", &s_cal);
}

bool FauxmotePointer_CalActive(void)
{
    return s_state != CAL_IDLE;
}
