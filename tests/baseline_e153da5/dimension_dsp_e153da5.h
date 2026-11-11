#pragma once

#include <stdint.h>
#include "dimension_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum DimensionMode_e153da5 {
    DIMENSION_MODE_I_e153da5 = 0,
    DIMENSION_MODE_II_e153da5,
    DIMENSION_MODE_III_e153da5,
    DIMENSION_MODE_IV_e153da5,
    DIMENSION_MODE_CUSTOM_e153da5
} DimensionMode_e153da5;

typedef enum DimensionQuality_e153da5 {
    DIMENSION_QUALITY_PERCEPTUAL_e153da5 = 0,
    DIMENSION_QUALITY_ANALOG_LITE_e153da5,
    DIMENSION_QUALITY_BBD_LITE_e153da5
} DimensionQuality_e153da5;

typedef struct DimensionParams_e153da5 {
    float sampleRate;
    DimensionMode_e153da5 mode;
    DimensionQuality_e153da5 quality;
    float inputGain;
    float outputGain;
    float dryGain;
    float wetDirectGain;
    float wetCrossGain;
    float baseDelayMs;
    float depthMs;
    float rateHz;
    float hpfHz;
    float lpfHz;
    float analogAmount;
    float companderAmount;
    float noiseAmount;
    float width;
    float stereoWidth;
    float bassFocusHz;
} DimensionParams_e153da5;

typedef struct DimensionDSP_e153da5 {
    DimensionParams_e153da5 params;
    float delayL[DIMENSION_DELAY_SIZE];
    float delayR[DIMENSION_DELAY_SIZE];
    uint32_t writePos;
    float lfoPhase;
    float hpfStateL;
    float hpfStateR;
    float lpf1StateL;
    float lpf1StateR;
    float lpf2StateL;
    float lpf2StateR;
    float compEnvL;
    float compEnvR;
    float expEnvL;
    float expEnvR;
    float bbdStateL;
    float bbdStateR;
    float bassFocusState;
    float smoothRateHz;
    float smoothDepthMs;
    float smoothBaseDelayMs;
    float smoothHpfHz;
    float smoothLpfHz;
    float smoothAnalogAmount;
    float smoothWetDirectGain;
    float smoothWetCrossGain;
    float smoothDryGain;
    float smoothInputGain;
    float smoothOutputGain;
    float smoothWidth;
    float smoothStereoWidth;
    float smoothCompanderAmount;
    float smoothBassFocusHz;
    float smoothBassFocusAmount;
    float smoothCoeff;
} DimensionDSP_e153da5;

void Dimension_Init_e153da5(DimensionDSP_e153da5* d, float sampleRate);
void Dimension_Reset_e153da5(DimensionDSP_e153da5* d);
void Dimension_SetMode_e153da5(DimensionDSP_e153da5* d, DimensionMode_e153da5 mode);
void Dimension_SetParams_e153da5(DimensionDSP_e153da5* d, const DimensionParams_e153da5* p);
void Dimension_GetParams_e153da5(const DimensionDSP_e153da5* d, DimensionParams_e153da5* p);
void Dimension_GetModeParams_e153da5(DimensionMode_e153da5 mode, DimensionParams_e153da5* p);

void Dimension_ProcessBlock_e153da5(
    DimensionDSP_e153da5* d,
    const float* inL,
    const float* inR,
    float* outL,
    float* outR,
    uint32_t n
);

#ifdef __cplusplus
}
#endif
