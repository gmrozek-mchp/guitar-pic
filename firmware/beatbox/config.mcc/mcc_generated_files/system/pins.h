/**
 * PINS Generated Driver Header File 
 * 
 * @file      pins.h
 *            
 * @defgroup  pinsdriver Pins Driver
 *            
 * @brief     The Pin Driver directs the operation and function of 
 *            the selected device pins using dsPIC MCUs.
 *
 * @skipline @version   PLIB Version 1.0.5
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

#ifndef PINS_H
#define PINS_H
// Section: Includes
#include <xc.h>

/**
 * @ingroup  pinsdriver
 * @brief    Locks all the Peripheral Remapping registers and cannot be written.
 * @return   none  
 */
#define PINS_PPSLock()           (RPCONbits.IOLOCK = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Unlocks all the Peripheral Remapping registers and can be written.
 * @return   none  
 */
#define PINS_PPSUnlock()         (RPCONbits.IOLOCK = 0)

// Section: Device Pin Macros
/**
 * @ingroup  pinsdriver
 * @brief    Sets the RA15 GPIO Pin which has a custom name of T1S_RST to High
 * @pre      The RA15 must be set as Output Pin             
 * @param    none
 * @return   none  
 */
#define T1S_RST_SetHigh()          (_LATA15 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RA15 GPIO Pin which has a custom name of T1S_RST to Low
 * @pre      The RA15 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define T1S_RST_SetLow()           (_LATA15 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Toggles the RA15 GPIO Pin which has a custom name of T1S_RST
 * @pre      The RA15 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define T1S_RST_Toggle()           (_LATA15 ^= 1)

/**
 * @ingroup  pinsdriver
 * @brief    Reads the value of the RA15 GPIO Pin which has a custom name of T1S_RST
 * @param    none
 * @return   none  
 */
#define T1S_RST_GetValue()         _RA15

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RA15 GPIO Pin which has a custom name of T1S_RST as Input
 * @param    none
 * @return   none  
 */
#define T1S_RST_SetDigitalInput()  (_TRISA15 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RA15 GPIO Pin which has a custom name of T1S_RST as Output
 * @param    none
 * @return   none  
 */
#define T1S_RST_SetDigitalOutput() (_TRISA15 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RB2 GPIO Pin which has a custom name of SW3 to High
 * @pre      The RB2 must be set as Output Pin             
 * @param    none
 * @return   none  
 */
#define SW3_SetHigh()          (_LATB2 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RB2 GPIO Pin which has a custom name of SW3 to Low
 * @pre      The RB2 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define SW3_SetLow()           (_LATB2 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Toggles the RB2 GPIO Pin which has a custom name of SW3
 * @pre      The RB2 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define SW3_Toggle()           (_LATB2 ^= 1)

/**
 * @ingroup  pinsdriver
 * @brief    Reads the value of the RB2 GPIO Pin which has a custom name of SW3
 * @param    none
 * @return   none  
 */
#define SW3_GetValue()         _RB2

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RB2 GPIO Pin which has a custom name of SW3 as Input
 * @param    none
 * @return   none  
 */
#define SW3_SetDigitalInput()  (_TRISB2 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RB2 GPIO Pin which has a custom name of SW3 as Output
 * @param    none
 * @return   none  
 */
#define SW3_SetDigitalOutput() (_TRISB2 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RC8 GPIO Pin which has a custom name of LED0 to High
 * @pre      The RC8 must be set as Output Pin             
 * @param    none
 * @return   none  
 */
#define LED0_SetHigh()          (_LATC8 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RC8 GPIO Pin which has a custom name of LED0 to Low
 * @pre      The RC8 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED0_SetLow()           (_LATC8 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Toggles the RC8 GPIO Pin which has a custom name of LED0
 * @pre      The RC8 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED0_Toggle()           (_LATC8 ^= 1)

/**
 * @ingroup  pinsdriver
 * @brief    Reads the value of the RC8 GPIO Pin which has a custom name of LED0
 * @param    none
 * @return   none  
 */
#define LED0_GetValue()         _RC8

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RC8 GPIO Pin which has a custom name of LED0 as Input
 * @param    none
 * @return   none  
 */
#define LED0_SetDigitalInput()  (_TRISC8 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RC8 GPIO Pin which has a custom name of LED0 as Output
 * @param    none
 * @return   none  
 */
#define LED0_SetDigitalOutput() (_TRISC8 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RC9 GPIO Pin which has a custom name of LED1 to High
 * @pre      The RC9 must be set as Output Pin             
 * @param    none
 * @return   none  
 */
#define LED1_SetHigh()          (_LATC9 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RC9 GPIO Pin which has a custom name of LED1 to Low
 * @pre      The RC9 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED1_SetLow()           (_LATC9 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Toggles the RC9 GPIO Pin which has a custom name of LED1
 * @pre      The RC9 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED1_Toggle()           (_LATC9 ^= 1)

/**
 * @ingroup  pinsdriver
 * @brief    Reads the value of the RC9 GPIO Pin which has a custom name of LED1
 * @param    none
 * @return   none  
 */
#define LED1_GetValue()         _RC9

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RC9 GPIO Pin which has a custom name of LED1 as Input
 * @param    none
 * @return   none  
 */
#define LED1_SetDigitalInput()  (_TRISC9 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RC9 GPIO Pin which has a custom name of LED1 as Output
 * @param    none
 * @return   none  
 */
#define LED1_SetDigitalOutput() (_TRISC9 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RC10 GPIO Pin which has a custom name of LED2 to High
 * @pre      The RC10 must be set as Output Pin             
 * @param    none
 * @return   none  
 */
#define LED2_SetHigh()          (_LATC10 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RC10 GPIO Pin which has a custom name of LED2 to Low
 * @pre      The RC10 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED2_SetLow()           (_LATC10 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Toggles the RC10 GPIO Pin which has a custom name of LED2
 * @pre      The RC10 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED2_Toggle()           (_LATC10 ^= 1)

/**
 * @ingroup  pinsdriver
 * @brief    Reads the value of the RC10 GPIO Pin which has a custom name of LED2
 * @param    none
 * @return   none  
 */
#define LED2_GetValue()         _RC10

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RC10 GPIO Pin which has a custom name of LED2 as Input
 * @param    none
 * @return   none  
 */
#define LED2_SetDigitalInput()  (_TRISC10 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RC10 GPIO Pin which has a custom name of LED2 as Output
 * @param    none
 * @return   none  
 */
#define LED2_SetDigitalOutput() (_TRISC10 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RC11 GPIO Pin which has a custom name of LED3 to High
 * @pre      The RC11 must be set as Output Pin             
 * @param    none
 * @return   none  
 */
#define LED3_SetHigh()          (_LATC11 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RC11 GPIO Pin which has a custom name of LED3 to Low
 * @pre      The RC11 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED3_SetLow()           (_LATC11 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Toggles the RC11 GPIO Pin which has a custom name of LED3
 * @pre      The RC11 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED3_Toggle()           (_LATC11 ^= 1)

/**
 * @ingroup  pinsdriver
 * @brief    Reads the value of the RC11 GPIO Pin which has a custom name of LED3
 * @param    none
 * @return   none  
 */
#define LED3_GetValue()         _RC11

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RC11 GPIO Pin which has a custom name of LED3 as Input
 * @param    none
 * @return   none  
 */
#define LED3_SetDigitalInput()  (_TRISC11 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RC11 GPIO Pin which has a custom name of LED3 as Output
 * @param    none
 * @return   none  
 */
#define LED3_SetDigitalOutput() (_TRISC11 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RC12 GPIO Pin which has a custom name of LED4 to High
 * @pre      The RC12 must be set as Output Pin             
 * @param    none
 * @return   none  
 */
#define LED4_SetHigh()          (_LATC12 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RC12 GPIO Pin which has a custom name of LED4 to Low
 * @pre      The RC12 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED4_SetLow()           (_LATC12 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Toggles the RC12 GPIO Pin which has a custom name of LED4
 * @pre      The RC12 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED4_Toggle()           (_LATC12 ^= 1)

/**
 * @ingroup  pinsdriver
 * @brief    Reads the value of the RC12 GPIO Pin which has a custom name of LED4
 * @param    none
 * @return   none  
 */
#define LED4_GetValue()         _RC12

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RC12 GPIO Pin which has a custom name of LED4 as Input
 * @param    none
 * @return   none  
 */
#define LED4_SetDigitalInput()  (_TRISC12 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RC12 GPIO Pin which has a custom name of LED4 as Output
 * @param    none
 * @return   none  
 */
#define LED4_SetDigitalOutput() (_TRISC12 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RC13 GPIO Pin which has a custom name of LED5 to High
 * @pre      The RC13 must be set as Output Pin             
 * @param    none
 * @return   none  
 */
#define LED5_SetHigh()          (_LATC13 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RC13 GPIO Pin which has a custom name of LED5 to Low
 * @pre      The RC13 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED5_SetLow()           (_LATC13 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Toggles the RC13 GPIO Pin which has a custom name of LED5
 * @pre      The RC13 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED5_Toggle()           (_LATC13 ^= 1)

/**
 * @ingroup  pinsdriver
 * @brief    Reads the value of the RC13 GPIO Pin which has a custom name of LED5
 * @param    none
 * @return   none  
 */
#define LED5_GetValue()         _RC13

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RC13 GPIO Pin which has a custom name of LED5 as Input
 * @param    none
 * @return   none  
 */
#define LED5_SetDigitalInput()  (_TRISC13 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RC13 GPIO Pin which has a custom name of LED5 as Output
 * @param    none
 * @return   none  
 */
#define LED5_SetDigitalOutput() (_TRISC13 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RC14 GPIO Pin which has a custom name of LED6 to High
 * @pre      The RC14 must be set as Output Pin             
 * @param    none
 * @return   none  
 */
#define LED6_SetHigh()          (_LATC14 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RC14 GPIO Pin which has a custom name of LED6 to Low
 * @pre      The RC14 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED6_SetLow()           (_LATC14 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Toggles the RC14 GPIO Pin which has a custom name of LED6
 * @pre      The RC14 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED6_Toggle()           (_LATC14 ^= 1)

/**
 * @ingroup  pinsdriver
 * @brief    Reads the value of the RC14 GPIO Pin which has a custom name of LED6
 * @param    none
 * @return   none  
 */
#define LED6_GetValue()         _RC14

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RC14 GPIO Pin which has a custom name of LED6 as Input
 * @param    none
 * @return   none  
 */
#define LED6_SetDigitalInput()  (_TRISC14 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RC14 GPIO Pin which has a custom name of LED6 as Output
 * @param    none
 * @return   none  
 */
#define LED6_SetDigitalOutput() (_TRISC14 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RC15 GPIO Pin which has a custom name of LED7 to High
 * @pre      The RC15 must be set as Output Pin             
 * @param    none
 * @return   none  
 */
#define LED7_SetHigh()          (_LATC15 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RC15 GPIO Pin which has a custom name of LED7 to Low
 * @pre      The RC15 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED7_SetLow()           (_LATC15 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Toggles the RC15 GPIO Pin which has a custom name of LED7
 * @pre      The RC15 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED7_Toggle()           (_LATC15 ^= 1)

/**
 * @ingroup  pinsdriver
 * @brief    Reads the value of the RC15 GPIO Pin which has a custom name of LED7
 * @param    none
 * @return   none  
 */
#define LED7_GetValue()         _RC15

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RC15 GPIO Pin which has a custom name of LED7 as Input
 * @param    none
 * @return   none  
 */
#define LED7_SetDigitalInput()  (_TRISC15 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RC15 GPIO Pin which has a custom name of LED7 as Output
 * @param    none
 * @return   none  
 */
#define LED7_SetDigitalOutput() (_TRISC15 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RD0 GPIO Pin which has a custom name of LED_G to High
 * @pre      The RD0 must be set as Output Pin             
 * @param    none
 * @return   none  
 */
#define LED_G_SetHigh()          (_LATD0 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RD0 GPIO Pin which has a custom name of LED_G to Low
 * @pre      The RD0 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED_G_SetLow()           (_LATD0 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Toggles the RD0 GPIO Pin which has a custom name of LED_G
 * @pre      The RD0 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED_G_Toggle()           (_LATD0 ^= 1)

/**
 * @ingroup  pinsdriver
 * @brief    Reads the value of the RD0 GPIO Pin which has a custom name of LED_G
 * @param    none
 * @return   none  
 */
#define LED_G_GetValue()         _RD0

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RD0 GPIO Pin which has a custom name of LED_G as Input
 * @param    none
 * @return   none  
 */
#define LED_G_SetDigitalInput()  (_TRISD0 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RD0 GPIO Pin which has a custom name of LED_G as Output
 * @param    none
 * @return   none  
 */
#define LED_G_SetDigitalOutput() (_TRISD0 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RD2 GPIO Pin which has a custom name of LED_B to High
 * @pre      The RD2 must be set as Output Pin             
 * @param    none
 * @return   none  
 */
#define LED_B_SetHigh()          (_LATD2 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RD2 GPIO Pin which has a custom name of LED_B to Low
 * @pre      The RD2 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED_B_SetLow()           (_LATD2 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Toggles the RD2 GPIO Pin which has a custom name of LED_B
 * @pre      The RD2 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED_B_Toggle()           (_LATD2 ^= 1)

/**
 * @ingroup  pinsdriver
 * @brief    Reads the value of the RD2 GPIO Pin which has a custom name of LED_B
 * @param    none
 * @return   none  
 */
#define LED_B_GetValue()         _RD2

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RD2 GPIO Pin which has a custom name of LED_B as Input
 * @param    none
 * @return   none  
 */
#define LED_B_SetDigitalInput()  (_TRISD2 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RD2 GPIO Pin which has a custom name of LED_B as Output
 * @param    none
 * @return   none  
 */
#define LED_B_SetDigitalOutput() (_TRISD2 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RD9 GPIO Pin which has a custom name of LED_R to High
 * @pre      The RD9 must be set as Output Pin             
 * @param    none
 * @return   none  
 */
#define LED_R_SetHigh()          (_LATD9 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RD9 GPIO Pin which has a custom name of LED_R to Low
 * @pre      The RD9 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED_R_SetLow()           (_LATD9 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Toggles the RD9 GPIO Pin which has a custom name of LED_R
 * @pre      The RD9 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define LED_R_Toggle()           (_LATD9 ^= 1)

/**
 * @ingroup  pinsdriver
 * @brief    Reads the value of the RD9 GPIO Pin which has a custom name of LED_R
 * @param    none
 * @return   none  
 */
#define LED_R_GetValue()         _RD9

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RD9 GPIO Pin which has a custom name of LED_R as Input
 * @param    none
 * @return   none  
 */
#define LED_R_SetDigitalInput()  (_TRISD9 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RD9 GPIO Pin which has a custom name of LED_R as Output
 * @param    none
 * @return   none  
 */
#define LED_R_SetDigitalOutput() (_TRISD9 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RE2 GPIO Pin which has a custom name of T1S_IRQ_N to High
 * @pre      The RE2 must be set as Output Pin             
 * @param    none
 * @return   none  
 */
#define T1S_IRQ_N_SetHigh()          (_LATE2 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RE2 GPIO Pin which has a custom name of T1S_IRQ_N to Low
 * @pre      The RE2 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define T1S_IRQ_N_SetLow()           (_LATE2 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Toggles the RE2 GPIO Pin which has a custom name of T1S_IRQ_N
 * @pre      The RE2 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define T1S_IRQ_N_Toggle()           (_LATE2 ^= 1)

/**
 * @ingroup  pinsdriver
 * @brief    Reads the value of the RE2 GPIO Pin which has a custom name of T1S_IRQ_N
 * @param    none
 * @return   none  
 */
#define T1S_IRQ_N_GetValue()         _RE2

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RE2 GPIO Pin which has a custom name of T1S_IRQ_N as Input
 * @param    none
 * @return   none  
 */
#define T1S_IRQ_N_SetDigitalInput()  (_TRISE2 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RE2 GPIO Pin which has a custom name of T1S_IRQ_N as Output
 * @param    none
 * @return   none  
 */
#define T1S_IRQ_N_SetDigitalOutput() (_TRISE2 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RE5 GPIO Pin which has a custom name of T1S_CS to High
 * @pre      The RE5 must be set as Output Pin             
 * @param    none
 * @return   none  
 */
#define T1S_CS_SetHigh()          (_LATE5 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RE5 GPIO Pin which has a custom name of T1S_CS to Low
 * @pre      The RE5 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define T1S_CS_SetLow()           (_LATE5 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Toggles the RE5 GPIO Pin which has a custom name of T1S_CS
 * @pre      The RE5 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define T1S_CS_Toggle()           (_LATE5 ^= 1)

/**
 * @ingroup  pinsdriver
 * @brief    Reads the value of the RE5 GPIO Pin which has a custom name of T1S_CS
 * @param    none
 * @return   none  
 */
#define T1S_CS_GetValue()         _RE5

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RE5 GPIO Pin which has a custom name of T1S_CS as Input
 * @param    none
 * @return   none  
 */
#define T1S_CS_SetDigitalInput()  (_TRISE5 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RE5 GPIO Pin which has a custom name of T1S_CS as Output
 * @param    none
 * @return   none  
 */
#define T1S_CS_SetDigitalOutput() (_TRISE5 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RF0 GPIO Pin which has a custom name of SW2 to High
 * @pre      The RF0 must be set as Output Pin             
 * @param    none
 * @return   none  
 */
#define SW2_SetHigh()          (_LATF0 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RF0 GPIO Pin which has a custom name of SW2 to Low
 * @pre      The RF0 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define SW2_SetLow()           (_LATF0 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Toggles the RF0 GPIO Pin which has a custom name of SW2
 * @pre      The RF0 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define SW2_Toggle()           (_LATF0 ^= 1)

/**
 * @ingroup  pinsdriver
 * @brief    Reads the value of the RF0 GPIO Pin which has a custom name of SW2
 * @param    none
 * @return   none  
 */
#define SW2_GetValue()         _RF0

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RF0 GPIO Pin which has a custom name of SW2 as Input
 * @param    none
 * @return   none  
 */
#define SW2_SetDigitalInput()  (_TRISF0 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RF0 GPIO Pin which has a custom name of SW2 as Output
 * @param    none
 * @return   none  
 */
#define SW2_SetDigitalOutput() (_TRISF0 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RF3 GPIO Pin which has a custom name of SW1 to High
 * @pre      The RF3 must be set as Output Pin             
 * @param    none
 * @return   none  
 */
#define SW1_SetHigh()          (_LATF3 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Sets the RF3 GPIO Pin which has a custom name of SW1 to Low
 * @pre      The RF3 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define SW1_SetLow()           (_LATF3 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Toggles the RF3 GPIO Pin which has a custom name of SW1
 * @pre      The RF3 must be set as Output Pin
 * @param    none
 * @return   none  
 */
#define SW1_Toggle()           (_LATF3 ^= 1)

/**
 * @ingroup  pinsdriver
 * @brief    Reads the value of the RF3 GPIO Pin which has a custom name of SW1
 * @param    none
 * @return   none  
 */
#define SW1_GetValue()         _RF3

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RF3 GPIO Pin which has a custom name of SW1 as Input
 * @param    none
 * @return   none  
 */
#define SW1_SetDigitalInput()  (_TRISF3 = 1)

/**
 * @ingroup  pinsdriver
 * @brief    Configures the RF3 GPIO Pin which has a custom name of SW1 as Output
 * @param    none
 * @return   none  
 */
#define SW1_SetDigitalOutput() (_TRISF3 = 0)

/**
 * @ingroup  pinsdriver
 * @brief    Initializes the PINS module
 * @param    none
 * @return   none  
 */
void PINS_Initialize(void);

/**
 * @ingroup  pinsdriver
 * @brief    This function is callback for T1S_IRQ_N Pin
 * @param    none
 * @return   none   
 */
void T1S_IRQ_N_CallBack(void);


/**
 * @ingroup    pinsdriver
 * @brief      This function assigns a function pointer with a callback address
 * @param[in]  InterruptHandler - Address of the callback function 
 * @return     none  
 */
void T1S_IRQ_N_SetInterruptHandler(void (* InterruptHandler)(void));


#endif
