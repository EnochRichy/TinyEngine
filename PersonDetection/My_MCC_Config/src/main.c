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
#include <stdint.h>
#include <string.h>                     // memset for DTCM zero-fill
#include "definitions.h"                // SYS function prototypes
#include "app_cam.h"

/* Linker-provided bounds of the DTCM (NOLOAD) section. Startup's BSS clear
 * doesn't touch it, so we zero it here to honor static-storage init. */
extern uint8_t __dtcm_start__[];
extern uint8_t __dtcm_end__[];


// *****************************************************************************
// *****************************************************************************
// Section: Main Entry Point
// *****************************************************************************
// *****************************************************************************

int main ( void )
{
    /* Initialize all modules */
    SYS_Initialize ( NULL );

    /* Zero the DTCM region (linker marks it NOLOAD, so startup didn't). */
    memset(__dtcm_start__, 0, (size_t)(__dtcm_end__ - __dtcm_start__));

    printf("---- TinyEngine ");

    while ( true )
    {
        /* Maintain state machines of all polled MPLAB Harmony modules. */
        SYS_Tasks ( );

        /* Call Application task APP_CAM. */
        APP_CAM_Tasks();
    }

    /* Execution should not come here during normal operation */

    return ( EXIT_FAILURE );
}


/*******************************************************************************
 End of File
*/

