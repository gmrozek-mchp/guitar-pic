/*******************************************************************************
  Main Source File

  Company:
    Microchip Technology Inc.

  File Name:
    main.c

  Summary:
    This file contains the "main" function for a project.

  Description:
    This file contains the "main" function for a project.  The
    "main" function calls the "SYS_Initialize" function to initialize the state
    machines of all modules in the system
 *******************************************************************************/

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include <stddef.h>                     // Defines NULL
#include <stdbool.h>                    // Defines true
#include <stdlib.h>                     // Defines EXIT_FAILURE
#include "definitions.h"                // SYS function prototypes
#include "t1s_follower.h"               // 10BASE-T1S PLCA follower (LAN8651)
#include "cli.h"                        // operator CLI on the SERCOM1 debug UART

// *****************************************************************************
// *****************************************************************************
// Section: Main Entry Point
// *****************************************************************************
// *****************************************************************************

int main ( void )
{
    /* Initialize all modules */
    SYS_Initialize ( NULL );

    /* MCC only initializes SysTick; the app enables it. Started here so
     * SYSTICK_DelayMs works for every subsystem. */
    SYSTICK_TimerStart ( );

    T1SFollower_Initialize ( );
    CLI_Initialize ( );

    while ( true )
    {
        /* Maintain state machines of all polled MPLAB Harmony modules. */
        SYS_Tasks ( );

        /* Service the T1S link: sync the MAC-PHY and emit the presence heartbeat. */
        T1SFollower_Tasks ( );

        /* Operator CLI on the debug UART (t1s status / diagnostics). */
        CLI_Tasks ( );
    }

    /* Execution should not come here during normal operation */

    return ( EXIT_FAILURE );
}


/*******************************************************************************
 End of File
*/

