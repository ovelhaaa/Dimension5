#pragma once

#include <stdint.h>
#include "dimension_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum DimensionMode {
    DIMENSION_MODE_I = 0,
    DIMENSION_MODE_II,
    DIMENSION_MODE_III,
    DIMENSION_MODE_IV,
    DIMENSION_MODE_CUSTOM
} DimensionMode;

typedef enum DimensionQuality {
    DIMENSION_QUALITY_PERCEPTUAL = 0,
    DIMENSION_QUALITY_ANALOG_LITE,
    DIMENSION_QUALITY_BBD_LITE
} DimensionQuality;

typedef struct DimensionParams {
    float sampleRate;
    DimensionMode mode;
    DimensionQuality quality;
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
} DimensionParams;

typedef struct DimensionDSP {
    DimensionParams params;
} DimensionDSP;

void Dimension_Init(DimensionDSP* d, float sampleRate);
void Dimension_Reset(DimensionDSP* d);
void Dimension_SetMode(DimensionDSP* d, DimensionMode mode);
void Dimension_SetParams(DimensionDSP* d, const DimensionParams* p);
void Dimension_GetParams(const DimensionDSP* d, DimensionParams* p);
void Dimension_ProcessBlock(
    DimensionDSP* d,
    const float* inL,
    const float* inR,
    float* outL,
    float* outR,
    uint32_t n);

#ifdef __cplusplus
}
#endif
