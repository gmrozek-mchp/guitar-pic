#include "actuator_enable.h"

#include <stdbool.h>
#include <stddef.h>   /* NULL */

#include "log.h"

/* Master state, indexed by t1s_actuator_t. Written from the UI/console tasks and
 * read by the dashboard's feed task; a plain bool per actuator is a single aligned
 * access either way, so no lock. */
static bool s_on[T1S_ACT_COUNT];

/* Performance window (game controller task). Same single-bool contract. */
static bool s_playing;

/* Which actuators the window gates. The guitar is exempt — it actuates the menus
 * that get marvin into a song, so gating it would strand the run. */
static const bool s_window_gated[T1S_ACT_COUNT] =
{
    [T1S_ACT_GUITAR]    = false,
    [T1S_ACT_LEMMY]     = true,
    [T1S_ACT_LIGHTSHOW] = true,
};

static const char *const s_name[T1S_ACT_COUNT] =
{
    [T1S_ACT_GUITAR]    = "guitar",
    [T1S_ACT_LEMMY]     = "lemmy",
    [T1S_ACT_LIGHTSHOW] = "lightshow",
};

static bool effective(t1s_actuator_t act)
{
    return s_on[act] && (s_playing || !s_window_gated[act]);
}

void ActuatorEnable_Initialize(void)
{
    s_playing = false;
    for (uint8_t a = 0u; a < (uint8_t)T1S_ACT_COUNT; a++)
    {
        s_on[a] = true;
        (void)T1SLink_SendActuatorCtrl((t1s_actuator_t)a, effective((t1s_actuator_t)a));
    }
}

bool ActuatorEnable_Get(t1s_actuator_t act)
{
    return (act < T1S_ACT_COUNT) ? s_on[act] : false;
}

void ActuatorEnable_Set(t1s_actuator_t act, bool on)
{
    if (act >= T1S_ACT_COUNT) { return; }

    s_on[act] = on;
    (void)T1SLink_SendActuatorCtrl(act, effective(act));
    LOG_INFO("ACT: %s output %s%s\r\n", s_name[act], on ? "on" : "off",
             (on && !effective(act)) ? " (idle - no song playing)" : "");
}

void ActuatorEnable_SetPlaying(bool playing)
{
    if (s_playing == playing) { return; }
    s_playing = playing;

    for (uint8_t a = 0u; a < (uint8_t)T1S_ACT_COUNT; a++)
    {
        if (!s_window_gated[a]) { continue; }
        (void)T1SLink_SendActuatorCtrl((t1s_actuator_t)a, effective((t1s_actuator_t)a));
    }
    LOG_INFO("ACT: performance window %s\r\n", playing ? "open" : "closed");
}

bool ActuatorEnable_IsPlaying(void) { return s_playing; }

bool ActuatorEnable_WindowGated(t1s_actuator_t act)
{
    return (act < T1S_ACT_COUNT) ? s_window_gated[act] : false;
}

bool ActuatorEnable_Effective(t1s_actuator_t act)
{
    return (act < T1S_ACT_COUNT) ? effective(act) : false;
}

bool ActuatorEnable_Reported(t1s_actuator_t act)
{
    bool out_on = false;
    (void)T1SLink_GetActuatorState(act, NULL, &out_on);
    return out_on;
}

bool ActuatorEnable_Present(t1s_actuator_t act)
{
    bool present = false;
    (void)T1SLink_GetActuatorState(act, &present, NULL);
    return present;
}

bool ActuatorEnable_Pending(t1s_actuator_t act)
{
    bool present = false;
    bool out_on  = false;

    if (!T1SLink_GetActuatorState(act, &present, &out_on)) { return false; }
    return present && (out_on != effective(act));
}

const char *ActuatorEnable_Name(t1s_actuator_t act)
{
    return (act < T1S_ACT_COUNT) ? s_name[act] : "?";
}
