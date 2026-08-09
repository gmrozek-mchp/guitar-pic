#include "actuator_enable.h"

#include <stdbool.h>
#include <stddef.h>   /* NULL */

#include "log.h"

/* Wanted state, indexed by t1s_actuator_t. Written from the UI/console tasks and
 * read by the dashboard's feed task; a plain bool per actuator is a single aligned
 * access either way, so no lock. */
static bool s_on[T1S_ACT_COUNT];

static const char *const s_name[T1S_ACT_COUNT] =
{
    [T1S_ACT_GUITAR]    = "guitar",
    [T1S_ACT_LEMMY]     = "lemmy",
    [T1S_ACT_LIGHTSHOW] = "lightshow",
};

void ActuatorEnable_Initialize(void)
{
    for (uint8_t a = 0u; a < (uint8_t)T1S_ACT_COUNT; a++)
    {
        s_on[a] = true;
        (void)T1SLink_SendActuatorCtrl((t1s_actuator_t)a, true);
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
    (void)T1SLink_SendActuatorCtrl(act, on);
    LOG_INFO("ACT: %s output %s\r\n", s_name[act], on ? "on" : "off");
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
    return present && (out_on != ActuatorEnable_Get(act));
}

const char *ActuatorEnable_Name(t1s_actuator_t act)
{
    return (act < T1S_ACT_COUNT) ? s_name[act] : "?";
}
