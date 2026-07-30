/**
 * UART2 Generated Driver Header File
 * 
 * @file      uart2.h
 *            
 * @ingroup   uartdriver
 *            
 * @brief     This is the generated driver header file for the UART2 driver
 *
 * @skipline @version   PLIB Version 1.1.3
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

#ifndef UART2_H
#define UART2_H

// Section: Included Files

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "uart_interface.h"

// Section: Data Type Definitions

/**
 @ingroup  uartdriver
 @brief    Structure object of type UART_INTERFACE with the 
           custom name given by the user in the Melody Driver User interface. 
           The default name e.g. UART1 can be changed by the 
           user in the UART user interface. 
           This allows defining a structure with application specific name 
           using the 'Custom Name' field. Application specific name allows the 
           API Portability.
*/
extern const struct UART_INTERFACE UART2_Drv;

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_Initialize API
 */
#define UART2_Drv_Initialize UART2_Initialize

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_Deinitialize API
 */
#define UART2_Drv_Deinitialize UART2_Deinitialize

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_Read API
 */
#define UART2_Drv_Read UART2_Read

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_Write API
 */
#define UART2_Drv_Write UART2_Write

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_IsRxReady API
 */
#define UART2_Drv_IsRxReady UART2_IsRxReady

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_IsTxReady API
 */
#define UART2_Drv_IsTxReady UART2_IsTxReady

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_IsTxDone API
 */
#define UART2_Drv_IsTxDone UART2_IsTxDone

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_TransmitEnable API
 */
#define UART2_Drv_TransmitEnable UART2_TransmitEnable

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_TransmitDisable API
 */
#define UART2_Drv_TransmitDisable UART2_TransmitDisable

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_AutoBaudSet API
 */
#define UART2_Drv_AutoBaudSet UART2_AutoBaudSet

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_AutoBaudQuery API
 */
#define UART2_Drv_AutoBaudQuery UART2_AutoBaudQuery

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_AutoBaudEventEnableGet API
 */
#define UART2_Drv_AutoBaudEventEnableGet UART2_AutoBaudEventEnableGet

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_ErrorGet API
 */
#define UART2_Drv_ErrorGet UART2_ErrorGet

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_BRGCountSet API
 */
#define UART2_Drv_BRGCountSet UART2_BRGCountSet

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_BRGCountGet API
 */
#define UART2_Drv_BRGCountGet UART2_BRGCountGet

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_BaudRateSet API
 */
#define UART2_Drv_BaudRateSet UART2_BaudRateSet

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_BaudRateGet API
 */
#define UART2_Drv_BaudRateGet UART2_BaudRateGet

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_RxCompleteCallbackRegister API
 */
#define UART2_Drv_RxCompleteCallbackRegister  UART2_RxCompleteCallbackRegister

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_TxCompleteCallbackRegister API
 */
#define UART2_Drv_TxCompleteCallbackRegister  UART2_TxCompleteCallbackRegister

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_TxCollisionCallbackRegister API
 */
#define UART2_Drv_TxCollisionCallbackRegister  UART2_TxCollisionCallbackRegister

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_FramingErrorCallbackRegister API
 */
#define UART2_Drv_FramingErrorCallbackRegister  UART2_FramingErrorCallbackRegister

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_OverrunErrorCallbackRegister API
 */
#define UART2_Drv_OverrunErrorCallbackRegister  UART2_OverrunErrorCallbackRegister

/**
 * @ingroup  uartdriver
 * @brief    This macro defines the Custom Name for \ref UART2_ParityErrorCallbackRegister API
 */
#define UART2_Drv_ParityErrorCallbackRegister  UART2_ParityErrorCallbackRegister

// Section: UART2 Driver Routines

/**
 * @ingroup  uartdriver
 * @brief    Initializes the UART driver
 * @param    none
 * @return   none
 */
void UART2_Initialize(void);

/**
 * @ingroup  uartdriver
 * @brief    Deinitializes the UART to POR values
 * @param    none
 * @return   none
 */
void UART2_Deinitialize(void);

/**
 * @ingroup  uartdriver
 * @brief    Reads a byte of data from the UART2
 * @pre      Check receive status using \ref UART2_IsRxReady function
 *           before calling this function or call this function within the 
 *           \ref UART2_RxCompleteCallback. 
 * @param    none
 * @return   A data byte received by the driver.
 */
uint8_t UART2_Read(void);

/**
 * @ingroup    uartdriver
 * @brief      Writes a byte of data to the UART2
 * @pre        UART2_Initialize function should have been called
 *             before calling this function. The transfer status should be checked
 *             to see if transmitter is not full before calling this function.
 * @param[in]  data - Data byte to write to the UART2
 * @return     none
 */
void UART2_Write(uint8_t data);

/**
 * @ingroup  uartdriver
 * @brief    Returns a boolean value if data is available to read
 * @param    none
 * @return   true  - Data available to read
 * @return   false - Data not available to read
 */
bool UART2_IsRxReady(void);

/**
 * @ingroup  uartdriver
 * @brief    Returns a boolean value if data can be written
 * @param    none
 * @return   true    - Data can be written
 * @return   false   - Data can not be written
 */
bool UART2_IsTxReady(void);

/**
 * @ingroup  uartdriver
 * @brief    Indicates if all bytes have been transferred
 * @param    none
 * @return   true    - All bytes transferred
 * @return   false   - Data transfer is pending
 */
bool UART2_IsTxDone(void);

/**
 * @ingroup  uartdriver
 * @brief    Enables UART2 transmit 
 * @param    none
 * @return   none
 */
void UART2_TransmitEnable(void);

/**
 * @ingroup  uartdriver
 * @brief    Disables UART2 transmit 
 * @param    none
 * @return   none
 */
void UART2_TransmitDisable(void);

/**
 * @ingroup  uartdriver
 * @brief    Enables or disables UART2 Auto-Baud detection
 * @param[in]  enable - true, starts the auto-baud detection  
 * @param[in]  enable - false, disables the auto-baud detection  
 * @return   none
 */
void UART2_AutoBaudSet(bool enable);

/**
 * @ingroup  uartdriver
 * @brief    Returns the status of Auto-Baud detection
 * @param    none
 * @return   true    - Auto-Baud detection in progress or counter overflow occurred
 * @return   false   - Auto-Baud detection is complete or disabled
 */
bool UART2_AutoBaudQuery(void);

/**
 * @ingroup  uartdriver
 * @brief    Returns enable state of the Auto-Baud feature
 * @param    none
 * @return   true    - Auto-Baud is enabled
 * @return   false   - Auto-Baud is disabled
 */
bool UART2_AutoBaudEventEnableGet(void);

/**
 * @ingroup  uartdriver
 * @brief    Sets the BRG value of UART2
 * @param[in]   baudRate - BRG value upto 20 bits   
 * @return   none
 * @note    Make sure the is no transmission in progress using \ref UART2_IsTxDone function
 */
void UART2_BRGCountSet(uint32_t brgValue);

/**
 * @ingroup  uartdriver
 * @brief    Gets the BRG value of UART2
 * @param    none
 * @return   Combined BRG value upto 20 bits
 */
uint32_t UART2_BRGCountGet(void);

/**
 * @ingroup  uartdriver
 * @brief    Sets the calculated Baud-Rate of UART2
 * @param[in]   baudRate - Value of Baud-Rate to be set  
 * @return   none
 * @note    Make sure the is no transmission in progress using \ref UART2_IsTxDone function
 */
void UART2_BaudRateSet(uint32_t baudRate);

/**
 * @ingroup  uartdriver
 * @brief    Gets the actual Baud-Rate of UART2
 * @param    none
 * @return   Actual baud-rate of UART2
 */
uint32_t UART2_BaudRateGet(void);

/**
 * @ingroup  uartdriver
 * @brief    Returns the error status of UART2
 * @param    none
 * @return   Errors with masking as per \ref UART2_ERROR_MASKS
 */
size_t UART2_ErrorGet(void);

/**
 * @ingroup         uartdriver
 * @brief           This function can be used to override default callback 
 *                  \ref UART2_RxCompleteCallback and to define custom callback for 
 *                  UART2 RxComplete event.
 * @param[in][out]  Address of the callback routine
 * @return          none
 */
void UART2_RxCompleteCallbackRegister(void (*handler)(void));

/**
 * @ingroup  uartdriver
 * @brief    This is the default callback with weak attribute. The user can 
 *           override and implement the default callback without weak attribute 
 *           or can register a custom callback function using UART2_RxCompleteCallbackRegister.
 * @param    none
 * @return   none
 */
void UART2_RxCompleteCallback(void);

/**
 * @ingroup         uartdriver
 * @brief           This function can be used to override default callback 
 *                  \ref UART2_TxCompleteCallback and to define custom callback for 
 *                  UART2 TxComplete event.
 * @param[in][out]  Address of the callback routine
 * @return          none
 */
void UART2_TxCompleteCallbackRegister(void (*handler)(void));

/**
 * @ingroup  uartdriver
 * @brief    This is the default callback with weak attribute. The user can 
 *           override and implement the default callback without weak attribute 
 *           or can register a custom callback function using UART2_TxCompleteCallbackRegister.
 * @param    none
 * @return   none
 * @note            This callback is triggerred if there is data in the software buffer and there is atleast 
 *                  one byte space in hardware transmit FIFO 
 */
void UART2_TxCompleteCallback(void);

/**
 * @ingroup         uartdriver
 * @brief           This function can be used to override default callback 
 *                  \ref UART2_TxCollisionCallback and to define custom callback for 
 *                  UART2 TxCollision event.
 * @param[in][out]  Address of the callback routine
 * @return          none
 */
void UART2_TxCollisionCallbackRegister(void (*handler)(void));

/**
 * @ingroup  uartdriver
 * @brief    This is the default callback with weak attribute. The user can 
 *           override and implement the default callback without weak attribute 
 *           or can register a custom callback function using UART2_TxCollisionCallbackRegister.
 * @param    none
 * @return   none
 */
void UART2_TxCollisionCallback(void);

/**
 * @ingroup         uartdriver
 * @brief           This function can be used to override default callback 
 *                  \ref UART2_FramingErrorCallback and to define custom callback for 
 *                  UART2 FramingError event.
 * @param[in][out]  Address of the callback routine
 * @return          none
 */
void UART2_FramingErrorCallbackRegister(void (*handler)(void));

/**
 * @ingroup  uartdriver
 * @brief    This is the default callback with weak attribute. The user can 
 *           override and implement the default callback without weak attribute 
 *           or can register a custom callback function using UART2_FramingErrorCallbackRegister.
 * @param    none
 * @return   none
 */
void UART2_FramingErrorCallback(void);

/**
 * @ingroup         uartdriver
 * @brief           This function can be used to override default callback 
 *                  \ref UART2_OverrunErrorCallback and to define custom callback for 
 *                  UART2 OverrunError event.
 * @param[in][out]  Address of the callback routine
 * @return          none
 */
void UART2_OverrunErrorCallbackRegister(void (*handler)(void));

/**
 * @ingroup  uartdriver
 * @brief    This is the default callback with weak attribute. The user can 
 *           override and implement the default callback without weak attribute 
 *           or can register a custom callback function using UART2_OverrunErrorCallbackRegister.
 * @param    none
 * @return   none
 */
void UART2_OverrunErrorCallback(void);

/**
 * @ingroup         uartdriver
 * @brief           This function can be used to override default callback 
 *                  \ref UART2_ParityErrorCallback and to define custom callback for 
 *                  UART2 ParityError event.
 * @param[in][out]  Address of the callback routine
 * @return          none
 */
void UART2_ParityErrorCallbackRegister(void (*handler)(void));

/**
 * @ingroup  uartdriver
 * @brief    This is the default callback with weak attribute. The user can 
 *           override and implement the default callback without weak attribute 
 *           or can register a custom callback function using UART2_ParityErrorCallbackRegister.
 * @param    none
 * @return   none
 */
void UART2_ParityErrorCallback(void);
#endif  // UART2_H

