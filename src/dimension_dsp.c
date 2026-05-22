#include "dimension_dsp.h"

#include <string.h>

static DimensionParams dimension_default_params(float sampleRate) {
    DimensionParams p;
    p.sampleRate = (sampleRate > 0.0f) ? sampleRate : DIMENSION_SAMPLE_RATE_DEFAULT;
    p.mode = DIMENSION_MODE_I;
    p.quality = DIMENSION_QUALITY_BBD_LITE;
    p.inputGain = 1.0f;
    p.outputGain = 1.0f;
    p.dryGain = 0.83f;
    p.wetDirectGain = 0.50f;
    p.wetCrossGain = 0.35f;
    p.baseDelayMs = 7.0f;
    p.depthMs = 0.9f;
    p.rateHz = 0.25f;
    p.hpfHz = 120.0f;
    p.lpfHz = 8000.0f;
    p.analogAmount = 0.35f;
    p.companderAmount = 0.35f;
    p.noiseAmount = 0.0f;
    p.width = 1.0f;
    return p;
}

void Dimension_Init(DimensionDSP* d, float sampleRate) {
    if (d == NULL) {
        return;
    }
    d->params = dimension_default_params(sampleRate);
}

void Dimension_Reset(DimensionDSP* d) {
    if (d == NULL) {
        return;
    }
    d->params = dimension_default_params(DIMENSION_SAMPLE_RATE_DEFAULT);
}

void Dimension_SetMode(DimensionDSP* d, DimensionMode mode) {
    if (d == NULL) {
        return;
    }
    d->params.mode = mode;
}

void Dimension_SetParams(DimensionDSP* d, const DimensionParams* p) {
    if (d == NULL || p == NULL) {
        return;
    }
    d->params = *p;
}

void Dimension_GetParams(const DimensionDSP* d, DimensionParams* p) {
    if (d == NULL || p == NULL) {
        return;
    }
    *p = d->params;
}

void Dimension_ProcessBlock(
    DimensionDSP* d,
    const float* inL,
    const float* inR,
    float* outL,
    float* outR,
    uint32_t n) {
    if (d == NULL || inL == NULL || inR == NULL || outL == NULL || outR == NULL) {
        return;
    }

    if (n > DIMENSION_MAX_BLOCK_SIZE) {
        n = DIMENSION_MAX_BLOCK_SIZE;
    }

    for (uint32_t i = 0U; i < n; ++i) {
        outL[i] = inL[i];
        outR[i] = inR[i];
    }
}
