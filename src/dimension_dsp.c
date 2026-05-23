#include "dimension_dsp.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define DIMENSION_PI 3.14159265358979323846f

static inline float dimension_clampf(float x, float lo, float hi) {
    return (x < lo) ? lo : ((x > hi) ? hi : x);
}

static inline float dimension_sanitize_sample(float x) {
    if (!isfinite(x)) {
        return 0.0f;
    }
    return dimension_clampf(x, -2.0f, 2.0f);
}

static inline float softclip_cubic(float x) {
    const float xc = dimension_clampf(x, -1.0f, 1.0f);
    return xc - (xc * xc * xc) * (1.0f / 3.0f);
}

static inline float one_pole_alpha(float cutoffHz, float sampleRate) {
    const float fc = dimension_clampf(cutoffHz, 1.0f, 0.49f * sampleRate);
    const float x = expf(-2.0f * DIMENSION_PI * fc / sampleRate);
    return 1.0f - x;
}

static inline float delay_hermite(const float* buffer, float readPos) {
    const int32_t base = (int32_t)floorf(readPos);
    const float t = readPos - (float)base;
    const uint32_t i0 = (uint32_t)(base - 1) & DIMENSION_DELAY_MASK;
    const uint32_t i1 = (uint32_t)base & DIMENSION_DELAY_MASK;
    const uint32_t i2 = (uint32_t)(base + 1) & DIMENSION_DELAY_MASK;
    const uint32_t i3 = (uint32_t)(base + 2) & DIMENSION_DELAY_MASK;
    const float xm1 = buffer[i0];
    const float x0 = buffer[i1];
    const float x1 = buffer[i2];
    const float x2 = buffer[i3];
    const float c0 = x0;
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

static inline float delay_linear(const float* buffer, float readPos) {
    const int32_t base = (int32_t)floorf(readPos);
    const float t = readPos - (float)base;
    const uint32_t i0 = (uint32_t)base & DIMENSION_DELAY_MASK;
    const uint32_t i1 = (uint32_t)(base + 1) & DIMENSION_DELAY_MASK;
    return buffer[i0] + t * (buffer[i1] - buffer[i0]);
}

static inline float delay_lagrange3(const float* buffer, float readPos) {
    const int32_t base = (int32_t)floorf(readPos);
    const float t = readPos - (float)base;
    const uint32_t i0 = (uint32_t)(base - 1) & DIMENSION_DELAY_MASK;
    const uint32_t i1 = (uint32_t)base & DIMENSION_DELAY_MASK;
    const uint32_t i2 = (uint32_t)(base + 1) & DIMENSION_DELAY_MASK;
    const uint32_t i3 = (uint32_t)(base + 2) & DIMENSION_DELAY_MASK;
    const float xm1 = buffer[i0];
    const float x0 = buffer[i1];
    const float x1 = buffer[i2];
    const float x2 = buffer[i3];
    const float c0 = (-t * (t - 1.0f) * (t - 2.0f)) * (1.0f / 6.0f);
    const float c1 = ((t + 1.0f) * (t - 1.0f) * (t - 2.0f)) * 0.5f;
    const float c2 = (-(t + 1.0f) * t * (t - 2.0f)) * 0.5f;
    const float c3 = ((t + 1.0f) * t * (t - 1.0f)) * (1.0f / 6.0f);
    return xm1 * c0 + x0 * c1 + x1 * c2 + x2 * c3;
}

static inline float delay_interp(const float* buffer, float readPos) {
#if DIMENSION_INTERP_MODE == DIMENSION_INTERP_LINEAR
    return delay_linear(buffer, readPos);
#elif DIMENSION_INTERP_MODE == DIMENSION_INTERP_LAGRANGE3
    return delay_lagrange3(buffer, readPos);
#else
    return delay_hermite(buffer, readPos);
#endif
}

static void dimension_clear_state(DimensionDSP* d) {
    memset(d->delayL, 0, sizeof(d->delayL));
    memset(d->delayR, 0, sizeof(d->delayR));
    d->writePos = 0U;
    d->lfoPhase = 0.0f;
    d->hpfStateL = d->hpfStateR = 0.0f;
    d->lpf1StateL = d->lpf1StateR = 0.0f;
    d->lpf2StateL = d->lpf2StateR = 0.0f;
    d->compEnvL = d->compEnvR = 0.0f;
    d->expEnvL = d->expEnvR = 0.0f;
    d->bbdStateL = d->bbdStateR = 0.0f;
}

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


static inline void dimension_apply_mode_preset(DimensionParams* p, DimensionMode mode) {
    if (p == NULL) {
        return;
    }

    float rateHz = p->rateHz;
    float baseDelayMs = p->baseDelayMs;
    float depthMs = p->depthMs;
    float hpfHz = p->hpfHz;
    float lpfHz = p->lpfHz;
    float wetScale = 1.0f;
    float analogAmount = p->analogAmount;
    float companderAmount = p->companderAmount;

    switch (mode) {
        case DIMENSION_MODE_I:
            rateHz = 0.25f; baseDelayMs = 7.0f; depthMs = 0.9f; hpfHz = 120.0f; lpfHz = 8000.0f; wetScale = 0.80f; analogAmount = 0.35f; companderAmount = 0.35f;
            break;
        case DIMENSION_MODE_II:
            rateHz = 0.25f; baseDelayMs = 8.5f; depthMs = 1.4f; hpfHz = 120.0f; lpfHz = 7500.0f; wetScale = 0.95f; analogAmount = 0.45f; companderAmount = 0.45f;
            break;
        case DIMENSION_MODE_III:
            rateHz = 0.50f; baseDelayMs = 10.0f; depthMs = 1.8f; hpfHz = 130.0f; lpfHz = 7000.0f; wetScale = 1.00f; analogAmount = 0.50f; companderAmount = 0.50f;
            break;
        case DIMENSION_MODE_IV:
            rateHz = 0.50f; baseDelayMs = 11.5f; depthMs = 2.4f; hpfHz = 140.0f; lpfHz = 6500.0f; wetScale = 1.10f; analogAmount = 0.60f; companderAmount = 0.60f;
            break;
        case DIMENSION_MODE_CUSTOM:
        default:
            break;
    }

    p->mode = mode;
    if (mode != DIMENSION_MODE_CUSTOM) {
        p->rateHz = rateHz;
        p->baseDelayMs = baseDelayMs;
        p->depthMs = depthMs;
        p->hpfHz = hpfHz;
        p->lpfHz = lpfHz;
        p->wetDirectGain = 0.50f * wetScale;
        p->wetCrossGain = 0.35f * wetScale;
        p->analogAmount = analogAmount;
        p->companderAmount = companderAmount;
    }
}

static inline float dimension_smooth_coeff(float sampleRate) {
    const float sr = (sampleRate > 1000.0f) ? sampleRate : DIMENSION_SAMPLE_RATE_DEFAULT;
    const float tauSec = 0.030f;
    return 1.0f - expf(-1.0f / (tauSec * sr));
}

static inline void dimension_update_smoothing_config(DimensionDSP* d) {
    const float sr = (d->params.sampleRate > 1000.0f) ? d->params.sampleRate : DIMENSION_SAMPLE_RATE_DEFAULT;
    d->smoothCoeff = dimension_smooth_coeff(sr);
}

static inline void dimension_sync_smoothers(DimensionDSP* d) {
    d->smoothRateHz = d->params.rateHz;
    d->smoothDepthMs = d->params.depthMs;
    d->smoothBaseDelayMs = d->params.baseDelayMs;
    d->smoothWetDirectGain = d->params.wetDirectGain;
    d->smoothWetCrossGain = d->params.wetCrossGain;
    d->smoothHpfHz = d->params.hpfHz;
    d->smoothLpfHz = d->params.lpfHz;
    d->smoothAnalogAmount = d->params.analogAmount;
    d->smoothCompanderAmount = d->params.companderAmount;
}

void Dimension_Init(DimensionDSP* d, float sampleRate) {
    if (d == NULL) {
        return;
    }
    d->params = dimension_default_params(sampleRate);
    dimension_apply_mode_preset(&d->params, d->params.mode);
    dimension_clear_state(d);
    dimension_update_smoothing_config(d);
    dimension_sync_smoothers(d);
}

void Dimension_Reset(DimensionDSP* d) {
    if (d == NULL) {
        return;
    }
    dimension_clear_state(d);
    dimension_update_smoothing_config(d);
    dimension_sync_smoothers(d);
}

void Dimension_SetMode(DimensionDSP* d, DimensionMode mode) {
    if (d == NULL) {
        return;
    }
    dimension_apply_mode_preset(&d->params, mode);
}

void Dimension_SetParams(DimensionDSP* d, const DimensionParams* p) {
    if (d == NULL || p == NULL) {
        return;
    }
    d->params = *p;
    if (d->params.mode != DIMENSION_MODE_CUSTOM) {
        dimension_apply_mode_preset(&d->params, d->params.mode);
    }
    dimension_update_smoothing_config(d);
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

    const float sr = (d->params.sampleRate > 1000.0f) ? d->params.sampleRate : DIMENSION_SAMPLE_RATE_DEFAULT;
    const float inputGain = dimension_clampf(d->params.inputGain, 0.0f, 8.0f);
    const float outputGain = dimension_clampf(d->params.outputGain, 0.0f, 8.0f);
    const float gDry = dimension_clampf(d->params.dryGain, 0.0f, 2.0f);
    const float width = dimension_clampf(d->params.width, 0.0f, 2.0f);
    const float smoothCoeff = dimension_clampf(d->smoothCoeff, 0.0f, 1.0f);
    const float maxDelaySpan = (float)(DIMENSION_DELAY_SIZE - 4U);
    const float invSr = 1.0f / sr;
    const float msToSamples = sr * 0.001f;
    const float atkC = expf(-1.0f / (0.005f * sr));
    const float relC = expf(-1.0f / (0.080f * sr));

    for (uint32_t i = 0U; i < n; ++i) {
        float xL = dimension_sanitize_sample(inL[i]) * inputGain;
        float xR = dimension_sanitize_sample(inR[i]) * inputGain;
        const float dryL = xL;
        const float dryR = xR;
        float wetL = xL;
        float wetR = xR;

        d->smoothRateHz += smoothCoeff * (d->params.rateHz - d->smoothRateHz);
        d->smoothDepthMs += smoothCoeff * (d->params.depthMs - d->smoothDepthMs);
        d->smoothBaseDelayMs += smoothCoeff * (d->params.baseDelayMs - d->smoothBaseDelayMs);
        d->smoothWetDirectGain += smoothCoeff * (d->params.wetDirectGain - d->smoothWetDirectGain);
        d->smoothWetCrossGain += smoothCoeff * (d->params.wetCrossGain - d->smoothWetCrossGain);
        d->smoothHpfHz += smoothCoeff * (d->params.hpfHz - d->smoothHpfHz);
        d->smoothLpfHz += smoothCoeff * (d->params.lpfHz - d->smoothLpfHz);
        d->smoothAnalogAmount += smoothCoeff * (d->params.analogAmount - d->smoothAnalogAmount);
        d->smoothCompanderAmount += smoothCoeff * (d->params.companderAmount - d->smoothCompanderAmount);

        const float gWet1 = dimension_clampf(d->smoothWetDirectGain * width, 0.0f, 2.0f);
        const float gWet2 = dimension_clampf(d->smoothWetCrossGain * width, 0.0f, 2.0f);
        const float rate = dimension_clampf(d->smoothRateHz, 0.01f, 8.0f);
        const float lfoInc = rate * invSr;
        const float baseDelay = dimension_clampf(d->smoothBaseDelayMs * msToSamples, 1.0f, maxDelaySpan - 1.0f);
        const float depthMax = fminf(baseDelay - 1.0f, maxDelaySpan - baseDelay);
        const float depthDelay = dimension_clampf(d->smoothDepthMs * msToSamples, 0.0f, depthMax);
        const float amount = dimension_clampf(d->smoothCompanderAmount, 0.0f, 1.0f);
        const float hpfAlpha = one_pole_alpha(d->smoothHpfHz, sr);
        const float lpfAlpha = one_pole_alpha(d->smoothLpfHz, sr);
        const float analogAmount = dimension_clampf(d->smoothAnalogAmount, 0.0f, 1.0f);
        const float preAlpha = one_pole_alpha(3200.0f + 6400.0f * (1.0f - analogAmount), sr);

        d->hpfStateL = (1.0f - hpfAlpha) * d->hpfStateL + hpfAlpha * wetL;
        d->hpfStateR = (1.0f - hpfAlpha) * d->hpfStateR + hpfAlpha * wetR;
        wetL = wetL - d->hpfStateL;
        wetR = wetR - d->hpfStateR;

        const float detL = fabsf(wetL);
        const float detR = fabsf(wetR);
        d->compEnvL = (detL > d->compEnvL) ? (atkC * d->compEnvL + (1.0f - atkC) * detL)
                                            : (relC * d->compEnvL + (1.0f - relC) * detL);
        d->compEnvR = (detR > d->compEnvR) ? (atkC * d->compEnvR + (1.0f - atkC) * detR)
                                            : (relC * d->compEnvR + (1.0f - relC) * detR);
        const float compGainL = 1.0f / (1.0f + amount * 0.5f * d->compEnvL);
        const float compGainR = 1.0f / (1.0f + amount * 0.5f * d->compEnvR);
        wetL *= compGainL;
        wetR *= compGainR;

        d->bbdStateL += preAlpha * (wetL - d->bbdStateL);
        d->bbdStateR += preAlpha * (wetR - d->bbdStateR);
        wetL = d->bbdStateL;
        wetR = d->bbdStateR;

        const float tri = (d->lfoPhase < 0.5f) ? (4.0f * d->lfoPhase - 1.0f) : (3.0f - 4.0f * d->lfoPhase);
        const float lfo = tri;
        const float delayL = baseDelay + depthDelay * lfo;
        const float delayR = baseDelay - depthDelay * lfo;
        const float readPosL = (float)d->writePos - delayL + (float)DIMENSION_DELAY_SIZE;
        const float readPosR = (float)d->writePos - delayR + (float)DIMENSION_DELAY_SIZE;

        d->delayL[d->writePos] = wetL;
        d->delayR[d->writePos] = wetR;

        wetL = delay_interp(d->delayL, readPosL);
        wetR = delay_interp(d->delayR, readPosR);

        d->lpf1StateL += lpfAlpha * (wetL - d->lpf1StateL);
        d->lpf1StateR += lpfAlpha * (wetR - d->lpf1StateR);
        d->lpf2StateL += lpfAlpha * (d->lpf1StateL - d->lpf2StateL);
        d->lpf2StateR += lpfAlpha * (d->lpf1StateR - d->lpf2StateR);
        wetL = d->lpf2StateL;
        wetR = d->lpf2StateR;

        wetL = softclip_cubic(wetL);
        wetR = softclip_cubic(wetR);

        const float wAbsL = fabsf(wetL);
        const float wAbsR = fabsf(wetR);
        d->expEnvL = (wAbsL > d->expEnvL) ? (atkC * d->expEnvL + (1.0f - atkC) * wAbsL)
                                           : (relC * d->expEnvL + (1.0f - relC) * wAbsL);
        d->expEnvR = (wAbsR > d->expEnvR) ? (atkC * d->expEnvR + (1.0f - atkC) * wAbsR)
                                           : (relC * d->expEnvR + (1.0f - relC) * wAbsR);
        const float expGainL = 1.0f + amount * 0.15f * d->expEnvL;
        const float expGainR = 1.0f + amount * 0.15f * d->expEnvR;
        wetL *= expGainL;
        wetR *= expGainR;

        float yL = gDry * dryL + gWet1 * wetL - gWet2 * wetR;
        float yR = gDry * dryR + gWet1 * wetR - gWet2 * wetL;

        yL *= outputGain;
        yR *= outputGain;
#if DIMENSION_ENABLE_SAFETY_LIMITER
        yL = dimension_clampf(yL, -1.2f, 1.2f);
        yR = dimension_clampf(yR, -1.2f, 1.2f);
#endif
        outL[i] = dimension_sanitize_sample(yL);
        outR[i] = dimension_sanitize_sample(yR);

        d->writePos = (d->writePos + 1U) & DIMENSION_DELAY_MASK;
        d->lfoPhase += lfoInc;
        if (d->lfoPhase >= 1.0f) {
            d->lfoPhase -= 1.0f;
        }
    }
}
