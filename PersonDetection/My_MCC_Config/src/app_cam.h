/*******************************************************************************
  MPLAB Harmony Application Header File

  Company:
    Microchip Technology Inc.

  File Name:
    app_cam.h

  Summary:
    This header file provides prototypes and definitions for the application.

  Description:
    This header file provides function prototypes and data type definitions for
    the application.  Some of these are required by the system (such as the
    "APP_CAM_Initialize" and "APP_CAM_Tasks" prototypes) and some of them are only used
    internally by the application (such as the "APP_CAM_STATES" definition).  Both
    are defined here for convenience.
*******************************************************************************/

#ifndef _APP_CAM_H
#define _APP_CAM_H

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "configuration.h"

// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility

extern "C" {

#endif
// DOM-IGNORE-END

// *****************************************************************************
// *****************************************************************************
// Section: Global Definitions and Macros
// *****************************************************************************
// *****************************************************************************

// Source frame dimensions (QQVGA RGB565)
#define IMG_WIDTH       160
#define IMG_HEIGHT      120
#define FRAME_BYTES     (IMG_WIDTH * IMG_HEIGHT * 2)

// *****************************************************************************
/* Application states

  Summary:
    Application states enumeration

  Description:
    This enumeration defines the valid application states.  These states
    determine the behavior of the application at various times.
*/

typedef enum
{
    /* Application's state machine's initial state. */
    APP_CAM_STATE_INIT=0,
    APP_CAM_STATE_SERVICE_TASKS,
    /* TODO: Define states used by the application state machine. */

} APP_CAM_STATES;


// *****************************************************************************
/* Application Data

  Summary:
    Holds application data

  Description:
    This structure holds the application's data.

  Remarks:
    Application strings and buffers are be defined outside this structure.
 */

typedef struct
{
    /* The application's current state */
    APP_CAM_STATES state;

    bool frame_ready;

    uint32_t line_index;

    bool processed_frame_data_ready;

} APP_CAM_DATA;

// *****************************************************************************
// *****************************************************************************
// Section: Application Callback Routines
// *****************************************************************************
// *****************************************************************************
/* These routines are called by drivers when certain events occur.
*/

// *****************************************************************************
// *****************************************************************************
// Section: Application Initialization and State Machine Functions
// *****************************************************************************
// *****************************************************************************

/*******************************************************************************
  Function:
    void APP_CAM_Initialize ( void )

  Summary:
     MPLAB Harmony application initialization routine.

  Description:
    This function initializes the Harmony application.  It places the
    application in its initial state and prepares it to run so that its
    APP_CAM_Tasks function can be called.

  Precondition:
    All other system initialization routines should be called before calling
    this routine (in "SYS_Initialize").

  Parameters:
    None.

  Returns:
    None.

  Example:
    <code>
    APP_CAM_Initialize();
    </code>

  Remarks:
    This routine must be called from the SYS_Initialize function.
*/

void APP_CAM_Initialize ( void );


/*******************************************************************************
  Function:
    void APP_CAM_Tasks ( void )

  Summary:
    MPLAB Harmony Demo application tasks function

  Description:
    This routine is the Harmony Demo application's tasks function.  It
    defines the application's state machine and core logic.

  Precondition:
    The system and application initialization ("SYS_Initialize") should be
    called before calling this.

  Parameters:
    None.

  Returns:
    None.

  Example:
    <code>
    APP_CAM_Tasks();
    </code>

  Remarks:
    This routine must be called from SYS_Tasks() routine.
 */

void APP_CAM_Tasks( void );

// MCUNet person-det model input dimensions (HWC int8, 128x96x3 after the
// 160x128 -> 128x96 InputResizer rescale at codegen time).
#define MODEL_IN_H  96
#define MODEL_IN_W  128
#define MODEL_IN_C  3
#define MODEL_IN_BYTES (MODEL_IN_H * MODEL_IN_W * MODEL_IN_C)

// Center-crop from 160x120 camera frame to 128x96 model input. Crop preserves
// pixel scale (objects keep their pixel size), which matters because the
// model's anchor table is unchanged and still calibrated in 160x128 scale.
#define CROP_ROW_OFFSET ((IMG_HEIGHT - MODEL_IN_H) / 2)   // (120-96)/2 = 12
#define CROP_COL_OFFSET ((IMG_WIDTH  - MODEL_IN_W) / 2)   // (160-128)/2 = 16

// Accessors
const uint8_t *APP_Cam_GetRGB565Frame(void);

/* Convert the most recent 160x120 RGB565 camera frame into a 160x128x3 HWC
 * int8 tensor for the MCUNet person-det detector.
 *
 * 160 cols pass through unchanged. 120 camera rows are letterboxed to 128
 * with 4 zero-int8 rows on top and 4 on bottom (centers the FOV vertically).
 * Per-pixel: RGB565 -> RGB888 -> (uint8 - 128) -> int8.
 *
 * If recall regresses near top/bottom borders, replace the memsets with
 * memset(_, -14, ...) to match COCO's training-time letterbox gray (uint8 114).
 *
 * @param src  Pointer to panda_scaled_data (size FRAME_BYTES).
 * @param dst  Pointer to TinyEngine getInput() buffer (size MODEL_IN_BYTES).
 */
void rgb565_to_modelinput_det(const uint8_t *src, signed char *dst);

// Expose frame buffer size for other modules
static inline uint32_t APP_Cam_GetFrameBytes(void) { return FRAME_BYTES; }


//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

#endif /* _APP_CAM_H */

/*******************************************************************************
 End of File
 */

