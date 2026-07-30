/**
 * INTERRUPT Generated Driver Source File 
 * 
 * @file      interrupt.c
 *            
 * @ingroup   interruptdriver
 *            
 * @brief     This is the generated driver source file for INTERRUPT driver          
 *
 * @skipline @version   PLIB Version 1.1.5
 *            
 * @skipline  Device : dsPIC33AK512MPS512
*/

/*
© [2026] Microchip Technology Inc. and its subsidiaries.

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
#include "../interrupt.h"

// Section: Driver Interface Function Definitions

void INTERRUPT_Initialize(void)
{
    // AD4CH1: ADC 4 data channel 1 interrupt
    // Priority: 6
    IPC27bits.AD4CH1IP = 6;
    
    // CNE: Change Notice E interrupt
    // Priority: 1
    IPC39bits.CNEIP = 1;
    
    // U2EVT: UART 2 event interrupt
    // Priority: 1
    IPC13bits.U2EVTIP = 1;
    
    // U2E: UART 2 error interrupt
    // Priority: 1
    IPC13bits.U2EIP = 1;
    
    // U2TX: UART 2 TX interrupt
    // Priority: 1
    IPC12bits.U2TXIP = 1;
    
    // U2RX: UART 2 RX interrupt
    // Priority: 1
    IPC12bits.U2RXIP = 1;
    
}

void INTERRUPT_Deinitialize(void)
{
    //POR default value of priority
    IPC27bits.AD4CH1IP = 4;
    IPC39bits.CNEIP = 4;
    IPC13bits.U2EVTIP = 4;
    IPC13bits.U2EIP = 4;
    IPC12bits.U2TXIP = 4;
    IPC12bits.U2RXIP = 4;
}
