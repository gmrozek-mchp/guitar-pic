#ifndef FRETBOARD_LINK_H
#define FRETBOARD_LINK_H

#include <stdbool.h>

/* Fretboard link — USB CDC host writer that ferries the timing pipeline's
 * 7-bit GPIO bitmask to the fretboard MCU as a stream of single-byte
 * messages. Spec §4.4 calls this the "actuator transport"; for now it's
 * the only one. Initialize after TimingPipeline_Initialize so the cmd
 * queue exists by the time the writer task subscribes. Also enables the
 * USB host bus, so call after SYS_Initialize. */

void FretboardLink_Initialize(void);
bool FretboardLink_IsConnected(void);

#endif
