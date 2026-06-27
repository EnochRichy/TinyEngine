/*******************************************************************************
  Application ML Header File (TinyEngine MCUNet person-det + SORT-lite tracker)

  YOLOv3-style person detector API. Each frame produces up to
  MAX_BOX_PER_CLASS boxes from postprocessing; APP_TRK_Update consumes them
  and exposes a stable-ID track table via app_tracker.h.
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

#include "TinyEngine/include/yoloOutput.h"   /* det_box */

typedef enum
{
    APP_ML_STATE_INIT = 0,
    APP_ML_STATE_SERVICE_TASKS,
    APP_ML_STATE_SMOKE_TEST,
} APP_ML_STATES;

typedef struct
{
    APP_ML_STATES state;
} APP_ML_DATA;

void APP_ML_Initialize(void);
void APP_ML_Tasks(void);

bool APP_ML_IsInferenceComplete(void);

/* Raw detector outputs from the most recent invoke()+postprocessing.
 * Coordinates are in model-input pixel space (0..MODEL_IN_W x 0..MODEL_IN_H).
 * build_trailer() in app_usb.c adds the crop offsets so coords reach the
 * host already in camera-frame space. */
int             APP_ML_GetBoxCount(void);
const det_box  *APP_ML_GetBoxes(void);

/* Wall-clock duration of the most recent invoke() + postprocessing, in microseconds. */
uint32_t APP_ML_GetInferenceUs(void);

/* Monotonic count of completed inferences, wraps at 256. */
uint8_t  APP_ML_GetInferenceCount(void);

/* PC-vs-MCU correlation harness. Fills getInput() with three deterministic
 * patterns (midgray / black / white) and runs invoke() + postprocessing,
 * printing the per-class box counts over UART. */
void APP_ML_RunSmokeTest(void);

#ifdef __cplusplus
}
#endif

#endif /* _APP_ML_H */
