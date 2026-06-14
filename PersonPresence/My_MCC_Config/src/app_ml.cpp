/*******************************************************************************
  Application ML Source File (TinyEngine MCUNet-VWW0 person presence)

  Visual Wake Words classifier: 64x64x3 int8 input, 2 int8 logits output.
  Output index mapping: out[0] = no-person, out[1] = person, matching
  torchvision ImageFolder's alphabetical class assignment ("non-person" <
  "person") used by mcunet/eval_tflite.py. Verified empirically on the
  VWW1 sibling project; ported here on the same template assumption and
  re-verifiable via the embedded image-test harness (ML_USE_TEST_IMAGES).
  Per-frame flow:

    1. Camera deposits a 160x120 RGB565 frame in panda_scaled_data[].
    2. rgb565_to_modelinput_vww() center-crops + downsamples to 64x64 HWC int8
       directly into TinyEngine's input buffer (returned by getInput()).
    3. invoke(NULL) runs the generated graph.
    4. Argmax on the 2 output logits decides person / no-person.
*******************************************************************************/

#include "app_ml.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "app_cam.h"
#include "peripheral/port/plib_port.h"

extern "C" {
    /* genNN.h declares getInput/getOutput/invoke. It also declares legacy
     * det_post_procesing/det_box symbols inherited from the detector codegen;
     * we never call them, so the linker won't pull anything in. */
    #include "TinyEngine/include/genNN.h"
}

#if ML_USE_TEST_IMAGES
    #include "test_images.h"
#endif

APP_ML_DATA app_mlData;

extern APP_CAM_DATA app_camData;

static volatile bool    s_inference_complete = true;
static volatile bool    s_person_present = false;
static volatile int16_t s_logit_margin = 0;   /* out[0] - out[1], int8 range */

/* Legacy mapping kept for app_display.c: 0 = person, 1 = no person. */
static volatile uint8_t s_identified_gesture_local = 1;


/******************************************************************************
 * Inference                                                                  *
 ******************************************************************************/

static void run_person_classification(void)
{
    TEST_PIN_Set();
    s_inference_complete = false;
    app_camData.processed_frame_data_ready = false;

    /* Step 1: camera frame -> TinyEngine input buffer.
     * getInput() points at &buffer0[16384] inside the generated arena (see
     * codegen/Source/genModel.c). Layout: 64 rows x 64 cols x 3 channels HWC,
     * int8 = (uint8 - 128). */
    signed char* in = getInput();
    rgb565_to_modelinput_vww((const uint8_t*)APP_Cam_GetRGB565Frame(), in);

    /* Diagnostic: per-channel range/mean of the int8 input every 32 frames.
     * Discriminates camera/preprocess pathologies (AGC clamp, BGR-vs-RGB,
     * grayscale collapse) from model-side issues. Remove once root-caused. */
    {
        static int dbg_count = 0;
        if ((dbg_count++ & 0x1F) == 0) {
            int rmin=127, rmax=-128, gmin=127, gmax=-128, bmin=127, bmax=-128;
            long rsum=0, gsum=0, bsum=0;
            const int N = MODEL_IN_H * MODEL_IN_W;
            for (int i = 0; i < N; ++i) {
                int r = in[i*3+0], g = in[i*3+1], b = in[i*3+2];
                if (r<rmin) rmin=r; if (r>rmax) rmax=r; rsum+=r;
                if (g<gmin) gmin=g; if (g>gmax) gmax=g; gsum+=g;
                if (b<bmin) bmin=b; if (b>bmax) bmax=b; bsum+=b;
            }
            printf("in stats: R[%d..%d mean=%ld] G[%d..%d mean=%ld] B[%d..%d mean=%ld]  px[0]=(%d,%d,%d)\r\n",
                   rmin,rmax,rsum/N, gmin,gmax,gsum/N, bmin,bmax,bsum/N,
                   in[0],in[1],in[2]);
        }
    }

    /* Step 2: run the generated graph. */

    invoke(NULL);
    

    /* Step 3: argmax on the 2 int8 logits (out[0]=no-person, out[1]=person). */
    const signed char* out = getOutput();
    int8_t noperson_logit  = out[0];
    int8_t person_logit    = out[1];
    bool present = (person_logit > noperson_logit);

    APP_ML_SetPersonPresent(present);
    s_logit_margin = (int16_t)((int)person_logit - (int)noperson_logit);

    printf("Person: %s  logits=[%d, %d]  margin=%d\r\n",
           present ? "YES" : "no ",
           (int)person_logit, (int)noperson_logit, (int)s_logit_margin);

    s_inference_complete = true;
    TEST_PIN_Clear();
}


/******************************************************************************
 * Harmony state machine                                                      *
 ******************************************************************************/

void APP_ML_Initialize(void)
{
#if ML_USE_TEST_IMAGES
    /* Camera-bypass model verifier — see APP_ML_RunImageTest. */
    app_mlData.state = APP_ML_STATE_IMAGE_TEST;
#else
    app_mlData.state = APP_ML_STATE_INIT;
#endif
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
                run_person_classification();
            }
            break;

        case APP_ML_STATE_SMOKE_TEST:
             APP_ML_RunSmokeTest();
             break;

        case APP_ML_STATE_IMAGE_TEST:
             APP_ML_RunImageTest();
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

int16_t APP_ML_GetLogitMargin(void)
{
    return s_logit_margin;
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
 * resulting 2 int8 logits. Useful for sanity-checking the deployed weights   *
 * vs. a PC-side TFLite reference run on the same patterns.                   *
 ******************************************************************************/

#define SMOKE_INPUT_LEN  (MODEL_IN_H * MODEL_IN_W * MODEL_IN_C)

static void smoke_run_pattern(const char *name, signed char fill)
{
    signed char *in = getInput();
    for (int i = 0; i < SMOKE_INPUT_LEN; ++i) {
        in[i] = fill;
    }

    invoke(NULL);

    const signed char *out = getOutput();
    int np = out[0], p = out[1];
    printf("smoke[%s] logits=[np=%d, p=%d]  decision=%s\r\n",
           name, np, p, (p > np) ? "person" : "no-person");
}

void APP_ML_RunSmokeTest(void)
{
    s_inference_complete = false;
    printf("---- TinyEngine VWW smoke test (input %d B) ----\r\n", SMOKE_INPUT_LEN);
    smoke_run_pattern("midgray",   0);     /* uint8 128 */
    smoke_run_pattern("black",  -128);     /* uint8 0 */
    smoke_run_pattern("white",   127);     /* uint8 255 */
    printf("---- end smoke test ----\r\n");
    s_inference_complete = true;
}


/******************************************************************************
 * Image test                                                                 *
 *                                                                            *
 * Bypasses the camera entirely: copies pre-baked VWW images (compiled into   *
 * test_images.h by ML Model/ConvertImagesToHex.py) into getInput(), runs     *
 * invoke(), and prints expected vs actual decision. Used to confirm the      *
 * deployed model itself classifies correctly when given clean ground-truth   *
 * input — separating model-deployment bugs from camera issues.               *
 ******************************************************************************/

#if ML_USE_TEST_IMAGES

/* Coarse busy-wait between images so a UART log reader has time to scroll
 * through each line. Not real-time-critical — this is a debug path. */
#define IMAGE_TEST_HOLD_LOOPS  20000000UL

static void image_test_hold(void)
{
    for (volatile uint32_t i = 0; i < IMAGE_TEST_HOLD_LOOPS; ++i) {
        __asm("nop");
    }
}

void APP_ML_RunImageTest(void)
{
    TEST_PIN_Set();
    s_inference_complete = false;
    printf("---- TinyEngine VWW image test (%d images) ----\r\n",
           test_images_count);

    for (int idx = 0; idx < test_images_count; ++idx) {
        const test_image_t *t = &test_images[idx];

        /* Fill the model input directly. test_images[].data is already
         * in int8 [-128,127] form — same domain as getInput() expects. */
        signed char *in = getInput();
        memcpy(in, t->data, (size_t)(MODEL_IN_H * MODEL_IN_W * MODEL_IN_C));

        invoke(NULL);

        const signed char *out = getOutput();
        int np = out[0];
        int p  = out[1];
        int actual = (p > np) ? 0 : 1;
        int margin = p - np;
        const char *exp_str = (t->expected_label == 0) ? "person" : "no-person";
        const char *act_str = (actual == 0) ? "person" : "no-person";
        const char *verdict = (actual == t->expected_label) ? "PASS" : "FAIL";

        printf("img[%-20s] logits=[np=%d, p=%d] margin=%d expected=%s decision=%s %s\r\n",
               t->name, np, p, margin, exp_str, act_str, verdict);

        TEST_PIN_Clear();

       // image_test_hold();
    }

    printf("---- end image test (looping) ----\r\n");
    s_inference_complete = true;
}

#else /* !ML_USE_TEST_IMAGES */

void APP_ML_RunImageTest(void)
{
    /* No-op when test images are not compiled in. Kept defined so the
     * symbol resolves regardless of the build flag. */
}

#endif /* ML_USE_TEST_IMAGES */
