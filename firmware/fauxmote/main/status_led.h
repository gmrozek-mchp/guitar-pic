#pragma once

/* Drive the Feather V2's onboard NeoPixel as a status light. One 2 s cycle shows
 * a red heartbeat (T1S bus state, lemmy's pattern) and then a blue report on the
 * Wii link (player count / connecting / idle) — never both colours at once. See
 * status_led.c for the timeline. */
void StatusLed_Start(void);
