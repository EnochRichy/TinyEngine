/*******************************************************************************
  Application ML Header File (TinyEngine person detection)

  Replaces the TFLite-Micro gesture-classification API with a TinyEngine
  person-detection API. Legacy gesture accessors are kept as shims so that
  app_display.c keeps linking until its overlay is updated for Person/NoPerson.
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

typedef enum
{
    APP_ML_STATE_INIT = 0,
    APP_ML_STATE_SERVICE_TASKS,
} APP_ML_STATES;

typedef struct
{
    APP_ML_STATES state;
} APP_ML_DATA;

void APP_ML_Initialize(void);
void APP_ML_Tasks(void);

bool APP_ML_IsInferenceComplete(void);

/* New person-detection API */
bool  APP_ML_GetPersonPresent(void);
void  APP_ML_SetPersonPresent(bool present);
float APP_ML_GetTopBoxScore(void);

/* PC-vs-MCU correlation harness. Fills getInput() with three deterministic
 * patterns (midgray / black / white) and prints fingerprints + box counts
 * over UART. Compare against tools/smoke_test_reference.py output. Call
 * once after APP_ML_Initialize, before the camera loop runs. */
void APP_ML_RunSmokeTest(void);

/* Legacy gesture-API shims (kept so app_display.c continues to compile).
 * Mapping: person -> 0, no person -> 1. */
uint8_t APP_ML_GetIdentifiedGesture(void);
void    APP_ML_SetIdentifiedGesture(uint8_t id);

#ifdef __cplusplus
}
#endif

#endif /* _APP_ML_H */
