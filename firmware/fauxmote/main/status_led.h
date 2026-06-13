#pragma once

/* Drive the Feather V2's onboard red LED (GPIO13) as a status light:
 * idle = brief blip every ~3 s, pairing/connecting = fast blink, assigned = N
 * flashes (= player slot 1..4) then a pause. */
void StatusLed_Start(void);
