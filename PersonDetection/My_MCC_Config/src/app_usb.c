/*******************************************************************************
  MPLAB Harmony Application Source File

  Company:
    Microchip Technology Inc.

  File Name:
    app_usb.c

  Summary:
    This file contains the source code for the MPLAB Harmony application.

  Description:
    This file contains the source code for the MPLAB Harmony application.  It
    implements the logic of the application's state machine and it may call
    API routines of other MPLAB Harmony modules in the system, such as drivers,
    system services, and middleware.  However, it does not call any of the
    system interfaces (such as the "Initialize" and "Tasks" functions) of any of
    the modules in the system or make any assumptions about when those functions
    are called.  That is the responsibility of the configuration-specific system
    files.
 *******************************************************************************/

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include "app_usb.h"
#include "app_cam.h"
#include "app_ml.h"
#include "app_tracker.h"
#include <string.h>

// *****************************************************************************
// *****************************************************************************
// Section: Global Data Definitions
// *****************************************************************************
// *****************************************************************************
#define APP_EP_BULK_OUT 1
#define APP_EP_BULK_IN 1
// *****************************************************************************
/* Application Data

  Summary:
    Holds application data

  Description:
    This structure holds the application's data.

  Remarks:
    This structure should be initialized by the APP_USB_Initialize function.

    Application strings and buffers are be defined outside this structure.
*/

APP_USB_DATA app_usbData;

/* Receive data buffer */
uint8_t receivedDataBuffer[512] CACHE_ALIGN;

/* Transmit data buffer */
uint8_t  transmitDataBuffer[512] CACHE_ALIGN;

extern APP_CAM_DATA app_camData;

/* Camera RGB565 frame is provided by the camera module via
 * APP_Cam_GetRGB565Frame() (160x120 little-endian RGB565, 38400 bytes). */

#define FRAME_WIDTH          IMG_WIDTH                                  // 160
#define FRAME_HEIGHT         IMG_HEIGHT                                 // 120
#define FRAME_BPP            2                                          // RGB565
#define FRAME_PAYLOAD_SIZE   FRAME_BYTES                                // 38400 (RGB565)
/* Header layout (little-endian) -- bytes 8..11 repurposed for detection mode:
 *   [0..3]   marker AA 55 AA 55
 *   [4..7]   frame counter (u32)
 *   [8]      num_tracks  (u8, 0..TRK_TRAILER_SLOTS) -- active tracks reported
 *   [9]      num_raw_boxes (u8) -- pre-tracker box count (diagnostic)
 *   [10]     reserved (0)
 *   [11]     inference counter (u8, wraps at 256) -- host uses increments
 *            to compute inference FPS
 *   [12..15] last inference duration in microseconds (u32) */
#define FRAME_HEADER_SIZE    16
#define FRAME_PACKET_SIZE    512
#define FRAME_NUM_PACKETS    (FRAME_PAYLOAD_SIZE / FRAME_PACKET_SIZE)   // 75

/* Boxes trailer: 8 fixed slots * 12 bytes = 96 B, appended after the RGB565
 * payload as a single short final packet. Slot record (little-endian):
 *   +0  u8   id          (0 = empty slot; track id mod 256 otherwise)
 *   +1  u8   score_q8    (round(box.score * 255), 0..255)
 *   +2  i16  x0          camera-frame pixel space (0..159 x 0..119);
 *   +4  i16  y0            build_trailer() adds CROP_*_OFFSET to the model
 *   +6  i16  x1            96x96 coords so the host renders directly.
 *   +8  i16  y1
 *   +10 u8   miss_count
 *   +11 u8   reserved
 */
#define TRK_TRAILER_SLOTS    8
#define TRK_TRAILER_RECSIZE  12
#define TRK_TRAILER_SIZE     (TRK_TRAILER_SLOTS * TRK_TRAILER_RECSIZE)  // 96

static uint8_t  frameHeader[FRAME_HEADER_SIZE];
static uint8_t  frameTrailer[TRK_TRAILER_SIZE];
static uint32_t frameCounter = 0;
static uint32_t txPacketIndex = 0;
static bool     streamingInProgress = false;

static void build_trailer(uint8_t *out)
{
    memset(out, 0, TRK_TRAILER_SIZE);
    int active = 0;
    const track_t *tracks = APP_TRK_GetTracks(&active);
    int j = 0;
    for (int t = 0; t < 8 /* TRK_MAX_TRACKS */ && j < TRK_TRAILER_SLOTS; ++t) {
        if (!tracks[t].active) continue;
        uint8_t *r = &out[j * TRK_TRAILER_RECSIZE];

        r[0] = (uint8_t)(tracks[t].id & 0xFF);

        int sq = (int)(tracks[t].box.score * 255.0f + 0.5f);
        if (sq < 0)   sq = 0;
        if (sq > 255) sq = 255;
        r[1] = (uint8_t)sq;

        /* Transform model-space (160x128 letterboxed) box to camera-space
         * (160x120). 160 cols pass through unchanged; rows are offset by
         * -LETTERBOX_ROW_PAD to undo the top/bottom zero padding. Host then
         * renders boxes directly on the camera frame with no further math. */
        int16_t x0 = (int16_t)((int)tracks[t].box.x0);
        int16_t y0 = (int16_t)((int)tracks[t].box.y0 - (int)LETTERBOX_ROW_PAD);
        int16_t x1 = (int16_t)((int)tracks[t].box.x1);
        int16_t y1 = (int16_t)((int)tracks[t].box.y1 - (int)LETTERBOX_ROW_PAD);
        r[2] = (uint8_t)(x0 & 0xFF); r[3] = (uint8_t)((x0 >> 8) & 0xFF);
        r[4] = (uint8_t)(y0 & 0xFF); r[5] = (uint8_t)((y0 >> 8) & 0xFF);
        r[6] = (uint8_t)(x1 & 0xFF); r[7] = (uint8_t)((x1 >> 8) & 0xFF);
        r[8] = (uint8_t)(y1 & 0xFF); r[9] = (uint8_t)((y1 >> 8) & 0xFF);

        r[10] = tracks[t].miss_count;
        r[11] = 0;
        ++j;
    }
}


// *****************************************************************************
// *****************************************************************************
// Section: Application Callback Functions
// *****************************************************************************
// *****************************************************************************

/* TODO:  Add any necessary callback functions.
*/

void APP_USBDeviceEventHandler(USB_DEVICE_EVENT event, void * eventData, uintptr_t context)
{
    uint8_t * configurationValue;
    USB_SETUP_PACKET * setupPacket;
    switch(event)
    {
        case USB_DEVICE_EVENT_RESET:
        case USB_DEVICE_EVENT_DECONFIGURED:

            /* Device is reset or deconfigured. Provide LED indication.*/

            app_usbData.deviceIsConfigured = false;

            break;

        case USB_DEVICE_EVENT_CONFIGURED:

            /* Check the configuration */
            configurationValue = (uint8_t *)eventData;
            if(*configurationValue == 1 )
            {
                /* Reset endpoint data send & receive flag  */
                app_usbData.deviceIsConfigured = true;
            }
            break;

        case USB_DEVICE_EVENT_SUSPENDED:

            /* Device is suspended. */ 
            break;


        case USB_DEVICE_EVENT_POWER_DETECTED:

            /* VBUS is detected. Attach the device */
            USB_DEVICE_Attach(app_usbData.usbDevHandle);
            break;

        case USB_DEVICE_EVENT_POWER_REMOVED:

            /* VBUS is removed. Detach the device */
            USB_DEVICE_Detach (app_usbData.usbDevHandle);
            break;

        case USB_DEVICE_EVENT_CONTROL_TRANSFER_SETUP_REQUEST:
            /* This means we have received a setup packet */
            setupPacket = (USB_SETUP_PACKET *)eventData;
            if(setupPacket->bRequest == USB_REQUEST_SET_INTERFACE)
            {
                /* If we have got the SET_INTERFACE request, we just acknowledge
                 for now. This demo has only one alternate setting which is already
                 active. */
                USB_DEVICE_ControlStatus(app_usbData.usbDevHandle,USB_DEVICE_CONTROL_STATUS_OK);
            }
            else if(setupPacket->bRequest == USB_REQUEST_GET_INTERFACE)
            {
                /* We have only one alternate setting and this setting 0. So
                 * we send this information to the host. */

                USB_DEVICE_ControlSend(app_usbData.usbDevHandle, &app_usbData.altSetting, 1);
            }
            else
            {
                /* We have received a request that we cannot handle. Stall it*/
                USB_DEVICE_ControlStatus(app_usbData.usbDevHandle, USB_DEVICE_CONTROL_STATUS_ERROR);
            }
            break;

        case USB_DEVICE_EVENT_ENDPOINT_READ_COMPLETE:
           /* Endpoint read is complete */
            app_usbData.epDataReadPending = false;
            break;

        case USB_DEVICE_EVENT_ENDPOINT_WRITE_COMPLETE:
            /* Endpoint write is complete */
            app_usbData.epDataWritePending = false;
            if (txPacketIndex < FRAME_NUM_PACKETS)
            {
                /* All RGB565 payload packets carry MORE_DATA_PENDING; the
                 * trailer that follows is the one flagged DATA_COMPLETE. */
                USB_DEVICE_EndpointWrite(
                    app_usbData.usbDevHandle,
                    &app_usbData.writeTranferHandle,
                    app_usbData.endpointTx,
                    (void *)(APP_Cam_GetRGB565Frame() + (txPacketIndex * FRAME_PACKET_SIZE)),
                    FRAME_PACKET_SIZE,
                    USB_DEVICE_TRANSFER_FLAGS_MORE_DATA_PENDING
                );

                txPacketIndex++;
            }
            else if (txPacketIndex == FRAME_NUM_PACKETS)
            {
                /* Short trailing packet: 96 B of tracker output, terminates the
                 * USB frame with DATA_COMPLETE so the host parses one atomic
                 * payload of 16 (header) + 38400 (RGB565) + 96 (trailer). */
                USB_DEVICE_EndpointWrite(
                    app_usbData.usbDevHandle,
                    &app_usbData.writeTranferHandle,
                    app_usbData.endpointTx,
                    frameTrailer,
                    TRK_TRAILER_SIZE,
                    USB_DEVICE_TRANSFER_FLAGS_DATA_COMPLETE
                );

                txPacketIndex++;
            }
            else
            {
                // Finished sending this frame
                streamingInProgress        = false;
                frameCounter++;          // for debug / PC sync
            }

            break;

        /* These events are not used in this demo. */
        case USB_DEVICE_EVENT_RESUMED:
            if(app_usbData.deviceIsConfigured == true)
            {
            }
            break;
        case USB_DEVICE_EVENT_ERROR:
        default:
            break;
    }
}


// *****************************************************************************
// *****************************************************************************
// Section: Application Local Functions
// *****************************************************************************
// *****************************************************************************


/* TODO:  Add any necessary local functions.
*/


// *****************************************************************************
// *****************************************************************************
// Section: Application Initialization and State Machine Functions
// *****************************************************************************
// *****************************************************************************

/*******************************************************************************
  Function:
    void APP_USB_Initialize ( void )

  Remarks:
    See prototype in app_usb.h.
 */

void APP_USB_Initialize ( void )
{
    /* Place the App state machine in its initial state. */
    app_usbData.state = APP_USB_STATE_INIT;
    app_usbData.usbDevHandle = USB_DEVICE_HANDLE_INVALID;
    app_usbData.deviceIsConfigured = false;
    app_usbData.endpointRx = (APP_EP_BULK_OUT | USB_EP_DIRECTION_OUT);
    app_usbData.endpointTx = (APP_EP_BULK_IN | USB_EP_DIRECTION_IN);
    app_usbData.epDataReadPending = false;
    app_usbData.epDataWritePending = false;
    app_usbData.altSetting = 0;
    /* TODO: Initialize your application's state machine and other
     * parameters.
     */
}


/******************************************************************************
  Function:
    void APP_USB_Tasks ( void )

  Remarks:
    See prototype in app_usb.h.
 */

void APP_USB_Tasks (void )
{
    switch(app_usbData.state)
    {
        case APP_USB_STATE_INIT:
            /* Open the device layer */
            app_usbData.usbDevHandle = USB_DEVICE_Open( USB_DEVICE_INDEX_0,
                    DRV_IO_INTENT_READWRITE );

            if(app_usbData.usbDevHandle != USB_DEVICE_HANDLE_INVALID)
            {
                /* Register a callback with device layer to get event notification (for end point 0) */
                USB_DEVICE_EventHandlerSet(app_usbData.usbDevHandle,  APP_USBDeviceEventHandler, 0);

                app_usbData.state = APP_USB_STATE_WAIT_FOR_CONFIGURATION;
            }
            else
            {
                /* The Device Layer is not ready to be opened. We should try
                 * again later. */
            }

            break;

        case APP_USB_STATE_WAIT_FOR_CONFIGURATION:

            /* Check if the device is configured */
            if(app_usbData.deviceIsConfigured == true)
            {
                if (USB_DEVICE_ActiveSpeedGet(app_usbData.usbDevHandle) == USB_SPEED_FULL)
                {
                    app_usbData.endpointMaxPktSize = 64;
                }
                else if (USB_DEVICE_ActiveSpeedGet(app_usbData.usbDevHandle) == USB_SPEED_HIGH)
                {
                    app_usbData.endpointMaxPktSize = 512;
                }
                if (USB_DEVICE_EndpointIsEnabled(app_usbData.usbDevHandle, app_usbData.endpointRx) == false )
                {
                    /* Enable Read Endpoint */
                    USB_DEVICE_EndpointEnable(app_usbData.usbDevHandle, 0, app_usbData.endpointRx,
                            USB_TRANSFER_TYPE_BULK, app_usbData.endpointMaxPktSize);
                }
                if (USB_DEVICE_EndpointIsEnabled(app_usbData.usbDevHandle, app_usbData.endpointTx) == false )
                {
                    /* Enable Write Endpoint */
                    USB_DEVICE_EndpointEnable(app_usbData.usbDevHandle, 0, app_usbData.endpointTx,
                            USB_TRANSFER_TYPE_BULK, app_usbData.endpointMaxPktSize);
                }
                /* Indicate that we are waiting for read */
                app_usbData.epDataReadPending = true;

                /* Place a new read request. */
                USB_DEVICE_EndpointRead(app_usbData.usbDevHandle, &app_usbData.readTranferHandle,
                        app_usbData.endpointRx, &receivedDataBuffer[0], sizeof(receivedDataBuffer) );

                /* Device is ready to run the main task */
                app_usbData.state = APP_USB_STATE_MAIN_TASK;
            }
            break;

        case APP_USB_STATE_MAIN_TASK:

            if(!app_usbData.deviceIsConfigured)
            {
                /* This means the device got deconfigured. Change the
                 * application state back to waiting for configuration. */
                app_usbData.state = APP_USB_STATE_WAIT_FOR_CONFIGURATION;

                /* Disable the endpoint*/
                USB_DEVICE_EndpointDisable(app_usbData.usbDevHandle, app_usbData.endpointRx);
                USB_DEVICE_EndpointDisable(app_usbData.usbDevHandle, app_usbData.endpointTx);
                app_usbData.epDataReadPending = false;
                app_usbData.epDataWritePending = false;
            }

            else if(app_usbData.epDataWritePending == false)
            {


              if (app_camData.usb_frame_ready) {
                /* Consume the USB-side latch. Set by DMA_EventHandler on
                 * every frame completion; independent from the ML pipeline's
                 * frame_ready so APP_CAM_Tasks cannot starve us. */
                app_camData.usb_frame_ready = false;

                // Build header: marker + frame counter + ML decision + timing
                frameHeader[0] = 0xAA;
                frameHeader[1] = 0x55;
                frameHeader[2] = 0xAA;
                frameHeader[3] = 0x55;

                uint32_t cnt = frameCounter;
                frameHeader[4] = (uint8_t)(cnt & 0xFF);
                frameHeader[5] = (uint8_t)((cnt >> 8) & 0xFF);
                frameHeader[6] = (uint8_t)((cnt >> 16) & 0xFF);
                frameHeader[7] = (uint8_t)((cnt >> 24) & 0xFF);

                /* Latch the most recent ML state alongside the frame so the
                 * host sees decision + frame as one atomic record. The boxes
                 * lag the displayed pixels by exactly one inference cycle. */
                int trk_active = 0;
                (void)APP_TRK_GetTracks(&trk_active);
                int raw_boxes = APP_ML_GetBoxCount();
                if (raw_boxes > 255) raw_boxes = 255;
                frameHeader[8]  = (uint8_t)trk_active;
                frameHeader[9]  = (uint8_t)raw_boxes;
                frameHeader[10] = 0u;
                frameHeader[11] = APP_ML_GetInferenceCount();

                /* Snapshot the tracker into the trailer at the same instant we
                 * latch the header so the host gets a consistent (frame, boxes)
                 * tuple for this USB frame. */
                build_trailer(frameTrailer);

                uint32_t inf_us = APP_ML_GetInferenceUs();
                frameHeader[12] = (uint8_t)(inf_us & 0xFF);
                frameHeader[13] = (uint8_t)((inf_us >>  8) & 0xFF);
                frameHeader[14] = (uint8_t)((inf_us >> 16) & 0xFF);
                frameHeader[15] = (uint8_t)((inf_us >> 24) & 0xFF);

                streamingInProgress        = true;
                txPacketIndex              = 0;            // next: payload packet 0
                app_usbData.epDataWritePending = true;

                USB_DEVICE_EndpointWrite ( app_usbData.usbDevHandle, &app_usbData.writeTranferHandle,
                        app_usbData.endpointTx, frameHeader,
                        FRAME_HEADER_SIZE,
                        USB_DEVICE_TRANSFER_FLAGS_MORE_DATA_PENDING);
                }
            }
            break;

        case APP_USB_STATE_ERROR:
            break;

        default:
            break;
    }
}


/*******************************************************************************
 End of File
 */
