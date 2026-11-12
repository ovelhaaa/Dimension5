#include "dimension_dsp_phase_a4.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define DIMENSION_PI_A4 3.14159265358979323846f
#define DIMENSION_SMOOTH_TAU_SEC_A4 0.030f
#define DIMENSION_SMOOTH_TAU_PREDICTOR_SEC_A4 0.030f

#define DIMENSION_BBD_NOMINAL_CLOCK_HZ_A4 60000.0f
#define DIMENSION_BBD_MIN_CLOCK_HZ_A4     16000.0f
#define DIMENSION_BBD_MAX_CLOCK_HZ_A4     120000.0f

static inline float dimension_clampf_a4(float x, float lo, float hi) {
    return (x < lo) ? lo : ((x > hi) ? hi : x);
}

static inline float dimension_sanitize_sample_a4(float x) {
    if (!isfinite(x)) return 0.0f;
    return dimension_clampf_a4(x, -2.0f, 2.0f);
}

static inline float dimension_sanitize_param_a4(float x, float fallback, float lo, float hi) {
    if (!isfinite(x)) x = fallback;
    return dimension_clampf_a4(x, lo, hi);
}

static inline float softclip_cubic_a4(float x) {
    const float xc = dimension_clampf_a4(x, -1.0f, 1.0f);
    return xc - (xc * xc * xc) * (1.0f / 3.0f);
}

static inline float one_pole_alpha_a4(float cutoffHz, float sampleRate) {
    const float fc = dimension_clampf_a4(cutoffHz, 1.0f, 0.49f * sampleRate);
    const float x = expf(-2.0f * DIMENSION_PI_A4 * fc / sampleRate);
    return 1.0f - x;
}

static inline float delay_hermite_a4(const float* buffer, float readPos) {
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

static inline float modulate_lfo_a4(float tri, float bbdChar) {
    if (bbdChar <= 0.0f) return tri;
    const float tri3 = tri * tri * tri;
    const float curved = 1.25f * tri - 0.25f * tri3;
    return tri + bbdChar * (curved - tri);
}

static inline float transport_lag_a4(float clockHz, float nominalClockHz, float bbdChar) {
    if (bbdChar <= 0.0f || nominalClockHz < 1000.0f) return 0.0f;
    const float relClockShift = (clockHz - nominalClockHz) / nominalClockHz;
    return bbdChar * 0.12f * relClockShift;
}

static inline float alpha_from_cutoff_a4(float fc, float sampleRate) {
    const float invSr = (sampleRate > 1000.0f) ? (1.0f / sampleRate) : (1.0f / 48000.0f);
    const float w = (6.28318530718f * fc) * invSr;
    const float p = ((((0.0027502425f * w - 0.03036323f) * w + 0.15583787f) * w - 0.49582136f) * w + 0.99960209f);
    float alpha = w * p;
    if (alpha < 0.01f) alpha = 0.01f;
    if (alpha > 0.98f) alpha = 0.98f;
    return alpha;
}

static inline float process_dispersion_a4(
    DimensionBBDClockChannel_phase_a4* ch,
    float sample,
    float clockHz,
    float sampleRate,
    float bbdChar) {
    if (bbdChar <= 0.0f) return sample;
    const float clockRatio = clockHz * (1.0f / DIMENSION_BBD_NOMINAL_CLOCK_HZ_A4);
    float fc = 11500.0f + 4500.0f * (clockRatio - 1.0f);
    const float maxFc = 0.45f * sampleRate;
    if (fc < 2500.0f) fc = 2500.0f;
    if (fc > maxFc) fc = maxFc;
    const float safeAlpha = alpha_from_cutoff_a4(fc, sampleRate);
    ch->dispState += safeAlpha * (sample - ch->dispState);
    if (fabsf(ch->dispState) < 1.0e-15f) ch->dispState = 0.0f;
    return sample + bbdChar * 0.35f * (ch->dispState - sample);
}

static void dimension_clear_state_a4(DimensionDSP_phase_a4* d) {
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
    d->bbdClock.chL.instantaneousClockHz = DIMENSION_BBD_NOMINAL_CLOCK_HZ_A4;
    d->bbdClock.chL.dispState = 0.0f;
    d->bbdClock.chR.instantaneousClockHz = DIMENSION_BBD_NOMINAL_CLOCK_HZ_A4;
    d->bbdClock.chR.dispState = 0.0f;
}

static DimensionParams_phase_a4 dimension_default_params_a4(float sampleRate) {
    DimensionParams_phase_a4 p;
    p.sampleRate = (sampleRate > 0.0f) ? sampleRate : DIMENSION_SAMPLE_RATE_DEFAULT;
    p.mode = DIMENSION_MODE_I_phase_a4;
    p.quality = DIMENSION_QUALITY_BBD_LITE_phase_a4;
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

static inline void dimension_apply_mode_preset_a4(DimensionParams_phase_a4* p, DimensionMode_phase_a4 mode) {
    if (p == NULL) return;
    float rateHz = p->rateHz, baseDelayMs = p->baseDelayMs, depthMs = p->depthMs;
    float hpfHz = p->hpfHz, lpfHz = p->lpfHz, wetScale = 1.0f;
    float analogAmount = p->analogAmount, companderAmount = p->companderAmount;

    switch (mode) {
        case DIMENSION_MODE_I_phase_a4:
            rateHz = 0.25f; baseDelayMs = 7.0f; depthMs = 0.9f; hpfHz = 120.0f; lpfHz = 8000.0f; wetScale = 0.80f; analogAmount = 0.35f; companderAmount = 0.35f;
            break;
        case DIMENSION_MODE_II_phase_a4:
            rateHz = 0.25f; baseDelayMs = 8.5f; depthMs = 1.4f; hpfHz = 120.0f; lpfHz = 7500.0f; wetScale = 0.95f; analogAmount = 0.45f; companderAmount = 0.45f;
            break;
        case DIMENSION_MODE_III_phase_a4:
            rateHz = 0.50f; baseDelayMs = 10.0f; depthMs = 1.8f; hpfHz = 130.0f; lpfHz = 7000.0f; wetScale = 1.00f; analogAmount = 0.50f; companderAmount = 0.50f;
            break;
        case DIMENSION_MODE_IV_phase_a4:
            rateHz = 0.50f; baseDelayMs = 11.5f; depthMs = 2.4f; hpfHz = 140.0f; lpfHz = 6500.0f; wetScale = 1.10f; analogAmount = 0.60f; companderAmount = 0.60f;
            break;
        case DIMENSION_MODE_CUSTOM_phase_a4:
        default:
            break;
    }
    p->mode = mode;
    if (mode != DIMENSION_MODE_CUSTOM_phase_a4) {
        p->rateHz = rateHz; p->baseDelayMs = baseDelayMs; p->depthMs = depthMs;
        p->hpfHz = hpfHz; p->lpfHz = lpfHz;
        p->wetDirectGain = 0.50f * wetScale; p->wetCrossGain = 0.35f * wetScale;
        p->dryGain = 0.83f; p->analogAmount = analogAmount; p->companderAmount = companderAmount;
    }
}

static inline void dimension_sync_smoothers_a4(DimensionDSP_phase_a4* d) {
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

void Dimension_Init_phase_a4(DimensionDSP_phase_a4* d, float sampleRate) {
    if (!d) return;
    d->params = dimension_default_params_a4(sampleRate);
    dimension_apply_mode_preset_a4(&d->params, d->params.mode);
    dimension_clear_state_a4(d);
    const float sr = (d->params.sampleRate > 1000.0f) ? d->params.sampleRate : DIMENSION_SAMPLE_RATE_DEFAULT;
    d->smoothCoeff = 1.0f - expf(-1.0f / (DIMENSION_SMOOTH_TAU_SEC_A4 * sr));
    dimension_sync_smoothers_a4(d);
}

void Dimension_Reset_phase_a4(DimensionDSP_phase_a4* d) {
    if (!d) return;
    dimension_clear_state_a4(d);
    const float sr = (d->params.sampleRate > 1000.0f) ? d->params.sampleRate : DIMENSION_SAMPLE_RATE_DEFAULT;
    d->smoothCoeff = 1.0f - expf(-1.0f / (DIMENSION_SMOOTH_TAU_SEC_A4 * sr));
    dimension_sync_smoothers_a4(d);
}

void Dimension_SetMode_phase_a4(DimensionDSP_phase_a4* d, DimensionMode_phase_a4 mode) {
    if (!d) return;
    dimension_apply_mode_preset_a4(&d->params, mode);
}

void Dimension_SetParams_phase_a4(DimensionDSP_phase_a4* d, const DimensionParams_phase_a4* p) {
    if (!d || !p) return;
    d->params = *p;
    d->params.sampleRate = dimension_sanitize_param_a4(d->params.sampleRate, DIMENSION_SAMPLE_RATE_DEFAULT, 1000.0f, 192000.0f);
    d->params.inputGain = dimension_sanitize_param_a4(d->params.inputGain, 1.0f, 0.0f, 8.0f);
    d->params.outputGain = dimension_sanitize_param_a4(d->params.outputGain, 1.0f, 0.0f, 8.0f);
    d->params.dryGain = dimension_sanitize_param_a4(d->params.dryGain, 0.83f, 0.0f, 2.0f);
    d->params.wetDirectGain = dimension_sanitize_param_a4(d->params.wetDirectGain, 0.50f, 0.0f, 2.0f);
    d->params.wetCrossGain = dimension_sanitize_param_a4(d->params.wetCrossGain, 0.35f, 0.0f, 2.0f);
    d->params.baseDelayMs = dimension_sanitize_param_a4(d->params.baseDelayMs, 7.0f, 1.0f, DIMENSION_DELAY_MAX_MS);
    d->params.depthMs = dimension_sanitize_param_a4(d->params.depthMs, 0.9f, 0.0f, DIMENSION_DELAY_MAX_MS);
    d->params.rateHz = dimension_sanitize_param_a4(d->params.rateHz, 0.25f, 0.01f, 8.0f);
    d->params.hpfHz = dimension_sanitize_param_a4(d->params.hpfHz, 120.0f, 1.0f, 2000.0f);
    d->params.lpfHz = dimension_sanitize_param_a4(d->params.lpfHz, 8000.0f, 200.0f, 22000.0f);
    d->params.analogAmount = dimension_sanitize_param_a4(d->params.analogAmount, 0.35f, 0.0f, 1.0f);
    d->params.companderAmount = dimension_sanitize_param_a4(d->params.companderAmount, 0.35f, 0.0f, 1.0f);
    d->params.noiseAmount = dimension_sanitize_param_a4(d->params.noiseAmount, 0.0f, 0.0f, 1.0f);
    d->params.width = dimension_sanitize_param_a4(d->params.width, 1.0f, 0.0f, 2.0f);
    d->params.stereoWidth = dimension_sanitize_param_a4(d->params.stereoWidth, 1.0f, 0.0f, 2.0f);
    d->params.bassFocusHz = dimension_sanitize_param_a4(d->params.bassFocusHz, 0.0f, 0.0f, 500.0f);
    d->params.bbdCharacter = dimension_sanitize_param_a4(d->params.bbdCharacter, 0.0f, 0.0f, 1.0f);
    if (d->params.mode != DIMENSION_MODE_CUSTOM_phase_a4) {
        dimension_apply_mode_preset_a4(&d->params, d->params.mode);
    }
    const float sr = (d->params.sampleRate > 1000.0f) ? d->params.sampleRate : DIMENSION_SAMPLE_RATE_DEFAULT;
    d->smoothCoeff = 1.0f - expf(-1.0f / (DIMENSION_SMOOTH_TAU_SEC_A4 * sr));
}

void Dimension_GetParams_phase_a4(const DimensionDSP_phase_a4* d, DimensionParams_phase_a4* p) {
    if (d && p) *p = d->params;
}

void Dimension_ProcessBlock_phase_a4(
    DimensionDSP_phase_a4* d,
    const float* inL,
    const float* inR,
    float* outL,
    float* outR,
    uint32_t n) {
    if (!d || !inL || !inR || !outL || !outR || n == 0U) return;
    if (n > DIMENSION_MAX_BLOCK_SIZE) n = DIMENSION_MAX_BLOCK_SIZE;

    const float sr = (d->params.sampleRate > 1000.0f) ? d->params.sampleRate : DIMENSION_SAMPLE_RATE_DEFAULT;
    const float smoothCoeff = dimension_clampf_a4(d->smoothCoeff, 0.0f, 1.0f);
    const float maxDelaySpan = (float)(DIMENSION_DELAY_SIZE - 4U);
    const float invSr = 1.0f / sr;
    const float msToSamples = sr * 0.001f;
    const float atkC = expf(-1.0f / (0.005f * sr));
    const float relC = expf(-1.0f / (0.080f * sr));

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
    const float bassFocusAlpha = one_pole_alpha_a4(d->smoothBassFocusHz, sr);
    const float bassFocusAmount = d->smoothBassFocusAmount;

    const float hpfAlpha = one_pole_alpha_a4(d->smoothHpfHz, sr);
    const float lpfAlpha = one_pole_alpha_a4(d->smoothLpfHz, sr);
    const float analogAmount = dimension_clampf_a4(d->smoothAnalogAmount, 0.0f, 1.0f);
    const float preAlpha = one_pole_alpha_a4(3200.0f + 6400.0f * (1.0f - analogAmount), sr);

    const float gammaDelay = expf(-(float)n / (DIMENSION_SMOOTH_TAU_PREDICTOR_SEC_A4 * sr));
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

        const float inGain = dimension_clampf_a4(d->smoothInputGain, 0.0f, 8.0f);
        const float outGain = dimension_clampf_a4(d->smoothOutputGain, 0.0f, 8.0f);
        const float gDry = dimension_clampf_a4(d->smoothDryGain, 0.0f, 2.0f);
        const float width = dimension_clampf_a4(d->smoothWidth, 0.0f, 2.0f);
        const float stereoWidth = dimension_clampf_a4(d->smoothStereoWidth, 0.0f, 2.0f);
        const float bbdChar = dimension_clampf_a4(d->smoothBbdCharacter, 0.0f, 1.0f);

        float xL = dimension_sanitize_sample_a4(inL[i]) * inGain;
        float xR = dimension_sanitize_sample_a4(inR[i]) * inGain;
        const float dryL = xL;
        const float dryR = xR;
        float wetL = xL;
        float wetR = xR;

        const float gWet1 = dimension_clampf_a4(d->smoothWetDirectGain * width, 0.0f, 2.0f);
        const float gWet2 = dimension_clampf_a4(d->smoothWetCrossGain * width, 0.0f, 2.0f);
        const float rate = dimension_clampf_a4(d->smoothRateHz, 0.01f, 8.0f);
        const float lfoInc = rate * invSr;
        const float baseDelay = dimension_clampf_a4(d->smoothBaseDelayMs * msToSamples, 1.0f, maxDelaySpan - 1.0f);
        const float depthMax = fminf(baseDelay - 1.0f, maxDelaySpan - baseDelay);
        const float depthDelay = dimension_clampf_a4(d->smoothDepthMs * msToSamples, 0.0f, depthMax);
        const float amount = dimension_clampf_a4(d->smoothCompanderAmount, 0.0f, 1.0f);

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

        /* Phase A.4 static Pre-BBD filter */
        d->bbdStateL += preAlpha * (wetL - d->bbdStateL);
        d->bbdStateR += preAlpha * (wetR - d->bbdStateR);
        wetL = d->bbdStateL;
        wetR = d->bbdStateR;

        const float tri = (d->lfoPhase < 0.5f) ? (4.0f * d->lfoPhase - 1.0f) : (3.0f - 4.0f * d->lfoPhase);
        const float lfo = modulate_lfo_a4(tri, bbdChar);

        const float x = dimension_clampf_a4(curBbdDepthRatio * lfo, -0.75f, 0.75f);
        const float x2 = x * x;
        const float x4 = x2 * x2;
        const float factor = (1.0f + x2) * (1.0f + x4);
        const float invL = (1.0f - x) * factor;
        const float invR = (1.0f + x) * factor;
        float clockL = curBbdF0 * invL;
        float clockR = curBbdF0 * invR;
        clockL = dimension_clampf_a4(clockL, DIMENSION_BBD_MIN_CLOCK_HZ_A4, DIMENSION_BBD_MAX_CLOCK_HZ_A4);
        clockR = dimension_clampf_a4(clockR, DIMENSION_BBD_MIN_CLOCK_HZ_A4, DIMENSION_BBD_MAX_CLOCK_HZ_A4);
        d->bbdClock.chL.instantaneousClockHz = clockL;
        d->bbdClock.chR.instantaneousClockHz = clockR;

        const float transLagL = transport_lag_a4(clockL, curBbdF0, bbdChar);
        const float transLagR = transport_lag_a4(clockR, curBbdF0, bbdChar);
        const float delayL = baseDelay + depthDelay * lfo + transLagL;
        const float delayR = baseDelay - depthDelay * lfo + transLagR;
        const float readPosL = (float)d->writePos - delayL + (float)DIMENSION_DELAY_SIZE;
        const float readPosR = (float)d->writePos - delayR + (float)DIMENSION_DELAY_SIZE;

        d->delayL[d->writePos] = wetL;
        d->delayR[d->writePos] = wetR;

        wetL = delay_hermite_a4(d->delayL, readPosL);
        wetR = delay_hermite_a4(d->delayR, readPosR);

        wetL = process_dispersion_a4(&d->bbdClock.chL, wetL, clockL, sr, bbdChar);
        wetR = process_dispersion_a4(&d->bbdClock.chR, wetR, clockR, sr, bbdChar);

        curBbdF0 += deltaBbdF0;
        curBbdDepthRatio += deltaBbdRatio;

        /* Phase A.4 static 2-pole Reconstruction filter */
        d->lpf1StateL += lpfAlpha * (wetL - d->lpf1StateL);
        d->lpf1StateR += lpfAlpha * (wetR - d->lpf1StateR);
        d->lpf2StateL += lpfAlpha * (d->lpf1StateL - d->lpf2StateL);
        d->lpf2StateR += lpfAlpha * (d->lpf1StateR - d->lpf2StateR);
        wetL = d->lpf2StateL;
        wetR = d->lpf2StateR;

        wetL = softclip_cubic_a4(wetL);
        wetR = softclip_cubic_a4(wetR);

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

        if (stereoWidth != 1.0f || bassFocusAmount > 0.0f) {
            const float mid = 0.5f * (yL + yR);
            const float side = 0.5f * (yL - yR);
            d->bassFocusState += bassFocusAlpha * (side - d->bassFocusState);
            if (fabsf(d->bassFocusState) < 1.0e-15f) d->bassFocusState = 0.0f;
            const float sideFiltered = side - bassFocusAmount * d->bassFocusState;
            const float processedSide = sideFiltered * stereoWidth;
            yL = mid + processedSide;
            yR = mid - processedSide;
        }

        yL *= outGain;
        yR *= outGain;
#if DIMENSION_ENABLE_SAFETY_LIMITER
        yL = dimension_clampf_a4(yL, -1.2f, 1.2f);
        yR = dimension_clampf_a4(yR, -1.2f, 1.2f);
#endif
        outL[i] = dimension_sanitize_sample_a4(yL);
        outR[i] = dimension_sanitize_sample_a4(yR);

        d->writePos = (d->writePos + 1U) & DIMENSION_DELAY_MASK;
        d->lfoPhase += lfoInc;
        if (d->lfoPhase >= 1.0f) d->lfoPhase -= 1.0f;
    }
}
