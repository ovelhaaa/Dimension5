#pragma once

#include <stdint.h>
#include "dimension_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum DimensionMode_phase_a4 {
    DIMENSION_MODE_I_phase_a4 = 0,
    DIMENSION_MODE_II_phase_a4,
    DIMENSION_MODE_III_phase_a4,
    DIMENSION_MODE_IV_phase_a4,
    DIMENSION_MODE_CUSTOM_phase_a4
} DimensionMode_phase_a4;

typedef enum DimensionQuality_phase_a4 {
    DIMENSION_QUALITY_PERCEPTUAL_phase_a4 = 0,
    DIMENSION_QUALITY_ANALOG_LITE_phase_a4,
    DIMENSION_QUALITY_BBD_LITE_phase_a4
} DimensionQuality_phase_a4;

typedef struct DimensionParams_phase_a4 {
    float sampleRate;
    DimensionMode_phase_a4 mode;
    DimensionQuality_phase_a4 quality;
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
    float bbdCharacter;
} DimensionParams_phase_a4;

typedef struct DimensionBBDClockChannel_phase_a4 {
    float instantaneousClockHz;
    float dispState;
} DimensionBBDClockChannel_phase_a4;

typedef struct DimensionBBDClock_phase_a4 {
    DimensionBBDClockChannel_phase_a4 chL;
    DimensionBBDClockChannel_phase_a4 chR;
} DimensionBBDClock_phase_a4;

typedef struct DimensionDSP_phase_a4 {
    DimensionParams_phase_a4 params;
    DIMENSION_ALIGN_32 float delayL[DIMENSION_DELAY_SIZE];
    DIMENSION_ALIGN_32 float delayR[DIMENSION_DELAY_SIZE];
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
    DimensionBBDClock_phase_a4 bbdClock;
    float smoothRateHz;
    float smoothDepthMs;
    float smoothBaseDelayMs;
    float smoothWetDirectGain;
    float smoothWetCrossGain;
    float smoothDryGain;
    float smoothInputGain;
    float smoothOutputGain;
    float smoothWidth;
    float smoothStereoWidth;
    float smoothHpfHz;
    float smoothLpfHz;
    float smoothAnalogAmount;
    float smoothCompanderAmount;
    float smoothBassFocusHz;
    float smoothBassFocusAmount;
    float smoothBbdCharacter;
    float smoothCoeff;
} DimensionDSP_phase_a4;

void Dimension_Init_phase_a4(DimensionDSP_phase_a4* d, float sampleRate);
void Dimension_Reset_phase_a4(DimensionDSP_phase_a4* d);
void Dimension_SetMode_phase_a4(DimensionDSP_phase_a4* d, DimensionMode_phase_a4 mode);
void Dimension_SetParams_phase_a4(DimensionDSP_phase_a4* d, const DimensionParams_phase_a4* p);
void Dimension_GetParams_phase_a4(const DimensionDSP_phase_a4* d, DimensionParams_phase_a4* p);
void Dimension_ProcessBlock_phase_a4(
    DimensionDSP_phase_a4* d,
    const float* inL,
    const float* inR,
    float* outL,
    float* outR,
    uint32_t n);

#ifdef __cplusplus
}
#endif
