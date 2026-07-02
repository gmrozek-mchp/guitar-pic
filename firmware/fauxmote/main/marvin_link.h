#pragma once

/* marvin command link: a second front-end (beside the CLI) that receives marvin's
 * controller input over a UART and drives the same Wiimote_/Guitar_/Fauxmote_ APIs,
 * and streams STATUS back. Wire protocol in mf_proto.h / docs/marvin-fauxmote-link.md.
 * Starts a FreeRTOS task; call once after Fauxmote_BtStart(). */
void MarvinLink_Start(void);
