#ifndef UART_DEBUG_H
#define UART_DEBUG_H

#include <stdint.h>

void UART_Debug_Initialize(void);
void UART_Debug_SendFrame(void);
void UART_Debug_SendSpectrum(void);
void UART_Debug_ProcessRx(void);

extern volatile uint8_t uart_bass_beat;     // 0=none, 1=normal, 2=big
extern volatile uint8_t uart_full_beat;     // 0=none, 1=normal, 2=big
extern volatile uint16_t uart_phase;
extern volatile uint16_t uart_bpm;
extern volatile uint8_t uart_band_select;   // 0=bass, 1=mid+high, 2=auto
extern volatile int16_t servo_test_position; // -1=normal, 0-1000=test mode position

#endif
