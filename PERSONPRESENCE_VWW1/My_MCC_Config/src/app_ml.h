/*******************************************************************************
  Application ML Header File (TinyEngine MCUNet-VWW0 person presence)

  Visual Wake Words classifier API. Legacy gesture accessors are kept as
  shims so that app_display.c keeps linking until its overlay is updated for
  Person/NoPerson.
*******************************************************************************/

#ifndef _APP_ML_H
#define _APP_ML_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "configuration.h"

#ifdef __cplusplus
extern "C" {
#endif

/* When 1, APP_ML_Initialize boots into APP_ML_STATE_IMAGE_TEST instead of
 * the live-camera service-tasks loop. The model is invoked on a small set
 * of pre-baked VWW images compiled into test_images.h, looping forever so
 * the USB host can also see each input via model_input_snapshot. Use to
 * isolate model deployment correctness from camera issues. Set back to 0
 * for normal operation. */
#define ML_USE_TEST_IMAGES 0

typedef enum
{
    APP_ML_STATE_INIT = 0,
    APP_ML_STATE_SERVICE_TASKS,
    APP_ML_STATE_SMOKE_TEST,
    APP_ML_STATE_IMAGE_TEST,
} APP_ML_STATES;

typedef struct
{
    APP_ML_STATES state;
} APP_ML_DATA;

void APP_ML_Initialize(void);
void APP_ML_Tasks(void);

bool APP_ML_IsInferenceComplete(void);

/* VWW classifier API */
bool    APP_ML_GetPersonPresent(void);
void    APP_ML_SetPersonPresent(bool present);
/* Returns out[0] - out[1] (int8 logit margin). Positive = person. */
int16_t APP_ML_GetLogitMargin(void);

/* PC-vs-MCU correlation harness. Fills getInput() with three deterministic
 * patterns (midgray / black / white) and prints the 2 output logits over
 * UART. Call once after APP_ML_Initialize, before the camera loop runs. */
void APP_ML_RunSmokeTest(void);

/* Camera-bypass model verifier. Iterates through the test_images[] manifest
 * compiled into test_images.h, copies each int8 image into getInput()
 * (and mirrors uint8 into model_input_snapshot for USB visualization),
 * runs invoke(), and prints the resulting logits + expected/actual decision.
 * Loops indefinitely; gated by ML_USE_TEST_IMAGES. */
void APP_ML_RunImageTest(void);

/* Legacy gesture-API shims (kept so app_display.c continues to compile).
 * Mapping: person -> 0, no person -> 1. */
uint8_t APP_ML_GetIdentifiedGesture(void);
void    APP_ML_SetIdentifiedGesture(uint8_t id);

#ifdef __cplusplus
}
#endif

#endif /* _APP_ML_H */
