/*******************************************************************************
  Application ML Source File (TinyEngine person detection)

  Replaces TFLite-Micro with TinyEngine. The TinyEngine codegen lives under
  src/TinyEngine/codegen and exposes a getInput()/invoke()/det_post_procesing()
  API via genNN.h. Per-frame flow:

    1. Camera deposits a 160x120 RGB565 frame in panda_scaled_data[].
    2. rgb565_to_modelinput_128x160() converts to 128x160 HWC int8 directly
       into TinyEngine's input buffer (returned by getInput()).
    3. invoke(NULL) runs the generated graph.
    4. det_post_procesing() applies YOLO NMS over the 3 output feature maps
       and returns a list of bounding boxes; we collapse it to a single
       person/no-person flag.
*******************************************************************************/

#include "app_ml.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "app_cam.h"
#include "peripheral/port/plib_port.h"

extern "C" {
    /* genNN.h pulls in yoloOutput.h (the latter has no include guard, so do
     * NOT include it directly here -- would re-define struct box / det_box). */
    #include "TinyEngine/include/genNN.h"
}

/* Confidence threshold for a positive detection (0..1). */
#ifndef PERSON_CONF_TH
#define PERSON_CONF_TH 0.30f
#endif

/* Maximum boxes returned by post-processing (defensive cap). */
#define MAX_DET_BOXES  16

APP_ML_DATA app_mlData;

extern APP_CAM_DATA app_camData;

static volatile bool  s_inference_complete = true;
static volatile bool  s_person_present = false;
static volatile float s_top_box_score = 0.0f;

/* Legacy mapping kept for app_display.c: 0 = person, 1 = no person. */
static volatile uint8_t s_identified_gesture_local = 1;


/******************************************************************************
 * Inference                                                                  *
 ******************************************************************************/

static void run_person_detection(void)
{
    s_inference_complete = false;
    app_camData.processed_frame_data_ready = false;

    /* Step 1: write camera frame directly into TinyEngine's input buffer.
     * getInput() points at &buffer0[81920] inside the generated weights/IO
     * arena (see genModel.c). Layout: 128 rows x 160 cols x 3 channels HWC,
     * int8 = (uint8 - 128). */
    signed char* in = getInput();
    rgb565_to_modelinput_128x160((const uint8_t*)APP_Cam_GetRGB565Frame(), in);

    /* Step 2: run the generated graph. */
    TEST_PIN_Set();
    invoke(NULL);
    TEST_PIN_Clear();

    /* Step 3: YOLO post-processing across the 3 FPN output maps.
     * det_post_procesing() takes a confidence threshold and fills ret_box
     * with up to box_cnt detections. The returned pointer is owned by
     * TinyEngine internals; we only read it. */
    int box_cnt = 0;
    det_box* ret_box = NULL;
    det_post_procesing(&box_cnt, &ret_box, PERSON_CONF_TH);

    if (box_cnt > MAX_DET_BOXES) {
        box_cnt = MAX_DET_BOXES;
    }

    float top_score = 0.0f;
    for (int i = 0; i < box_cnt; ++i) {
        if (ret_box[i].score > top_score) {
            top_score = ret_box[i].score;
        }
    }

    bool present = (box_cnt > 0) && (top_score >= PERSON_CONF_TH);
    APP_ML_SetPersonPresent(present);
    s_top_box_score = top_score;

    printf("Person: %s  boxes=%d  top=%.2f\r\n",
           present ? "YES" : "no ", box_cnt, top_score);

    s_inference_complete = true;
}


/******************************************************************************
 * Harmony state machine                                                      *
 ******************************************************************************/

void APP_ML_Initialize(void)
{
    app_mlData.state = APP_ML_STATE_INIT;
    /* TinyEngine has no runtime allocator; weights and buffers are static. */
}

void APP_ML_Tasks(void)
{
    switch (app_mlData.state)
    {
        case APP_ML_STATE_INIT:
            app_mlData.state = APP_ML_STATE_SERVICE_TASKS;
            break;

        case APP_ML_STATE_SERVICE_TASKS:
            if (app_camData.processed_frame_data_ready) {
                run_person_detection();
            }
            break;

        default:
            break;
    }
}


/******************************************************************************
 * Public accessors                                                           *
 ******************************************************************************/

bool APP_ML_IsInferenceComplete(void)
{
    return s_inference_complete;
}

bool APP_ML_GetPersonPresent(void)
{
    return s_person_present;
}

void APP_ML_SetPersonPresent(bool present)
{
    s_person_present = present;
    /* Mirror to the legacy gesture id so app_display.c keeps rendering
     * something sensible until the overlay is updated. */
    s_identified_gesture_local = present ? 0u : 1u;
}

float APP_ML_GetTopBoxScore(void)
{
    return s_top_box_score;
}

uint8_t APP_ML_GetIdentifiedGesture(void)
{
    return (uint8_t)s_identified_gesture_local;
}

void APP_ML_SetIdentifiedGesture(uint8_t id)
{
    s_identified_gesture_local = id;
}


/******************************************************************************
 * Smoke test                                                                 *
 *                                                                            *
 * Runs invoke() on three deterministic input patterns and prints the         *
 * resulting fingerprint over UART. The PC-side reference                     *
 * (tools/smoke_test_reference.py) prints the same patterns through the       *
 * float32 detection.tflite.                                                  *
 *                                                                            *
 * What "matches" means here: out[0] is the same 4x5x18 feature map in        *
 * both. After dequantizing the int8 C output via                             *
 *   real = (q - y_zero[0]) * y_scale[0]    (y_zero=43, y_scale=0.1109)       *
 * the min/max/mean should be in the same ballpark as Python's out[0].       *
 * Bit-exact equality is NOT expected (TinyEngine quantizes, tflite is        *
 * float32) but order-of-magnitude and sign patterns must agree.              *
 ******************************************************************************/

/* Quant params for output[0] read from genModel.c det_post_procesing. */
#define SMOKE_Y_ZERO_0   43
#define SMOKE_Y_SCALE_0  0.11086384952068329f
#define SMOKE_OUT0_LEN   (4 * 5 * 18)   /* 360 elements */
#define SMOKE_INPUT_LEN  (MODEL_IN_H * MODEL_IN_W * MODEL_IN_C)

static void smoke_run_pattern(const char *name, signed char fill)
{
    signed char *in = getInput();
    for (int i = 0; i < SMOKE_INPUT_LEN; ++i) {
        in[i] = fill;
    }

    invoke(NULL);

    /* Fingerprint over the smallest output feature map (the one returned by
     * getOutput()). int8 -> float dequantization for direct comparison. */
    const signed char *out = getOutput();
    int q_min = 127, q_max = -128;
    long q_sum = 0;
    long q_abs_sum = 0;
    for (int i = 0; i < SMOKE_OUT0_LEN; ++i) {
        int v = out[i];
        if (v < q_min) q_min = v;
        if (v > q_max) q_max = v;
        q_sum += v;
        q_abs_sum += (v < 0) ? -v : v;
    }
    float min_f  = ((float)q_min - (float)SMOKE_Y_ZERO_0) * SMOKE_Y_SCALE_0;
    float max_f  = ((float)q_max - (float)SMOKE_Y_ZERO_0) * SMOKE_Y_SCALE_0;
    float mean_f = (((float)q_sum / (float)SMOKE_OUT0_LEN)
                    - (float)SMOKE_Y_ZERO_0) * SMOKE_Y_SCALE_0;
    /* abs_sum after dequant: sum_i |q_i - z| * scale -- approximate via
     * |q_i| - z for small zero-points; we just print the int sum so the user
     * can see the raw scale. */

    /* Box count at a very low threshold so the count is sensitive to any
     * spurious activation. For a uniform input we expect 0. */
    int box_cnt = 0;
    det_box *ret_box = NULL;
    det_post_procesing(&box_cnt, &ret_box, 0.001f);

    printf("smoke[%s] q_min=%d q_max=%d q_sum=%ld q_abs=%ld   "
           "deq_min=%+.4f deq_max=%+.4f deq_mean=%+.4f   boxes@0.001=%d\r\n",
           name, q_min, q_max, q_sum, q_abs_sum,
           (double)min_f, (double)max_f, (double)mean_f, box_cnt);
}

void APP_ML_RunSmokeTest(void)
{
    s_inference_complete = false;
    printf("---- TinyEngine smoke test (input %d B) ----\r\n", SMOKE_INPUT_LEN);
    smoke_run_pattern("midgray",   0);     /* uint8 128 */
    smoke_run_pattern("black",  -128);     /* uint8 0 */
    smoke_run_pattern("white",   127);     /* uint8 255 */
    printf("---- end smoke test ----\r\n");
    s_inference_complete = true;
}
