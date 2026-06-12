#pragma once

/* Drive the Feather V2's onboard red LED (GPIO13) as a Wiimote-style status light:
 * slow heartbeat = waiting, fast blink = connected, solid = assigned a player slot. */
void StatusLed_Start(void);
