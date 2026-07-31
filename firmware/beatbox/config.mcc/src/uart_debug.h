#ifndef UART_DEBUG_H
#define UART_DEBUG_H

#include <stdint.h>
#include <stdbool.h>

#include "beat_engine.h"

/* GUI telemetry on UART1 (the PC-side visualizer port; UART2 stays the CLI).
 * Per beat frame it emits a "D," line (envelope, flux, beats, peak bins) and,
 * every third frame, an "S," line of the 64-bin display spectrum. Output is
 * queued to a non-blocking ring drained a few bytes per UART_Debug_Tasks pass,
 * so the main loop never busy-waits on the wire; a frame is dropped whole if the
 * ring can't hold it. RX consumes the visualizer's "F,n" band-select line. */

void UART_Debug_Initialize(void);
void UART_Debug_Publish(const BeatFrame *f);   /* call once per new beat frame */
void UART_Debug_Tasks(void);                   /* call once per main-loop pass  */

void    UART_Debug_SetEnabled(bool en);
bool    UART_Debug_IsEnabled(void);
uint8_t UART_Debug_BandSelect(void);           /* last "F,n" from the GUI (0-2) */

#endif /* UART_DEBUG_H */
