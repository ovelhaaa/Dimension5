#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DIMENSION_PARAM_SCHEMA_VERSION 1U

typedef enum DimensionParamId {
    DIMENSION_PARAM_INPUT_GAIN = 0,
    DIMENSION_PARAM_OUTPUT_GAIN,
    DIMENSION_PARAM_DRY_GAIN,
    DIMENSION_PARAM_WET_DIRECT_GAIN,
    DIMENSION_PARAM_WET_CROSS_GAIN,
    DIMENSION_PARAM_BASE_DELAY_MS,
    DIMENSION_PARAM_DEPTH_MS,
    DIMENSION_PARAM_RATE_HZ,
    DIMENSION_PARAM_HPF_HZ,
    DIMENSION_PARAM_LPF_HZ,
    DIMENSION_PARAM_ANALOG_AMOUNT,
    DIMENSION_PARAM_COMPANDER_AMOUNT,
    DIMENSION_PARAM_WIDTH,
    DIMENSION_PARAM_COUNT
} DimensionParamId;

typedef enum DimensionParamRole {
    DIMENSION_PARAM_ROLE_PRIMARY = 0,
    DIMENSION_PARAM_ROLE_ADVANCED
} DimensionParamRole;

typedef struct DimensionParamDescriptor {
    DimensionParamId id;
    const char* stableId;
    const char* displayName;
    const char* unit;
    float minValue;
    float maxValue;
    float defaultValue;
    DimensionParamRole role;
    uint8_t automatable;
} DimensionParamDescriptor;

static const DimensionParamDescriptor DimensionParamDescriptors[DIMENSION_PARAM_COUNT] = {
    { DIMENSION_PARAM_INPUT_GAIN, "inputGain", "Input", "x", 0.0f, 4.0f, 1.0f, DIMENSION_PARAM_ROLE_PRIMARY, 1U },
    { DIMENSION_PARAM_OUTPUT_GAIN, "outputGain", "Output", "x", 0.0f, 4.0f, 1.0f, DIMENSION_PARAM_ROLE_PRIMARY, 1U },
    { DIMENSION_PARAM_DRY_GAIN, "dryGain", "Dry", "x", 0.0f, 2.0f, 0.83f, DIMENSION_PARAM_ROLE_ADVANCED, 1U },
    { DIMENSION_PARAM_WET_DIRECT_GAIN, "wetDirectGain", "Wet Direct", "x", 0.0f, 2.0f, 0.50f, DIMENSION_PARAM_ROLE_ADVANCED, 1U },
    { DIMENSION_PARAM_WET_CROSS_GAIN, "wetCrossGain", "Wet Cross", "x", 0.0f, 2.0f, 0.35f, DIMENSION_PARAM_ROLE_ADVANCED, 1U },
    { DIMENSION_PARAM_BASE_DELAY_MS, "baseDelayMs", "Base Delay", "ms", 1.0f, 20.0f, 7.0f, DIMENSION_PARAM_ROLE_ADVANCED, 1U },
    { DIMENSION_PARAM_DEPTH_MS, "depthMs", "Depth", "ms", 0.0f, 6.0f, 0.9f, DIMENSION_PARAM_ROLE_ADVANCED, 1U },
    { DIMENSION_PARAM_RATE_HZ, "rateHz", "Rate", "Hz", 0.01f, 4.0f, 0.25f, DIMENSION_PARAM_ROLE_ADVANCED, 1U },
    { DIMENSION_PARAM_HPF_HZ, "hpfHz", "Low Focus", "Hz", 20.0f, 400.0f, 120.0f, DIMENSION_PARAM_ROLE_ADVANCED, 1U },
    { DIMENSION_PARAM_LPF_HZ, "lpfHz", "Color", "Hz", 2000.0f, 12000.0f, 8000.0f, DIMENSION_PARAM_ROLE_PRIMARY, 1U },
    { DIMENSION_PARAM_ANALOG_AMOUNT, "analogAmount", "Analog", "", 0.0f, 1.0f, 0.35f, DIMENSION_PARAM_ROLE_PRIMARY, 1U },
    { DIMENSION_PARAM_COMPANDER_AMOUNT, "companderAmount", "Compander", "", 0.0f, 1.0f, 0.35f, DIMENSION_PARAM_ROLE_ADVANCED, 1U },
    { DIMENSION_PARAM_WIDTH, "width", "Width", "", 0.0f, 2.0f, 1.0f, DIMENSION_PARAM_ROLE_PRIMARY, 1U }
};

static inline const DimensionParamDescriptor* Dimension_GetParamDescriptor(DimensionParamId id) {
    if ((uint32_t)id >= (uint32_t)DIMENSION_PARAM_COUNT) {
        return 0;
    }
    return &DimensionParamDescriptors[(uint32_t)id];
}

#ifdef __cplusplus
}
#endif
