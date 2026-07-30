#include <xc.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "uart_debug.h"
#include "beat_detect.h"
#include "nod_engine.h"

void UART_Debug_Initialize(void)
{
    // Async 8N1, fractional BRG mode
    U1CON = 0x8000000UL;
    U1STAT = 0x2E0080UL;
    // 115200 baud from 100 MHz peripheral clock: BRG = 100000000/115200 = 868
    U1BRG = 0x364UL;

    U1CONbits.ON = 1;
    U1CONbits.TXEN = 1;
    U1CONbits.RXEN = 1;
}

// Redirect printf() to UART1
int __attribute__((__section__(".libc.write"))) write(int handle, void *buffer, unsigned int len)
{
    unsigned int i = 0;
    (void)handle;
    while (i < len) {
        while (U1STATbits.TXBF) {}
        U1TXB = *((uint8_t *)buffer + i);
        i++;
    }
    return (int)len;
}

// Beat flags: 0=none, 1=normal red beat, 2=big yellow beat
volatile uint8_t uart_bass_beat = 0;
volatile uint8_t uart_full_beat = 0;
// Phase value from main loop (0-1000)
volatile uint16_t uart_phase = 0;
// BPM from period_frames (0 = no tempo)
volatile uint16_t uart_bpm = 0;
// Band selection: 0=bass, 1=mid+high, 2=auto
volatile uint8_t uart_band_select = 0;  // default to bass
// Servo test mode: -1=normal (phase-driven), 0-1000=hold at position
volatile int16_t servo_test_position = -1;

void UART_Debug_SendFrame(void)
{
    // Format: "D,env,flux,bass_beat,full_beat,phase,frame_time,bpm,bass_flux,bass_peak_bin,full_peak_bin,nod_angle,nod_state,rep_count,pot_offset,winning_band\n"
    printf("D,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,%u\n",
           BeatDetect_GetEnvelopeValue(), BeatDetect_GetFluxValue(),
           uart_bass_beat, uart_full_beat, uart_phase, BeatDetect_GetFrameTime(),
           uart_bpm, BeatDetect_GetBassFluxValue(),
           BeatDetect_GetBassPeakBin(), BeatDetect_GetFullPeakBin(),
           NodEngine_GetTargetAngle(), NodEngine_GetState(),
           NodEngine_GetRepetitionCount(), NodEngine_GetPotOffset(),
           NodEngine_GetWinningBand());
    uart_bass_beat = 0;
    uart_full_beat = 0;
}

void UART_Debug_SendSpectrum(void)
{
    uint8_t bins[BEAT_SPEC_BINS];
    BeatDetect_GetSpectrumData(bins);
    // Format: "S,b0,b1,...,b63\n"
    printf("S");
    for (int i = 0; i < BEAT_SPEC_BINS; i++) {
        printf(",%u", bins[i]);
    }
    printf("\n");
}

// RX buffer for incoming commands
static char rx_buf[32];
static uint8_t rx_idx = 0;

void UART_Debug_ProcessRx(void)
{
    // Clear RX FIFO overflow if set (prevents RX from stalling)
    if (U1STATbits.RXFOIF) {
        U1STATbits.RXFOIF = 0;
    }

    // Non-blocking: process any available bytes
    while (!(U1STATbits.RXBE)) {
        char c = (char)U1RXB;

        if (c == '\n' || c == '\r') {
            if (rx_idx > 0) {
                rx_buf[rx_idx] = '\0';
                // "S,<0-100>" — sensitivity/threshold
                if ((rx_buf[0] == 'T' || rx_buf[0] == 'S') && rx_buf[1] == ',') {
                    int val = atoi(&rx_buf[2]);
                    BeatDetect_SetThreshold((float)val);
                }
                // "M,<0-2>" — detection mode (legacy)
                if (rx_buf[0] == 'M' && rx_buf[1] == ',') {
                    int mode = atoi(&rx_buf[2]);
                    BeatDetect_SetMode((uint8_t)mode);
                }
                // "B,<mask>" — band enable bitmask (0-63, bit0=Sub...bit5=Hi)
                if (rx_buf[0] == 'B' && rx_buf[1] == ',') {
                    int mask = atoi(&rx_buf[2]);
                    BeatDetect_SetBandMask((uint8_t)mask);
                }
                // "F,<0|1|2>" — band select: 0=bass, 1=mid+high, 2=auto
                if (rx_buf[0] == 'F' && rx_buf[1] == ',') {
                    int band = atoi(&rx_buf[2]);
                    if (band >= 0 && band <= 2) uart_band_select = (uint8_t)band;
                }
                // "V,<-1 to 1000>" — servo test: -1=resume normal, 0-1000=hold position
                if (rx_buf[0] == 'V' && rx_buf[1] == ',') {
                    int val = atoi(&rx_buf[2]);
                    if (val < 0) servo_test_position = -1;
                    else if (val > 1000) servo_test_position = 1000;
                    else servo_test_position = (int16_t)val;
                }
                // "O,<0|1>" — oscillator enable: 0=beat-only, 1=oscillator+beat
                if (rx_buf[0] == 'O' && rx_buf[1] == ',') {
                    int val = atoi(&rx_buf[2]);
                    NodEngine_SetOscEnabled((uint8_t)(val != 0));
                }
                rx_idx = 0;
            }
        } else {
            if (rx_idx < sizeof(rx_buf) - 1) {
                rx_buf[rx_idx++] = c;
            }
        }
    }
}
