/*******************************************************************************
  MPLAB Harmony Application Source File

  Company:
    Microchip Technology Inc.

  File Name:
    app_cam.c

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

#include "app_cam.h"
#include "definitions.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// *****************************************************************************
// *****************************************************************************
// Section: Global Data Definitions
// *****************************************************************************
// *****************************************************************************

/* OV7670 I2C addresses */
#define OV7670_I2C_ADDR_WRITE       0x21    /* 0x42 >> 1 for 7-bit addressing */
#define OV7670_I2C_ADDR_READ        0x43    /* 0x86 >> 1 */

/* Camera frame layout (QQVGA RGB565: 160x120) */
#define CAM_LINE_BYTES              (IMG_WIDTH * 2)

/* Busy-wait loop iterations for timing (conservative approximation) */
#define I2C_WRITE_DELAY_LOOPS       100000

/* RGB565 camera DMA target. Defined by the MCC-generated Legato image
 * asset le_gen_images.c (38400 bytes); we just reuse the symbol so the
 * camera DMA, USB stream, and ML preprocessor all share one buffer. */
extern uint8_t panda_scaled_data[FRAME_BYTES];


// *****************************************************************************
/* Application Data

  Summary:
    Holds application data

  Description:
    This structure holds the application's data.

  Remarks:
    This structure should be initialized by the APP_CAM_Initialize function.

    Application strings and buffers are be defined outside this structure.
*/

APP_CAM_DATA app_camData;

// *****************************************************************************
// *****************************************************************************
// Section: Application Callback Functions
// *****************************************************************************
// *****************************************************************************

/**
 * DMA_EventHandler - Camera DMA transfer completion callback
 *
 * Called when DMA_CHANNEL_1 completes block transfer (full frame received).
 * Sets frame_ready flag to trigger conversion in main loop.
 *
 * Context:
 *   event - DMA event type (error, transfer complete, etc.)
 *   context - Unused context pointer
 */
void DMA_EventHandler(DMA_TRANSFER_EVENT event, uintptr_t context)
{
    if (event == DMA_TRANSFER_EVENT_BLOCK_TRANSFER_COMPLETE) {
        app_camData.frame_ready = true;
    } else if (event == DMA_TRANSFER_EVENT_ERROR) {
        /* Simple error handling: log and continue (original behavior) */
        app_camData.frame_ready = true;
    }
}

/**
 * HSYNC_ISR - Camera horizontal sync interrupt handler
 *
 * Increments line counter each time camera sends HSYNC pulse (end of line).
 * Used by VSYNC handler to determine when frame is complete.
 */
void HSYNC_ISR(void)
{
    app_camData.line_index++;
}

/**
 * VSYNC_ISR - Camera vertical sync interrupt handler
 *
 * Detects end of frame (when line_index reaches frame height minus threshold).
 * Resets DMA for next frame and clears line counter.
 */
void VSYNC_ISR(void)
{
        if (app_camData.line_index >= (IMG_HEIGHT - 5))
    {
      DMA_ChannelDisable(DMA_CHANNEL_1);
      DMA_ChannelTransfer(DMA_CHANNEL_1,
                          (const void *)((const uint8_t *)&PORT_REGS->GROUP[2].PORT_IN + 1),
                          &panda_scaled_data[0],
                                                    FRAME_BYTES);

      app_camData.line_index = 0;
      app_camData.frame_ready = false;
    }
}


// *****************************************************************************
// *****************************************************************************
// Section: Application Local Functions
// *****************************************************************************
// *****************************************************************************


/* ============================================================================
 * I2C Register Access (OV7670 Camera Control)
 * ============================================================================ */

/**
 * I2C_Write - Write OV7670 register via I2C
 *
 * Sends a single register write command to the camera over I2C bus (SERCOM5).
 * Includes busy-wait loops for timing compatibility with original code.
 *
 * Parameters:
 *   reg_addr - Register address (0x00-0xFF)
 *   reg_value - 8-bit value to write
 */
void I2C_Write(uint8_t reg_addr, uint8_t reg_value)
{
    uint8_t write_buffer[2];

    write_buffer[0] = reg_addr;
    write_buffer[1] = reg_value;

    while(SERCOM5_I2C_IsBusy());
    SERCOM5_I2C_Write(OV7670_I2C_ADDR_WRITE, write_buffer, 2);

    while(SERCOM5_I2C_IsBusy());

    /* Timing delays (3x conservative busy-wait for I2C settling) */
    for(int delay_count = 0; delay_count < I2C_WRITE_DELAY_LOOPS; delay_count++);
    for(int delay_count = 0; delay_count < I2C_WRITE_DELAY_LOOPS; delay_count++);
    for(int delay_count = 0; delay_count < I2C_WRITE_DELAY_LOOPS; delay_count++);
}

/**
 * I2C_Read - Read OV7670 register via I2C
 *
 * Sends read command to camera and returns register value.
 * Currently primarily for debug/diagnostic purposes.
 *
 * Parameters:
 *   reg_addr - Register address to read
 */
void I2C_Read(uint8_t reg_addr)
{
    uint8_t read_buffer;

    while(SERCOM5_I2C_IsBusy());
    SERCOM5_I2C_Write((OV7670_I2C_ADDR_READ >> 1), &reg_addr, 1);
    while(SERCOM5_I2C_IsBusy());
    SERCOM5_I2C_Read((OV7670_I2C_ADDR_READ >> 1), &read_buffer, 1);
    while(SERCOM5_I2C_IsBusy());
}

/* ============================================================================
 * OV7670 Camera Initialization and Configuration
 * ============================================================================ */

/**
 * ov7670_init - Initialize OV7670 for RGB565 QQVGA mode
 *
 * Configures OV7670 camera via I2C for:
 *   - RGB565 color format (16-bit output)
 *   - QQVGA resolution (160x120)
 *   - Pixel clock scaling and windowing
 *   - Color matrix and gamma settings
 *   - Auto-exposure and white balance
 *
 * Resets camera, applies register configuration, and includes settling delays.
 * Called once during application startup via APP_Cam_Initialize().
 */
void ov7670_init(void)
{
   /* Reset OV7670 via GPIO */
  RST_OV_Clear();
  for(int delay_count = 0; delay_count < I2C_WRITE_DELAY_LOOPS; delay_count++);
  for(int delay_count = 0; delay_count < I2C_WRITE_DELAY_LOOPS; delay_count++);
  for(int delay_count = 0; delay_count < I2C_WRITE_DELAY_LOOPS; delay_count++);
  RST_OV_Set();

   /* Hardware reset via I2C */
   I2C_Write(0x12, 0x80);

   I2C_Write(0x00, 0x0B);       /* AGC gain control */

  /* Color format and scaling settings */
  I2C_Write(0x11, 0x80);        /* CLKRC: Prescaler /1 */
  I2C_Write(0x12, 0x14);        /* COM7: RGB mode + QVGA scaling enabled */

   /* Auto-exposure, auto-gain, auto-white-balance */
  I2C_Write(0x13, 0x87);        /* COM8: Fast AEC/AGC | AGC | AWB | AEC.
                                 * Was 0x81 (only Fast + AEC), which left AWB
                                 * and AGC OFF — produced a fixed color cast
                                 * tied to the room illuminant and a fixed
                                 * gain (manual 0x0B at reg 0x00) that misbehaved
                                 * outside one lighting level. With AGC on, the
                                 * sensor overrides the manual gain register. */

  /* AGC ceiling and AEC target. Without these, AGC ceiling defaults high (8x)
   * and the AEC target is set bright — once the color matrix was corrected
   * to pass full red, the combined gain blew highlights out completely. */
  I2C_Write(0x14, 0x18);        /* COM9: AGC ceiling = 2x (was default ~8x).
                                 * Bits[6:4] = 001 -> 2x ceiling. */
  I2C_Write(0x24, 0x70);        /* AEW: AEC upper window  (was Linux 0x95) */
  I2C_Write(0x25, 0x60);        /* AEB: AEC lower window  (was Linux 0x33) */
  I2C_Write(0x26, 0xA5);        /* VPT: fast-mode region  (was Linux 0xE3) */

  I2C_Write(0x8C, 0x00);        /* RGB444: Disabled (use RGB565) */

  I2C_Write(0x3A, 0x08);        /* TSLB: Correct RGB byte order */
  I2C_Write(0x3D, 0x88);        /* TSLB: Set RGB order (duplicate for stability) */

  I2C_Write(0xA2, 0x02);        /* PCLK: Automatic mode */

  /* RGB565 format configuration */
  I2C_Write(0x40, 0xD0);        /* COM15: Full range output, RGB565 mode */

  /* Synchronization and signal polarity */
  I2C_Write(0x15, 0x20);        /* COM10: PCLK output only during active HREF */

  /* Downsampling and pixel clock settings */
  I2C_Write(0x0C, 0x0C);        /* COM3: Disable DCW scaling (use hardware scaler) */
  I2C_Write(0x3E, 0x12);        /* COM14: Normal PCLK, manual scaling */

  I2C_Write(0x09, 0x01);        /* Output drive capability */

  /* QQVGA (160x120) scaling configuration */
  I2C_Write(0x70, 0x3F);        /* SCALING_XSC: Horizontal scaling */
  I2C_Write(0x71, 0x35);        /* SCALING_YSC: Vertical scaling */
  I2C_Write(0x72, 0x21);        /* SCALING_DCWCTR: Enable 4x downsample */
  I2C_Write(0x73, 0xF0);        /* SCALING_PCLK_DIV: Divide pixel clock */

  /* Image windowing (centering) */
  I2C_Write(0x17, 0x16);        /* HSTART: Horizontal window start */
  I2C_Write(0x18, 0x04);        /* HSTOP: Horizontal window stop */
  I2C_Write(0x32, 0xA4);        /* HREF: Reference pixel */
  I2C_Write(0x19, 0x02);        /* VSTART: Vertical window start */
  I2C_Write(0x1A, 0x7A);        /* VSTOP: Vertical window stop */
  I2C_Write(0x03, 0x0A);        /* VREF: Vertical reference */

  /* Color matrix — full 6-coefficient set + sign register (Linux ov7670 driver
   * values). Previous init wrote only MTX1..MTX3 and skipped MTX4..MTX6 / MTXS,
   * which left the YUV->RGB basis incomplete and produced a strong cyan cast
   * (red channel suppressed). MTXS at 0x58 carries the per-coefficient signs
   * and is mandatory. */
  I2C_Write(0x4F, 0x80);        /* MTX1 */
  I2C_Write(0x50, 0x80);        /* MTX2 */
  I2C_Write(0x51, 0x00);        /* MTX3 */
  I2C_Write(0x52, 0x22);        /* MTX4 */
  I2C_Write(0x53, 0x5E);        /* MTX5 */
  I2C_Write(0x54, 0x80);        /* MTX6 */
  I2C_Write(0x58, 0x9E);        /* MTXS: matrix coefficient signs */

  /* Gamma curve (16-point, Linux driver values). Without this the sensor uses
   * a near-linear default that crushes shadows and blooms highlights — visible
   * as the over-bright wash on flat surfaces. */
  I2C_Write(0x7A, 0x20);        /* SLOP */
  I2C_Write(0x7B, 0x10);        /* GAM1 */
  I2C_Write(0x7C, 0x1E);        /* GAM2 */
  I2C_Write(0x7D, 0x35);        /* GAM3 */
  I2C_Write(0x7E, 0x5A);        /* GAM4 */
  I2C_Write(0x7F, 0x69);        /* GAM5 */
  I2C_Write(0x80, 0x76);        /* GAM6 */
  I2C_Write(0x81, 0x80);        /* GAM7 */
  I2C_Write(0x82, 0x88);        /* GAM8 */
  I2C_Write(0x83, 0x8F);        /* GAM9 */
  I2C_Write(0x84, 0x96);        /* GAM10 */
  I2C_Write(0x85, 0xA3);        /* GAM11 */
  I2C_Write(0x86, 0xAF);        /* GAM12 */
  I2C_Write(0x87, 0xC4);        /* GAM13 */
  I2C_Write(0x88, 0xD7);        /* GAM14 */
  I2C_Write(0x89, 0xE8);        /* GAM15 */

  /* AWB seed gains. Power-on defaults at regs 0x01/0x02 bias toward blue,
   * which is what AWB then "locks in" if pointed at a flat scene with no
   * neutral reference (e.g. a uniform ceiling). Seeding with neutral values
   * gives AWB a sane starting point. */
  I2C_Write(0x01, 0x40);        /* BLUE gain */
  I2C_Write(0x02, 0x60);        /* RED gain */

  /* AWB advanced configuration (Linux driver values) */
  I2C_Write(0x43, 0x0A);
  I2C_Write(0x44, 0xF0);
  I2C_Write(0x45, 0x34);
  I2C_Write(0x46, 0x58);
  I2C_Write(0x47, 0x28);
  I2C_Write(0x48, 0x3A);
  I2C_Write(0x59, 0x88);
  I2C_Write(0x5A, 0x88);
  I2C_Write(0x5B, 0x44);
  I2C_Write(0x5C, 0x67);
  I2C_Write(0x5D, 0x49);
  I2C_Write(0x5E, 0x0E);
  I2C_Write(0x6C, 0x0A);
  I2C_Write(0x6D, 0x55);
  I2C_Write(0x6E, 0x11);
  I2C_Write(0x6F, 0x9F);        /* "9e for advance AWB" per datasheet */
  I2C_Write(0x6A, 0x40);        /* GGAIN */
  I2C_Write(0x01, 0x40);        /* BLUE gain (re-seed after AWB regs) */
  I2C_Write(0x02, 0x60);        /* RED gain (re-seed) */

  /* Saturation / brightness / contrast (mid-point defaults) */
  I2C_Write(0x55, 0x00);        /* BRIGHT */
  I2C_Write(0x56, 0x40);        /* CNST */

  I2C_Write(0xB0, 0x84);        /* "Magic" undocumented register — fixes
                                 * red-channel suppression on virtually every
                                 * OV7670 module. Was 0x0F, which is the main
                                 * driver of the cyan cast we were seeing. */

  /* Wait for settling (50ms) */
  SYS_TIME_HANDLE delayHandle;
  SYS_TIME_DelayMS(50, &delayHandle);
  while(!SYS_TIME_DelayIsComplete(delayHandle));
}


/* ----------------------------------------------------------------------------
 * rgb565_to_modelinput_det - 160x120 RGB565 -> 96x96 RGB int8 (HWC), centered
 *
 * Source : panda_scaled_data, FRAME_BYTES = 160*120*2 little-endian RGB565.
 * Dest   : TinyEngine input buffer (getInput()), MODEL_IN_BYTES = 96*96*3.
 *
 * Model expects 96x96 input (graph rescaled from the original 160x128 by
 * TinyEngine InputResizer at codegen time). Camera is 160x120, so we take a
 * centered 96x96 crop -- no rescale -- preserving pixel scale so the
 * (unchanged) anchor table still matches the apparent object sizes.
 *
 * Per-pixel: RGB565 -> RGB888 (with high-bit replication for full range) ->
 * (uint8 - 128) -> int8. Codegen sets unsigned_input=False which absorbs the
 * sign convention on the input edge of the graph.
 *
 * Layout in dst: dst[((row*MODEL_IN_W) + col)*3 + {0:R,1:G,2:B}].
 * --------------------------------------------------------------------------*/
static inline void rgb565_unpack(uint16_t pixel,
                                 uint8_t *r8, uint8_t *g8, uint8_t *b8)
{
    uint8_t r5 = (pixel >> 11) & 0x1F;
    uint8_t g6 = (pixel >>  5) & 0x3F;
    uint8_t b5 =  pixel        & 0x1F;
    *r8 = (uint8_t)((r5 << 3) | (r5 >> 2));
    *g8 = (uint8_t)((g6 << 2) | (g6 >> 4));
    *b8 = (uint8_t)((b5 << 3) | (b5 >> 2));
}

void rgb565_to_modelinput_det(const uint8_t *src, signed char *dst)
{
    /* Center-crop 96x96 from the 160x120 camera frame -- no letterbox,
     * no rescale. Camera coords (CROP_ROW_OFFSET+y, CROP_COL_OFFSET+x)
     * map to model coords (y, x). */
    signed char *out = dst;
    for (int y = 0; y < MODEL_IN_H; ++y)
    {
        const uint8_t *src_row = &src[(y + CROP_ROW_OFFSET) * IMG_WIDTH * 2];
        for (int x = 0; x < MODEL_IN_W; ++x)
        {
            int cx = x + CROP_COL_OFFSET;
            uint16_t p = src_row[cx*2] | ((uint16_t)src_row[cx*2 + 1] << 8);
            uint8_t r, g, b;
            rgb565_unpack(p, &r, &g, &b);
            *out++ = (signed char)(r - 128);
            *out++ = (signed char)(g - 128);
            *out++ = (signed char)(b - 128);
        }
    }
}

/* ============================================================================
 * Public API - Data Access
 * ============================================================================ */

/**
 * APP_Cam_GetRGB565Frame - Return pointer to the raw 160x120 RGB565 frame.
 *
 * Frame contents are valid after the camera DMA / VSYNC handler has populated
 * panda_scaled_data and processed_frame_data_ready has been latched.
 */
const uint8_t *APP_Cam_GetRGB565Frame(void)
{
    return panda_scaled_data;
}

// *****************************************************************************
// *****************************************************************************
// Section: Application Initialization and State Machine Functions
// *****************************************************************************
// *****************************************************************************

/*******************************************************************************
  Function:
    void APP_CAM_Initialize ( void )

  Remarks:
    See prototype in app_cam.h.
 */

void APP_CAM_Initialize ( void )
{
    /* Place the App state machine in its initial state. */
    app_camData.state = APP_CAM_STATE_INIT;
    app_camData.frame_ready = false;
    app_camData.line_index = 0;
    app_camData.processed_frame_data_ready = false;
}


/******************************************************************************
  Function:
    void APP_CAM_Tasks ( void )

  Remarks:
    See prototype in app_cam.h.
 */

void APP_CAM_Tasks ( void )
{

    /* Check the application's current state. */
    switch ( app_camData.state )
    {
        /* Application's initial state. */
        case APP_CAM_STATE_INIT:
        {
            bool appInitialized = true;

            TCC7_PWMStart();

            TCC1_PWMStart();

            /* Configure camera registers for RGB565 QQVGA output */
            ov7670_init();

            /* Register DMA completion callback for frame capture */
            DMA_ChannelCallbackRegister(DMA_CHANNEL_1, DMA_EventHandler, 0);

            /* Register external interrupt handlers for HSYNC/VSYNC line/frame sync */
            EIC_CallbackRegister(EIC_PIN_1, (EIC_CALLBACK)VSYNC_ISR, 0);
            EIC_CallbackRegister(EIC_PIN_2, (EIC_CALLBACK)HSYNC_ISR, 0);


            if (appInitialized)
            {
                app_camData.state = APP_CAM_STATE_SERVICE_TASKS;
            }
            break;
        }

        case APP_CAM_STATE_SERVICE_TASKS:
        {

            if (app_camData.frame_ready)
            {
                app_camData.frame_ready = false;
                /* Cache-clean the DMA'd frame so the CPU (ML preprocessor and
                 * USB endpoint write) sees fresh pixels, then signal the ML
                 * task that a new frame is available. */
                DCACHE_CLEAN_BY_ADDR((uint32_t *)panda_scaled_data, APP_Cam_GetFrameBytes());
                app_camData.processed_frame_data_ready = true;
            }

            break;
        }

        /* TODO: implement your application state machine.*/


        /* The default state should never be executed. */
        default:
        {
            /* TODO: Handle error in application's state machine. */
            break;
        }
    }
}


/*******************************************************************************
 End of File
 */
