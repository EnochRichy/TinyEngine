/*******************************************************************************
  Application ML Source File (TinyEngine MCUNet person-det + SORT-lite tracker)

  YOLOv3-style person detector: 160x128x3 int8 input (native detector
  shape, no InputResizer rescale at codegen time), 3 anchor heads at
  strides 8/16/32, 1 class (person). Per-frame flow:

    1. Camera deposits a 160x120 RGB565 frame in panda_scaled_data[].
    2. rgb565_to_modelinput_det() letterboxes the 160x120 camera frame
       to 160x128 (4 zero rows top + 4 bottom) into TinyEngine's input
       buffer (getInput()).
    3. invoke(NULL) runs the generated graph.
    4. The codegen-emitted det_post_procesing() reads the 3 head tensors at
       their fixed buffer0 offsets and calls yoloOutput.c's postprocessing()
       to decode anchors + NMS. Output: s_ret_box[0][0..n-1] in model-pixel
       space (0..159 x 0..127). One class, so all boxes are in row 0.
    5. APP_TRK_Update() greedy-IoU-associates boxes to existing tracks for
       stable IDs across frames.
*******************************************************************************/

#include "app_ml.h"
#include "app_tracker.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "app_cam.h"
#include "peripheral/port/plib_port.h"

extern "C" {
    #include "TinyEngine/include/genNN.h"
}

/* Cortex-M7 DWT cycle counter. Used here as a dedicated timing source
 * because SYS_TIME's reported frequency (RTC_COUNTER_CLOCK_FREQUENCY)
 * disagrees with the actual hardware tick rate, which made the previous
 * s_inference_us value off by ~3x. DWT counts at exactly the CPU clock,
 * giving sub-microsecond resolution. Wraps every ~14 s at 300 MHz; the
 * 32-bit subtraction handles a single wrap correctly.
 *
 * CPU_CLOCK_HZ duplicates definitions.h's CPU_CLOCK_FREQUENCY; we don't
 * include definitions.h here because it transitively pulls in the
 * USB-HS driver headers, which contain a C-style enum cast that fails
 * to compile as C++. Keep this in sync with the MCC clock config. */
#define CPU_CLOCK_HZ         300000000u
#define DWT_DEMCR   (*(volatile uint32_t*)0xE000EDFC)
#define DWT_CTRL    (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT  (*(volatile uint32_t*)0xE0001004)
#define DEMCR_TRCENA_Msk     (1u << 24)
#define DWT_CTRL_CYCCNTENA   (1u)

static inline void dwt_enable(void)
{
    DWT_DEMCR |= DEMCR_TRCENA_Msk;
    DWT_CTRL  |= DWT_CTRL_CYCCNTENA;
}

APP_ML_DATA app_mlData;

extern APP_CAM_DATA app_camData;

/* VALID_THRESHOLD is the per-anchor confidence floor applied before NMS.
 * 0.5 is the standard starting point; lower if recall is poor, raise if
 * the dashboard shows too many false positives. NMS threshold (0.45) is
 * hard-coded inside the codegen-generated det_post_procesing wrapper. */
#define DET_VALID_THRESHOLD   0.5f

/* Must match yoloOutput.c's internal #define MAX_NUM_CLASSES 4. The header
 * does not expose it, but postprocessing() writes into all 4 slots regardless
 * of num_classes; under-sizing this array is a stack smash. */
#define DET_MAX_NUM_CLASSES   4

static volatile bool     s_inference_complete = true;
static volatile uint32_t s_inference_us = 0;
static volatile uint8_t  s_inference_count = 0;

static int      s_box_count[DET_MAX_NUM_CLASSES] = {0};
static det_box *s_ret_box[DET_MAX_NUM_CLASSES]   = {0};


/******************************************************************************
 * Inference                                                                  *
 ******************************************************************************/

static void run_person_detection(void)
{
    s_inference_complete = false;
    app_camData.processed_frame_data_ready = false;

    /* Step 1: camera frame -> TinyEngine input buffer (center-crop 160x120 -> 96x96). */
    signed char *in = getInput();
    rgb565_to_modelinput_det((const uint8_t*)APP_Cam_GetRGB565Frame(), in);

    /* Step 2: run the generated graph + decode boxes (timed end-to-end). */
    uint32_t c0 = DWT_CYCCNT;
    invoke(NULL);
    det_post_procesing(s_box_count, s_ret_box, DET_VALID_THRESHOLD);
    uint32_t c1 = DWT_CYCCNT;
    s_inference_us = (uint32_t)(((uint64_t)(c1 - c0) * 1000000ULL)
                                / (uint64_t)CPU_CLOCK_HZ);

    /* Step 3: feed boxes through the tracker. person-det is single-class, so
     * all detections live in s_ret_box[0]. */
    int n_raw = s_box_count[0];
    APP_TRK_Update(s_ret_box[0], n_raw);

    int n_tracks = 0;
    (void)APP_TRK_GetTracks(&n_tracks);
    // printf("Boxes: raw=%d tracks=%d  inf=%lu us\r\n",
    //        n_raw, n_tracks, (unsigned long)s_inference_us);

    s_inference_count = (uint8_t)(s_inference_count + 1u);

    s_inference_complete = true;
}


/******************************************************************************
 * Harmony state machine                                                      *
 ******************************************************************************/

void APP_ML_Initialize(void)
{
    app_mlData.state = APP_ML_STATE_INIT;
    /* TinyEngine has no runtime allocator; weights and buffers are static. */
    dwt_enable();
}

void APP_ML_Tasks(void)
{
    switch (app_mlData.state)
    {
        case APP_ML_STATE_INIT:
            app_mlData.state = APP_ML_STATE_SERVICE_TASKS;
            break;

        case APP_ML_STATE_SERVICE_TASKS:
        {
            /* SW0 (PB24, active-LOW with internal pull-up) resets the
             * tripwire IN/OUT counts and the live track table. Latch on
             * the first LOW sample; re-arm only after the button is
             * released (pin returns HIGH). The press-then-release gate
             * is sufficient debounce — any bounce while pressed lands on
             * the already-disarmed state and is ignored. */
            static bool sw0_armed = true;
            if (sw0_armed && SWITCH0_Get() == 0U) {
                APP_TRK_Reset();
                sw0_armed = false;
            } else if (!sw0_armed && SWITCH0_Get() != 0U) {
                sw0_armed = true;
            }

            if (app_camData.processed_frame_data_ready) {
                TEST_PIN_Set();
                run_person_detection();
                TEST_PIN_Clear();
            }
            break;
        }

        case APP_ML_STATE_SMOKE_TEST:
            APP_ML_RunSmokeTest();
            break;

        default:
            break;
    }
}


/******************************************************************************
 * Public accessors                                                           *
 ******************************************************************************/

bool APP_ML_IsInferenceComplete(void)        { return s_inference_complete; }
uint32_t APP_ML_GetInferenceUs(void)         { return s_inference_us; }
uint8_t  APP_ML_GetInferenceCount(void)      { return s_inference_count; }
int      APP_ML_GetBoxCount(void)            { return s_box_count[0]; }
const det_box *APP_ML_GetBoxes(void)         { return s_ret_box[0]; }


/******************************************************************************
 * Smoke test                                                                 *
 *                                                                            *
 * Runs invoke()+postprocessing on three deterministic input patterns and     *
 * prints the box count. Useful for sanity-checking the deployed weights.    *
 ******************************************************************************/

#define SMOKE_INPUT_LEN  (MODEL_IN_H * MODEL_IN_W * MODEL_IN_C)

static void smoke_run_pattern(const char *name, signed char fill)
{
    signed char *in = getInput();
    for (int i = 0; i < SMOKE_INPUT_LEN; ++i) {
        in[i] = fill;
    }

    invoke(NULL);
    det_post_procesing(s_box_count, s_ret_box, DET_VALID_THRESHOLD);

    printf("smoke[%s] raw_boxes=%d\r\n", name, s_box_count[0]);
}

void APP_ML_RunSmokeTest(void)
{
    s_inference_complete = false;
    printf("---- TinyEngine person-det smoke test (input %d B) ----\r\n", SMOKE_INPUT_LEN);
    smoke_run_pattern("midgray",   0);     /* uint8 128 */
    smoke_run_pattern("black",  -128);     /* uint8 0 */
    smoke_run_pattern("white",   127);     /* uint8 255 */
    printf("---- end smoke test ----\r\n");
    s_inference_complete = true;
}
