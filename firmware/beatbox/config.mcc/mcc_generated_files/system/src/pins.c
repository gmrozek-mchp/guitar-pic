/**
 * PINS Generated Driver Source File 
 * 
 * @file      pins.c
 *            
 * @ingroup   pinsdriver
 *            
 * @brief     This is the generated driver source file for PINS driver.
 *
 * @skipline @version   PLIB Version 1.0.5
 *
 * @skipline  Device : dsPIC33AK256MPS306
*/

/*
� [2026] Microchip Technology Inc. and its subsidiaries.

    Subject to your compliance with these terms, you may use Microchip 
    software and any derivatives exclusively with Microchip products. 
    You are responsible for complying with 3rd party license terms  
    applicable to your use of 3rd party software (including open source  
    software) that may accompany Microchip software. SOFTWARE IS ?AS IS.? 
    NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS 
    SOFTWARE, INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT,  
    MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT 
    WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, 
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY 
    KIND WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF 
    MICROCHIP HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE 
    FORESEEABLE. TO THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP?S 
    TOTAL LIABILITY ON ALL CLAIMS RELATED TO THE SOFTWARE WILL NOT 
    EXCEED AMOUNT OF FEES, IF ANY, YOU PAID DIRECTLY TO MICROCHIP FOR 
    THIS SOFTWARE.
*/

// Section: Includes
#include <xc.h>
#include <stddef.h>
#include "../pins.h"

// Section: File specific functions

// Section: Driver Interface Function Definitions
// PPS unlock/lock macros
#define PINS_PPSLock()       (RPCONbits.IOLOCK = 1)
#define PINS_PPSUnlock()     (RPCONbits.IOLOCK = 0)

void PINS_Initialize(void)
{
    /****************************************************************************
     * Setting the Output Latch SFR(s)
     ***************************************************************************/
    LATA = 0x0000UL;
    LATB = 0x0000UL;
    LATC = 0x0000UL;
    LATD = 0x0000UL;

    /****************************************************************************
     * Setting the GPIO Direction SFR(s)
     * RA2=output (Red LED), RA10=output (Green LED)
     * RB8=output (PWM4H left audio), RB9=output (PWM3H right audio)
     * RC0=output (Blue LED), RC10=output (UART1 TX)
     * RD1=output (beat LED)
     ***************************************************************************/
    TRISA = 0x0FFFUL;
    TRISAbits.TRISA2 = 0;      // RA2 output (Red LED)
    TRISAbits.TRISA9 = 0;      // RA9 output (Servo PWM)
    TRISAbits.TRISA10 = 0;     // RA10 output (Green LED)
    TRISB = 0xFCFFUL;          // bits 8,9 cleared (PWM outputs)
    TRISC = 0xFBFEUL;          // bit 0 cleared (Blue LED), bit 10 cleared (UART TX)
    TRISD = 0x01FDUL;          // bit 1 cleared (LED on RD1)

    /****************************************************************************
     * Setting the Weak Pull Up and Weak Pull Down SFR(s)
     ***************************************************************************/
    CNPUA = 0x0000UL;
    CNPUB = 0x0000UL;
    CNPUC = 0x0000UL;
    CNPUD = 0x0000UL;
    CNPDA = 0x0000UL;
    CNPDB = 0x0000UL;
    CNPDC = 0x0000UL;
    CNPDD = 0x0000UL;

    /****************************************************************************
     * Setting the Open Drain SFR(s)
     ***************************************************************************/
    ODCA = 0x0000UL;
    ODCB = 0x0000UL;
    ODCC = 0x0000UL;
    ODCD = 0x0000UL;

    /****************************************************************************
     * Setting the Analog/Digital Configuration SFR(s)
     * RB3=analog (AD2AN3, audio left), RB4=analog (AD2AN4, audio right)
     * RB8,RB9=digital (PWM outputs), RB2,RB12,RB13=digital
     ***************************************************************************/
    ANSELA = 0x0FFFUL;
    ANSELAbits.ANSELA2 = 0;    // RA2 digital (Red LED)
    ANSELAbits.ANSELA9 = 0;    // RA9 digital (Servo PWM)
    ANSELAbits.ANSELA10 = 0;   // RA10 digital (Green LED)
    ANSELB = 0x0CFBUL;         // RB3,RB4 analog; RB2,RB8,RB9,RB12,RB13 digital
    ANSELC = 0x00C0UL;
    ANSELD = 0x0060UL;

    /****************************************************************************
     * PPS Configuration
     ***************************************************************************/
    PINS_PPSUnlock();

    // UART1 TX on RC10/RP43 (PPS code 10 = U1TX on 306)
    RPOR10bits.RP43R = 0x000AUL;
    // UART1 RX on RC4/RP37
    RPINR13bits.U1RXR = 37;

    // PWM4H (left audio) on RB8/RP25 (PPS code 7)
    RPOR6bits.RP25R = 7;
    // PWM3H (right audio) on RB9/RP26 (PPS code 5)
    RPOR6bits.RP26R = 5;

    // RGB LED: SCCP OCM outputs
    // Green = SCCP1/OCM1 -> RA10/RP11, PPS code 29
    RPOR2bits.RP11R = 29;
    // Red = SCCP2/OCM2 -> RA2/RP3, PPS code 30
    RPOR0bits.RP3R = 30;
    // Blue = SCCP3/OCM3 -> RC0/RP33, PPS code 31
    RPOR8bits.RP33R = 31;

    // Servo: SCCP4/OCM4 -> RA9/RP10, PPS code 32
    RPOR2bits.RP10R = 32;

    //PINS_PPSLock();
}

