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

static inline float dimension_sanitize_param(float x, float fallback, float lo, float hi) {
    if (!isfinite(x)) {
        x = fallback;
    }
    return dimension_clampf(x, lo, hi);
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

#if DIMENSION_INTERP_MODE == DIMENSION_INTERP_LINEAR
static inline float delay_linear(const float* buffer, float readPos) {
    const int32_t base = (int32_t)floorf(readPos);
    const float t = readPos - (float)base;
    const uint32_t i0 = (uint32_t)base & DIMENSION_DELAY_MASK;
    const uint32_t i1 = (uint32_t)(base + 1) & DIMENSION_DELAY_MASK;
    return buffer[i0] + t * (buffer[i1] - buffer[i0]);
}
#endif

#if DIMENSION_INTERP_MODE == DIMENSION_INTERP_LAGRANGE3
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
#endif

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
    d->bassFocusState = 0.0f;
    DimensionBBDClock_Reset(&d->bbdClock);
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
    p.stereoWidth = 1.0f;
    p.bassFocusHz = 0.0f;
    p.bbdCharacter = 0.0f;
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
        p->dryGain = 0.83f;
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
    d->smoothDryGain = d->params.dryGain;
    d->smoothInputGain = d->params.inputGain;
    d->smoothOutputGain = d->params.outputGain;
    d->smoothWidth = d->params.width;
    d->smoothStereoWidth = d->params.stereoWidth;
    d->smoothHpfHz = d->params.hpfHz;
    d->smoothLpfHz = d->params.lpfHz;
    d->smoothAnalogAmount = d->params.analogAmount;
    d->smoothCompanderAmount = d->params.companderAmount;
    d->smoothBassFocusHz = (d->params.bassFocusHz > 0.0f) ? d->params.bassFocusHz : 120.0f;
    d->smoothBassFocusAmount = (d->params.bassFocusHz > 0.0f) ? 1.0f : 0.0f;
    d->smoothBbdCharacter = d->params.bbdCharacter;
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
    d->params.sampleRate = dimension_sanitize_param(d->params.sampleRate, DIMENSION_SAMPLE_RATE_DEFAULT, 1000.0f, 192000.0f);
    d->params.inputGain = dimension_sanitize_param(d->params.inputGain, 1.0f, 0.0f, 8.0f);
    d->params.outputGain = dimension_sanitize_param(d->params.outputGain, 1.0f, 0.0f, 8.0f);
    d->params.dryGain = dimension_sanitize_param(d->params.dryGain, 0.83f, 0.0f, 2.0f);
    d->params.wetDirectGain = dimension_sanitize_param(d->params.wetDirectGain, 0.50f, 0.0f, 2.0f);
    d->params.wetCrossGain = dimension_sanitize_param(d->params.wetCrossGain, 0.35f, 0.0f, 2.0f);
    d->params.baseDelayMs = dimension_sanitize_param(d->params.baseDelayMs, 7.0f, 1.0f, DIMENSION_DELAY_MAX_MS);
    d->params.depthMs = dimension_sanitize_param(d->params.depthMs, 0.9f, 0.0f, DIMENSION_DELAY_MAX_MS);
    d->params.rateHz = dimension_sanitize_param(d->params.rateHz, 0.25f, 0.01f, 8.0f);
    d->params.hpfHz = dimension_sanitize_param(d->params.hpfHz, 120.0f, 1.0f, 2000.0f);
    d->params.lpfHz = dimension_sanitize_param(d->params.lpfHz, 8000.0f, 200.0f, 22000.0f);
    d->params.analogAmount = dimension_sanitize_param(d->params.analogAmount, 0.35f, 0.0f, 1.0f);
    d->params.companderAmount = dimension_sanitize_param(d->params.companderAmount, 0.35f, 0.0f, 1.0f);
    d->params.noiseAmount = dimension_sanitize_param(d->params.noiseAmount, 0.0f, 0.0f, 1.0f);
    d->params.width = dimension_sanitize_param(d->params.width, 1.0f, 0.0f, 2.0f);
    d->params.stereoWidth = dimension_sanitize_param(d->params.stereoWidth, 1.0f, 0.0f, 2.0f);
    d->params.bassFocusHz = dimension_sanitize_param(d->params.bassFocusHz, 0.0f, 0.0f, 500.0f);
    d->params.bbdCharacter = dimension_sanitize_param(d->params.bbdCharacter, 0.0f, 0.0f, 1.0f);
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

void Dimension_GetModeParams(DimensionMode mode, DimensionParams* p) {
    if (p == NULL) {
        return;
    }
    *p = dimension_default_params(DIMENSION_SAMPLE_RATE_DEFAULT);
    dimension_apply_mode_preset(p, mode);
}

void Dimension_ProcessBlock(
    DimensionDSP* d,
    const float* inL,
    const float* inR,
    float* outL,
    float* outR,
    uint32_t n) {
    if (d == NULL || inL == NULL || inR == NULL || outL == NULL || outR == NULL || n == 0U) {
        return;
    }

    if (n > DIMENSION_MAX_BLOCK_SIZE) {
        n = DIMENSION_MAX_BLOCK_SIZE;
    }

    const float sr = (d->params.sampleRate > 1000.0f) ? d->params.sampleRate : DIMENSION_SAMPLE_RATE_DEFAULT;
    const float smoothCoeff = dimension_clampf(d->smoothCoeff, 0.0f, 1.0f);
    const float maxDelaySpan = (float)(DIMENSION_DELAY_SIZE - 4U);
    const float invSr = 1.0f / sr;
    const float msToSamples = sr * 0.001f;
    const float atkC = expf(-1.0f / (0.005f * sr));
    const float relC = expf(-1.0f / (0.080f * sr));

    /* Block-rate smoother for filter cutoffs, analog saturation, and bass focus.
     * Derived from effective block frame count n to guarantee consistent
     * time constant (~30 ms) across blocks of 16, 32, 64 or partial frames.
     * Computes only one exponential once per block. */
    const float tauSec = 0.030f;
    const float blockSmoothCoeff = 1.0f - expf(-(float)n / (tauSec * sr));
    d->smoothHpfHz += blockSmoothCoeff * (d->params.hpfHz - d->smoothHpfHz);
    d->smoothLpfHz += blockSmoothCoeff * (d->params.lpfHz - d->smoothLpfHz);
    d->smoothAnalogAmount += blockSmoothCoeff * (d->params.analogAmount - d->smoothAnalogAmount);

    const float targetBassFocusAmount = (d->params.bassFocusHz > 0.0f) ? 1.0f : 0.0f;
    const float targetBassFocusHz = (d->params.bassFocusHz > 0.0f) ? d->params.bassFocusHz : d->smoothBassFocusHz;
    d->smoothBassFocusAmount += blockSmoothCoeff * (targetBassFocusAmount - d->smoothBassFocusAmount);
    d->smoothBassFocusHz += blockSmoothCoeff * (targetBassFocusHz - d->smoothBassFocusHz);
    if (d->smoothBassFocusAmount < 1e-4f && targetBassFocusAmount == 0.0f) {
        d->smoothBassFocusAmount = 0.0f;
        d->bassFocusState = 0.0f;
    }
    const float bassFocusAlpha = one_pole_alpha(d->smoothBassFocusHz, sr);
    const float bassFocusAmount = d->smoothBassFocusAmount;

    const float hpfAlpha = one_pole_alpha(d->smoothHpfHz, sr);
    const float lpfAlpha = one_pole_alpha(d->smoothLpfHz, sr);
    const float analogAmount = dimension_clampf(d->smoothAnalogAmount, 0.0f, 1.0f);
    const float preAlpha = one_pole_alpha(3200.0f + 6400.0f * (1.0f - analogAmount), sr);

    /* Temporal Clock Tracking & Block Interpolation:
     * Compute start and predicted end values of baseDelay and depth across this block (n frames)
     * using the closed form of the 20 ms one-pole smoother (gamma = exp(-n / (0.020 * sr))).
     * Linearly interpolate bbdF0 and bbdDepthRatio across the n samples.
     * Guarantees exact continuity across block boundaries (0 jump) for any block size,
     * keeps clock in sync with delay evolution during automation, and eliminates per-sample division. */
    const float tauDelaySec = 0.020f;
    const float gammaDelay = expf(-(float)n / (tauDelaySec * sr));
    const float endBaseMs = d->params.baseDelayMs + (d->smoothBaseDelayMs - d->params.baseDelayMs) * gammaDelay;
    const float endDepthMs = d->params.depthMs + (d->smoothDepthMs - d->params.depthMs) * gammaDelay;

    const float startBaseMs = (d->smoothBaseDelayMs > 0.5f) ? d->smoothBaseDelayMs : 0.5f;
    const float startDepthMs = (d->smoothDepthMs >= 0.0f) ? d->smoothDepthMs : 0.0f;
    const float safeEndBaseMs = (endBaseMs > 0.5f) ? endBaseMs : 0.5f;
    const float safeEndDepthMs = (endDepthMs >= 0.0f) ? endDepthMs : 0.0f;

    const float startF0 = 512000.0f / startBaseMs;
    const float startRatio = startDepthMs / startBaseMs;
    const float endF0 = 512000.0f / safeEndBaseMs;
    const float endRatio = safeEndDepthMs / safeEndBaseMs;

    const float invN = (n > 0U) ? (1.0f / (float)n) : 0.0f;
    const float deltaBbdF0 = (endF0 - startF0) * invN;
    const float deltaBbdRatio = (endRatio - startRatio) * invN;
    float curBbdF0 = startF0;
    float curBbdDepthRatio = startRatio;

    for (uint32_t i = 0U; i < n; ++i) {
        /* Per-sample parameter smoothing (rate, depth, delay, matrix gains, width, stereoWidth, input/output gains, bbdCharacter) */
        d->smoothRateHz += smoothCoeff * (d->params.rateHz - d->smoothRateHz);
        d->smoothDepthMs += smoothCoeff * (d->params.depthMs - d->smoothDepthMs);
        d->smoothBaseDelayMs += smoothCoeff * (d->params.baseDelayMs - d->smoothBaseDelayMs);
        d->smoothWetDirectGain += smoothCoeff * (d->params.wetDirectGain - d->smoothWetDirectGain);
        d->smoothWetCrossGain += smoothCoeff * (d->params.wetCrossGain - d->smoothWetCrossGain);
        d->smoothDryGain += smoothCoeff * (d->params.dryGain - d->smoothDryGain);
        d->smoothInputGain += smoothCoeff * (d->params.inputGain - d->smoothInputGain);
        d->smoothOutputGain += smoothCoeff * (d->params.outputGain - d->smoothOutputGain);
        d->smoothWidth += smoothCoeff * (d->params.width - d->smoothWidth);
        d->smoothStereoWidth += smoothCoeff * (d->params.stereoWidth - d->smoothStereoWidth);
        if (fabsf(d->smoothStereoWidth - 1.0f) < 1e-5f && d->params.stereoWidth == 1.0f) {
            d->smoothStereoWidth = 1.0f;
        }
        d->smoothCompanderAmount += smoothCoeff * (d->params.companderAmount - d->smoothCompanderAmount);
        d->smoothBbdCharacter += smoothCoeff * (d->params.bbdCharacter - d->smoothBbdCharacter);
        if (d->smoothBbdCharacter < 1e-5f && d->params.bbdCharacter == 0.0f) {
            d->smoothBbdCharacter = 0.0f;
        }

        const float inGain = dimension_clampf(d->smoothInputGain, 0.0f, 8.0f);
        const float outGain = dimension_clampf(d->smoothOutputGain, 0.0f, 8.0f);
        const float gDry = dimension_clampf(d->smoothDryGain, 0.0f, 2.0f);
        const float width = dimension_clampf(d->smoothWidth, 0.0f, 2.0f);
        const float stereoWidth = dimension_clampf(d->smoothStereoWidth, 0.0f, 2.0f);
        const float bbdChar = dimension_clampf(d->smoothBbdCharacter, 0.0f, 1.0f);

        float xL = dimension_sanitize_sample(inL[i]) * inGain;
        float xR = dimension_sanitize_sample(inR[i]) * inGain;
        const float dryL = xL;
        const float dryR = xR;
        float wetL = xL;
        float wetR = xR;

        const float gWet1 = dimension_clampf(d->smoothWetDirectGain * width, 0.0f, 2.0f);
        const float gWet2 = dimension_clampf(d->smoothWetCrossGain * width, 0.0f, 2.0f);
        const float rate = dimension_clampf(d->smoothRateHz, 0.01f, 8.0f);
        const float lfoInc = rate * invSr;
        const float baseDelay = dimension_clampf(d->smoothBaseDelayMs * msToSamples, 1.0f, maxDelaySpan - 1.0f);
        const float depthMax = fminf(baseDelay - 1.0f, maxDelaySpan - baseDelay);
        const float depthDelay = dimension_clampf(d->smoothDepthMs * msToSamples, 0.0f, depthMax);
        const float amount = dimension_clampf(d->smoothCompanderAmount, 0.0f, 1.0f);

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
        const float lfo = DimensionBBDClock_ModulateLfo(tri, bbdChar);

        /* 7th-order reciprocal: 1/(1 +/- x) = (1 -/+ x)(1 + x^2)(1 + x^4)
         * Max error < 0.0004% across Modes I..IV, and < 0.4% up to x = 0.50.
         * Factor is shared between channels; requires only 5 multiplies, 0 divisions. */
        const float x = dimension_clampf(curBbdDepthRatio * lfo, -0.75f, 0.75f);
        const float x2 = x * x;
        const float x4 = x2 * x2;
        const float factor = (1.0f + x2) * (1.0f + x4);
        const float invL = (1.0f - x) * factor;
        const float invR = (1.0f + x) * factor;
        float clockL = curBbdF0 * invL;
        float clockR = curBbdF0 * invR;
        clockL = dimension_clampf(clockL, DIMENSION_BBD_MIN_CLOCK_HZ, DIMENSION_BBD_MAX_CLOCK_HZ);
        clockR = dimension_clampf(clockR, DIMENSION_BBD_MIN_CLOCK_HZ, DIMENSION_BBD_MAX_CLOCK_HZ);
        d->bbdClock.chL.instantaneousClockHz = clockL;
        d->bbdClock.chR.instantaneousClockHz = clockR;

        const float transLagL = DimensionBBDClock_TransportLag(clockL, curBbdF0, bbdChar);
        const float transLagR = DimensionBBDClock_TransportLag(clockR, curBbdF0, bbdChar);
        const float delayL = baseDelay + depthDelay * lfo + transLagL;
        const float delayR = baseDelay - depthDelay * lfo + transLagR;
        const float readPosL = (float)d->writePos - delayL + (float)DIMENSION_DELAY_SIZE;
        const float readPosR = (float)d->writePos - delayR + (float)DIMENSION_DELAY_SIZE;

        d->delayL[d->writePos] = wetL;
        d->delayR[d->writePos] = wetR;

        wetL = delay_interp(d->delayL, readPosL);
        wetR = delay_interp(d->delayR, readPosR);

        wetL = DimensionBBDClock_ProcessDispersion(&d->bbdClock.chL, wetL, clockL, sr, bbdChar);
        wetR = DimensionBBDClock_ProcessDispersion(&d->bbdClock.chR, wetR, clockR, sr, bbdChar);

        curBbdF0 += deltaBbdF0;
        curBbdDepthRatio += deltaBbdRatio;

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

        /* Spatial Engine 2.0: True Stereo Width and Bass Focus (Mid/Side processing)
         * Exact bit-neutral bypass when stereoWidth == 1.0 and bassFocusAmount == 0.0f.
         * The Mid component (0.5*(yL+yR)) is mathematically invariant in the linear domain. */
        if (stereoWidth != 1.0f || bassFocusAmount > 0.0f) {
            const float mid = 0.5f * (yL + yR);
            const float side = 0.5f * (yL - yR);

            d->bassFocusState += bassFocusAlpha * (side - d->bassFocusState);
            if (fabsf(d->bassFocusState) < 1.0e-15f) {
                d->bassFocusState = 0.0f;
            }
            const float sideFiltered = side - bassFocusAmount * d->bassFocusState;
            const float processedSide = sideFiltered * stereoWidth;

            yL = mid + processedSide;
            yR = mid - processedSide;
        }

        yL *= outGain;
        yR *= outGain;
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
