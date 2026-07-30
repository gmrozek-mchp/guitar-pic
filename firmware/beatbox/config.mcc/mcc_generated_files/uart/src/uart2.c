/**
 * UART2 Generated Driver Source File
 * 
 * @file        uart2.c
 *  
 * @ingroup     uartdriver
 *  
 * @brief       This is the generated driver source file for the UART2 driver
 *
 * @skipline @version     PLIB Version 1.1.3
 *
 * @skipline    Device : dsPIC33AK512MPS512
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

// Section: Included Files
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <xc.h>
#include "../uart2.h"

// Section: Macro Definitions
#define UART2_CLOCK 100000000U
#define UART2_BAUD_TO_BRG_WITH_FRACTIONAL(x) (UART2_CLOCK/(x))
#define UART2_BAUD_TO_BRG_WITH_BRGS_1(x) (UART2_CLOCK/(4U*(x))-1U)
#define UART2_BAUD_TO_BRG_WITH_BRGS_0(x) (UART2_CLOCK/(16U*(x))-1U)
#define UART2_BRG_TO_BAUD_WITH_FRACTIONAL(x) (UART2_CLOCK/(x))
#define UART2_BRG_TO_BAUD_WITH_BRGS_1(x) (UART2_CLOCK/(4U*((x)+1U)))
#define UART2_BRG_TO_BAUD_WITH_BRGS_0(x) (UART2_CLOCK/(16U*((x)+1U)))

#define UART2_MIN_ACHIEVABLE_BAUD_WITH_FRACTIONAL 95U
#define UART2_MIN_ACHIEVABLE_BAUD_WITH_BRGS_1 24U

// Section: Driver Interface

const struct UART_INTERFACE UART2_Drv = {
    .Initialize = &UART2_Initialize,
    .Deinitialize = &UART2_Deinitialize,
    .Read = &UART2_Read,
    .Write = &UART2_Write,
    .IsRxReady = &UART2_IsRxReady,
    .IsTxReady = &UART2_IsTxReady,
    .IsTxDone = &UART2_IsTxDone,
    .TransmitEnable = &UART2_TransmitEnable,
    .TransmitDisable = &UART2_TransmitDisable,
    .TransmitInterruptEnable = NULL,
    .TransmitInterruptDisable = NULL,
    .AutoBaudSet = &UART2_AutoBaudSet,
    .AutoBaudQuery = &UART2_AutoBaudQuery,
    .AutoBaudEventEnableGet = &UART2_AutoBaudEventEnableGet,
    .BRGCountSet = &UART2_BRGCountSet,
    .BRGCountGet = &UART2_BRGCountGet,
    .BaudRateSet = &UART2_BaudRateSet,
    .BaudRateGet = &UART2_BaudRateGet,
    .ErrorGet = &UART2_ErrorGet,
    .RxCompleteCallbackRegister = &UART2_RxCompleteCallbackRegister,
    .TxCompleteCallbackRegister = &UART2_TxCompleteCallbackRegister,
    .TxCollisionCallbackRegister = &UART2_TxCollisionCallbackRegister,
    .FramingErrorCallbackRegister = &UART2_FramingErrorCallbackRegister,
    .OverrunErrorCallbackRegister = &UART2_OverrunErrorCallbackRegister,
    .ParityErrorCallbackRegister = &UART2_ParityErrorCallbackRegister,
};

// Section: Private Variable Definitions

static volatile bool softwareBufferEmpty = true;
static union
{
    struct
    {
        uint16_t frammingError :1;
        uint16_t parityError :1;
        uint16_t overrunError :1;
        uint16_t txCollisionError :1;
        uint16_t autoBaudOverflow :1;
        uint16_t reserved :11;
    };
    size_t status;
} uartError;

// Section: Data Type Definitions

/**
 @ingroup  uartdriver
 @static   UART Driver Queue Status
 @brief    Defines the object required for the status of the queue
*/
static uint8_t * volatile rxTail;
static uint8_t * volatile rxHead;
static uint8_t * volatile txTail;
static uint8_t * volatile txHead;
static bool volatile rxOverflowed;

/**
 @ingroup  uartdriver
 @brief    Defines the length of the Transmit and Receive Buffers
*/

/* We add one extra byte than requested so that we don't have to have a separate
 * bit to determine the difference between buffer full and buffer empty, but
 * still be able to hold the amount of data requested by the user.  Empty is
 * when head == tail.  So full will result in head/tail being off by one due to
 * the extra byte.
 */
#define UART2_CONFIG_TX_BYTEQ_LENGTH (256+1)
#define UART2_CONFIG_RX_BYTEQ_LENGTH (128+1)

/**
 @ingroup  uartdriver
 @static   UART Driver Queue
 @brief    Defines the Transmit and Receive Buffers
*/
static uint8_t txQueue[UART2_CONFIG_TX_BYTEQ_LENGTH];
static uint8_t rxQueue[UART2_CONFIG_RX_BYTEQ_LENGTH];

static void (*UART2_RxCompleteHandler)(void);
static void (*UART2_TxCompleteHandler)(void);
static void (*UART2_TxCollisionHandler)(void);
static void (*UART2_FramingErrorHandler)(void);
static void (*UART2_OverrunErrorHandler)(void);
static void (*UART2_ParityErrorHandler)(void);

// Section: Driver Interface

void UART2_Initialize(void)
{
    IEC3bits.U2TXIE = 0;
    IEC3bits.U2RXIE = 0;
    IEC3bits.U2EVTIE = 0;

    // MODE Asynchronous 8-bit UART; RXEN ; TXEN ; ABDEN ; BRGS ; SENDB ; BRKOVR ; RXBIMD ; WUE ; SIDL ; ON disabled; FLO ; TXPOL ; C0EN ; STP 1 Stop bit sent, 1 checked at RX; RXPOL ; RUNOVF ; HALFDPLX ; CLKSEL Standard Speed Peripheral Clock; CLKMOD enabled; ACTIVE ; SLPEN ; 
    U2CON = 0x8000000UL;
    // TXCIF ; RXFOIF ; RXBKIF ; CERIF ; ABDOVIF ; TXCIE ; RXFOIE ; RXBKIE ; FERIE ; CERIE ; ABDOVIE ; PERIE ; TXMTIE ; STPMD ; TXWRE ; RXWM ; TXWM TX_BUF_EMPTY; 
    U2STAT = 0x2E0080UL;
    // BaudRate 115207.37; Frequency 100000000 Hz; BRG 868; 
    U2BRG = 0x364UL;
    
    txHead = txQueue;
    txTail = txQueue;
    rxHead = rxQueue;
    rxTail = rxQueue;
   
    rxOverflowed = false;
    
    UART2_RxCompleteCallbackRegister(&UART2_RxCompleteCallback);
    UART2_TxCompleteCallbackRegister(&UART2_TxCompleteCallback);
    UART2_TxCollisionCallbackRegister(&UART2_TxCollisionCallback);
    UART2_FramingErrorCallbackRegister(&UART2_FramingErrorCallback);
    UART2_OverrunErrorCallbackRegister(&UART2_OverrunErrorCallback);
    UART2_ParityErrorCallbackRegister(&UART2_ParityErrorCallback);

    // UART Frame error interrupt
    U2STATbits.FERIF = 1;
    // UART Parity error interrupt
    U2STATbits.PERIF = 1;
    // UART Receive Buffer Overflow interrupt
    U2STATbits.RXFOIE = 1;
    // UART Transmit collision interrupt
    U2STATbits.TXCIE = 1;
    // UART Auto-Baud Overflow interrupt
    U2STATbits.ABDOVIE = 1;  
    // UART Receive Interrupt
    IEC3bits.U2RXIE = 1;
    // UART Event interrupt
    IEC3bits.U2EVTIE = 1;
    // UART Error interrupt
    IEC3bits.U2EIE    = 1;
    
    //Make sure to set LAT bit corresponding to TxPin as high before UART initialization
    U2CONbits.ON = 1;   // enabling UART ON bit
    U2CONbits.TXEN = 1;
    U2CONbits.RXEN = 1;
}

void UART2_Deinitialize(void)
{
    // UART Transmit interrupt
    IFS3bits.U2TXIF = 0;
    IEC3bits.U2TXIE = 0;
    
    // UART Receive Interrupt
    IFS3bits.U2RXIF = 0;
    IEC3bits.U2RXIE = 0;
    
    // UART Event interrupt
    IFS3bits.U2EVTIF = 0;
    IEC3bits.U2EVTIE = 0;
    
    // UART Error interrupt
    IFS3bits.U2EIF = 0;
    IEC3bits.U2EIE    = 0;
    
    U2CON = 0x0UL;
    U2STAT = 0x2E0080UL;
    U2BRG = 0x0UL;
}

uint8_t UART2_Read(void)
{
    uint8_t data = 0;

    if(rxHead != rxTail)
	{
		data = *rxHead;

		rxHead++;

		if (rxHead == &rxQueue[UART2_CONFIG_RX_BYTEQ_LENGTH])
		{
			rxHead = rxQueue;
		}
	}
    return data;
}

void UART2_Write(uint8_t byte)
{
    while(UART2_IsTxReady() == 0)
    {
    }

    *txTail = byte;

    txTail++;
        
    if (txTail == &txQueue[UART2_CONFIG_TX_BYTEQ_LENGTH])
    {
        txTail = txQueue;
    }

    IEC3bits.U2TXIE = 1;
    softwareBufferEmpty = false;
}

bool UART2_IsRxReady(void)
{    
    return !(rxHead == rxTail);
}

bool UART2_IsTxReady(void)
{
    uint16_t size;
    uint8_t *snapshot_txHead = (uint8_t*)txHead;
    
    if (txTail < snapshot_txHead)
    {
        /* cppcheck-suppress misra-c2012-18.4
        *   Subtracting two pointers to get no of bytes transmit
        */
        size = (snapshot_txHead - txTail - 1);
    }
    else
    {
        /* cppcheck-suppress misra-c2012-18.4
        *   Subtracting two pointers to get no of bytes transmit
        */
        size = ( UART2_CONFIG_TX_BYTEQ_LENGTH - (txTail - snapshot_txHead) - (uint16_t)1 );
    }
    
    return (size != (uint16_t)0);
}

bool UART2_IsTxDone(void)
{
    bool status = false;
    
    if(txTail == txHead)
    {
        status = (bool)(U2STATbits.TXMTIF && U2STATbits.TXBE);
    }
    
    return status;
}

void UART2_TransmitEnable(void)
{
    U2CONbits.TXEN = 1;
}

void UART2_TransmitDisable(void)
{
    U2CONbits.TXEN = 0;
}

void UART2_AutoBaudSet(bool enable)
{
    U2UIRbits.ABDIF = 0U;
    U2UIRbits.ABDIE = enable;
    U2CONbits.ABDEN = enable;
}

bool UART2_AutoBaudQuery(void)
{
    return U2CONbits.ABDEN;
}

bool UART2_AutoBaudEventEnableGet(void)
{ 
    return U2UIRbits.ABDIE; 
}


void UART2_BRGCountSet(uint32_t brgValue)
{
    U2BRG = brgValue;
}

uint32_t UART2_BRGCountGet(void)
{
    return U2BRG;
}

void UART2_BaudRateSet(uint32_t baudRate)
{
    uint32_t brgValue;
    
    if((baudRate >= UART2_MIN_ACHIEVABLE_BAUD_WITH_FRACTIONAL) && (baudRate != 0U))
    {
        U2CONbits.CLKMOD = 1;
        U2CONbits.BRGS = 0;
        brgValue = UART2_BAUD_TO_BRG_WITH_FRACTIONAL(baudRate);
    }
    else if(baudRate >= UART2_MIN_ACHIEVABLE_BAUD_WITH_BRGS_1)
    {
        U2CONbits.CLKMOD = 0;
        U2CONbits.BRGS = 1;
        brgValue = UART2_BAUD_TO_BRG_WITH_BRGS_1(baudRate);
    }
    else
    {
        U2CONbits.CLKMOD = 0;
        U2CONbits.BRGS = 0;
        brgValue = UART2_BAUD_TO_BRG_WITH_BRGS_0(baudRate);
    }
    U2BRG = brgValue;

}

uint32_t UART2_BaudRateGet(void)
{
    uint32_t brgValue;
    uint32_t baudRate;
    
    brgValue = UART2_BRGCountGet();
    if((U2CONbits.CLKMOD == 1U) && (brgValue != 0U))
    {
        baudRate = UART2_BRG_TO_BAUD_WITH_FRACTIONAL(brgValue);
    }
    else if(U2CONbits.BRGS == 1)
    {
        baudRate = UART2_BRG_TO_BAUD_WITH_BRGS_1(brgValue);
    }
    else
    {
        baudRate = UART2_BRG_TO_BAUD_WITH_BRGS_0(brgValue);
    }
    return baudRate;
}

size_t UART2_ErrorGet(void)
{
    size_t fetchUartError = uartError.status;
    uartError.status = 0;
    return fetchUartError;
}

void UART2_RxCompleteCallbackRegister(void (*handler)(void))
{
    if(NULL != handler)
    {
        UART2_RxCompleteHandler = handler;
    }
}

void __attribute__ ((weak)) UART2_RxCompleteCallback(void)
{ 

} 

void UART2_TxCompleteCallbackRegister(void (*handler)(void))
{
    if(NULL != handler)
    {
        UART2_TxCompleteHandler = handler;
    }
}

void __attribute__ ((weak)) UART2_TxCompleteCallback(void)
{ 

} 

void UART2_TxCollisionCallbackRegister(void (*handler)(void))
{
    if(NULL != handler)
    {
        UART2_TxCollisionHandler = handler;
    }
}

void __attribute__ ((weak)) UART2_TxCollisionCallback(void)
{ 

} 

void UART2_FramingErrorCallbackRegister(void (*handler)(void))
{
    if(NULL != handler)
    {
        UART2_FramingErrorHandler = handler;
    }
}

void __attribute__ ((weak)) UART2_FramingErrorCallback(void)
{ 

} 

void UART2_OverrunErrorCallbackRegister(void (*handler)(void))
{
    if(NULL != handler)
    {
        UART2_OverrunErrorHandler = handler;
    }
}

void __attribute__ ((weak)) UART2_OverrunErrorCallback(void)
{ 

} 

void UART2_ParityErrorCallbackRegister(void (*handler)(void))
{
    if(NULL != handler)
    {
        UART2_ParityErrorHandler = handler;
    }
}

void __attribute__ ((weak)) UART2_ParityErrorCallback(void)
{ 

} 

/* cppcheck-suppress misra-c2012-8.4
*
* (Rule 8.4) REQUIRED: A compatible declaration shall be visible when an object or 
* function with external linkage is defined
*
* Reasoning: Interrupt declaration are provided by compiler and are available
* outside the driver folder
*/
void __attribute__ ( ( interrupt ) ) _U2TXInterrupt(void)
{

    if(txHead == txTail)
    {
                if(NULL != UART2_TxCompleteHandler)
            {
                (*UART2_TxCompleteHandler)();
            }
                                                IEC3bits.U2TXIE = 0;
        softwareBufferEmpty = true;
    }
    else
    {

        while(!(U2STATbits.TXBF == 1))
        {
            U2TXB = *txHead;
            txHead++;

            if(txHead == &txQueue[UART2_CONFIG_TX_BYTEQ_LENGTH])
            {
                txHead = txQueue;
            }

            // Are we empty?
            if(txHead == txTail)
            {
                break;
            }
        }
    }
}

/* cppcheck-suppress misra-c2012-8.4
*
* (Rule 8.4) REQUIRED: A compatible declaration shall be visible when an object or 
* function with external linkage is defined
*
* Reasoning: Interrupt declaration are provided by compiler and are available
* outside the driver folder
*/
void __attribute__ ( ( interrupt ) ) _U2RXInterrupt(void)
{
    size_t rxQueueSize;
    uint8_t *rxTailPtr = NULL;
    
    IFS3bits.U2RXIF = 0;
    
    while(!(U2STATbits.RXBE == 1))
    {
        *rxTail = U2RXB;

        rxQueueSize = UART2_CONFIG_RX_BYTEQ_LENGTH - 1;
        rxTailPtr = rxTail;
        rxTailPtr++;
        // Will the increment not result in a wrap and not result in a pure collision?
        // This is most often condition so check first
        if ((rxTail != &rxQueue[rxQueueSize]) && (rxTailPtr != rxHead))
        {
            rxTail++;
        } 
        else if ( (rxTail == &rxQueue[rxQueueSize]) &&
                  (rxHead !=  rxQueue) )
        {
            // Pure wrap no collision
            rxTail = rxQueue;
        } 
        else // must be collision
        {
            rxOverflowed = true;
        }
    }
	
    if(NULL != UART2_RxCompleteHandler)
    {
        (*UART2_RxCompleteHandler)();
    }
}

/* cppcheck-suppress misra-c2012-8.4
*
* (Rule 8.4) REQUIRED: A compatible declaration shall be visible when an object or 
* function with external linkage is defined
*
* Reasoning: Interrupt declaration are provided by compiler and are available
* outside the driver folder
*/
void __attribute__ ( ( interrupt ) ) _U2EInterrupt(void)
{
    if (U2STATbits.ABDOVIF == 1)
    {
        uartError.status = uartError.status|(uint16_t)UART_ERROR_AUTOBAUD_OVERFLOW_MASK;
        U2STATbits.ABDOVIF = 0;
    }
    
    if (U2STATbits.TXCIF == 1)
    {
        uartError.status = uartError.status|(uint16_t)UART_ERROR_TX_COLLISION_MASK;
        if(NULL != UART2_TxCollisionHandler)
        {
            (*UART2_TxCollisionHandler)();
        }
        
        U2STATbits.TXCIF = 0;
    }
    
    if (U2STATbits.RXFOIF == 1)
    {
        uartError.status = uartError.status|(uint16_t)UART_ERROR_RX_OVERRUN_MASK;
        if(NULL != UART2_OverrunErrorHandler)
        {
            (*UART2_OverrunErrorHandler)();
        }
        
        U2STATbits.RXFOIF = 0;
    }
    
    if (U2STATbits.PERIF == 1)
    {
        uartError.status = uartError.status|(uint16_t)UART_ERROR_PARITY_MASK;
        if(NULL != UART2_ParityErrorHandler)
        {
            (*UART2_ParityErrorHandler)();
        }
    }
    
    if (U2STATbits.FERIF == 1)
    {
        uartError.status = uartError.status|(uint16_t)UART_ERROR_FRAMING_MASK;
        if(NULL != UART2_FramingErrorHandler)
        {
            (*UART2_FramingErrorHandler)();
        }
    }
        
    IFS3bits.U2EIF = 0;
}

/* ISR for UART Event Interrupt */
/* cppcheck-suppress misra-c2012-8.4
*
* (Rule 8.4) REQUIRED: A compatible declaration shall be visible when an object or 
* function with external linkage is defined
*
* Reasoning: Interrupt declaration are provided by compiler and are available
* outside the driver folder
*/
void __attribute__ ( ( interrupt ) ) _U2EVTInterrupt(void)
{
    U2UIRbits.ABDIF = false;
    IFS3bits.U2EVTIF = false;
}
