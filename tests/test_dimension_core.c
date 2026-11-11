#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/dimension_dsp.h"

#define TEST_SR 48000.0f
#define BLOCK 64U
#define PI_F 3.14159265358979323846f
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "CHECK failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); return 1; } } while (0)

static float frand_uniform(uint32_t* state) {
    *state = (*state * 1664525u) + 1013904223u;
    return ((float)((*state >> 8) & 0x00FFFFFFu) / 8388608.0f) - 1.0f;
}

static int process_constant(DimensionDSP* d, float value, uint32_t total, float* peak, float* dc_acc) {
    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    uint32_t rem = total;
    while (rem > 0U) {
        const uint32_t n = (rem > BLOCK) ? BLOCK : rem;
        for (uint32_t i = 0; i < n; ++i) inL[i] = inR[i] = value;
        Dimension_ProcessBlock(d, inL, inR, outL, outR, n);
        for (uint32_t i = 0; i < n; ++i) {
            CHECK(isfinite(outL[i]) && isfinite(outR[i]));
            const float a = fabsf(outL[i]);
            const float b = fabsf(outR[i]);
            if (a > *peak) *peak = a;
            if (b > *peak) *peak = b;
            *dc_acc += 0.5f * (outL[i] + outR[i]);
        }
        rem -= n;
    }
    return 0;
}

static int test_zero_input_long_no_nan_low_dc(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    Dimension_SetMode(&d, DIMENSION_MODE_II);
    float peak = 0.0f, dc = 0.0f;
    const uint32_t total = (uint32_t)TEST_SR * 8U;
    CHECK(process_constant(&d, 0.0f, total, &peak, &dc) == 0);
    dc /= (float)total;
    CHECK(fabsf(dc) < 1e-4f);
    CHECK(peak < 1e-6f);
    return 0;
}

static int test_impulse_delay_correct(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    DimensionParams p;
    Dimension_GetParams(&d, &p);
    p.mode = DIMENSION_MODE_CUSTOM;
    p.depthMs = 0.0f;
    p.baseDelayMs = 10.0f;
    p.analogAmount = 0.0f;
    p.companderAmount = 0.0f;
    p.dryGain = 0.0f;
    p.wetDirectGain = 1.0f;
    p.wetCrossGain = 0.0f;
    Dimension_SetParams(&d, &p);
    Dimension_Reset(&d);

    const uint32_t expected = (uint32_t)(p.baseDelayMs * 0.001f * TEST_SR);
    float inL[BLOCK] = {0}, inR[BLOCK] = {0}, outL[BLOCK] = {0}, outR[BLOCK] = {0};
    uint32_t idx = 0xFFFFFFFFu;
    uint32_t t = 0U;

    inL[0] = 1.0f;
    while (t < expected + 128U) {
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
        memset(inL, 0, sizeof(inL));
        memset(inR, 0, sizeof(inR));
        for (uint32_t i = 0; i < BLOCK; ++i) {
            if (fabsf(outL[i]) > 0.2f) {
                idx = t + i;
                t = expected + 128U;
                break;
            }
        }
        t += BLOCK;
    }
    CHECK(idx != 0xFFFFFFFFu);
    CHECK((int32_t)idx >= (int32_t)expected - 8);
    CHECK((int32_t)idx <= (int32_t)expected + 24);
    return 0;
}

static int test_sine_1khz_low_distortion_analog_zero(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    DimensionParams p;
    Dimension_GetParams(&d, &p);
    p.mode = DIMENSION_MODE_CUSTOM;
    p.analogAmount = 0.0f;
    p.companderAmount = 0.0f;
    Dimension_SetParams(&d, &p);

    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    const uint32_t total = (uint32_t)TEST_SR * 2U;
    double ein = 0.0, eerr = 0.0;
    for (uint32_t n = 0; n < total; n += BLOCK) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            const float s = 0.25f * sinf(2.0f * PI_F * 1000.0f * (float)(n + i) / TEST_SR);
            inL[i] = inR[i] = s;
        }
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
        for (uint32_t i = 0; i < BLOCK; ++i) {
            const float o = 0.5f * (outL[i] + outR[i]);
            const float e = o - inL[i];
            ein += (double)inL[i] * (double)inL[i];
            eerr += (double)e * (double)e;
        }
    }
    const double nrmse = sqrt(eerr / (ein + DBL_MIN));
    CHECK(nrmse < 0.35);
    return 0;
}

static int test_sine_100hz_center_preserved(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    const uint32_t total = (uint32_t)TEST_SR * 2U;
    double inMid = 0.0, outMid = 0.0;
    for (uint32_t n = 0; n < total; n += BLOCK) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            const float s = 0.25f * sinf(2.0f * PI_F * 100.0f * (float)(n + i) / TEST_SR);
            inL[i] = inR[i] = s;
        }
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
        for (uint32_t i = 0; i < BLOCK; ++i) {
            inMid += (double)inL[i] * (double)inL[i];
            const float m = 0.5f * (outL[i] + outR[i]);
            outMid += (double)m * (double)m;
        }
    }
    const double ratio = sqrt(outMid / (inMid + DBL_MIN));
    CHECK(ratio > 0.55 && ratio < 1.25);
    return 0;
}

static int test_input_plusminus2_safe_saturation(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    float peak = 0.0f, dc = 0.0f;
    CHECK(process_constant(&d, 2.0f, TEST_SR, &peak, &dc) == 0);
    CHECK(process_constant(&d, -2.0f, TEST_SR, &peak, &dc) == 0);
    CHECK(peak <= 2.0f);
    return 0;
}

static float run_mode_switch_step(bool switchMode) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    const uint32_t total = (uint32_t)TEST_SR * 6U;
    float prevL = 0.0f, prevR = 0.0f;
    float maxStep = 0.0f;
    bool first = true;

    for (uint32_t n = 0; n < total; n += BLOCK) {
        if (switchMode) {
            const uint32_t sec = n / (uint32_t)TEST_SR;
            Dimension_SetMode(&d, (DimensionMode)(sec % 4U));
        }
        for (uint32_t i = 0; i < BLOCK; ++i) {
            const float s = 0.1f * sinf(2.0f * PI_F * 100.0f * (float)(n + i) / TEST_SR);
            inL[i] = inR[i] = s;
        }
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
        for (uint32_t i = 0; i < BLOCK; ++i) {
            if (!first) {
                const float dL = fabsf(outL[i] - prevL);
                const float dR = fabsf(outR[i] - prevR);
                if (dL > maxStep) maxStep = dL;
                if (dR > maxStep) maxStep = dR;
            }
            prevL = outL[i];
            prevR = outR[i];
            first = false;
        }
    }
    return maxStep;
}

static int test_mode_switch_1s_no_clicks(void) {
    const float baselineMaxStep = run_mode_switch_step(false);
    const float switchedMaxStep = run_mode_switch_step(true);

    CHECK(switchedMaxStep < 0.01f);
    CHECK((switchedMaxStep - baselineMaxStep) < 0.003f);
    return 0;
}

static int test_noise_stability(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    uint32_t rng = 0x12345678u;
    const uint32_t total = (uint32_t)TEST_SR * 3U;
    float peak = 0.0f;
    for (uint32_t n = 0; n < total; n += BLOCK) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            inL[i] = 0.35f * frand_uniform(&rng);
            inR[i] = 0.35f * frand_uniform(&rng);
        }
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
        for (uint32_t i = 0; i < BLOCK; ++i) {
            CHECK(isfinite(outL[i]) && isfinite(outR[i]));
            float a = fabsf(outL[i]); if (a > peak) peak = a;
            float b = fabsf(outR[i]); if (b > peak) peak = b;
        }
    }
    CHECK(peak < 2.0f);
    return 0;
}

static int test_sample_rates_and_block_limits(void) {
    const float rates[] = {44100.0f, 48000.0f, 96000.0f};
    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    for (uint32_t r = 0; r < 3U; ++r) {
        DimensionDSP d;
        Dimension_Init(&d, rates[r]);
        for (uint32_t mode = 0; mode < 4U; ++mode) {
            Dimension_SetMode(&d, (DimensionMode)mode);
            for (uint32_t i = 0; i < BLOCK; ++i) {
                inL[i] = 0.2f * sinf(2.0f * PI_F * 440.0f * (float)i / rates[r]);
                inR[i] = -inL[i];
            }
            Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
            for (uint32_t i = 0; i < BLOCK; ++i) CHECK(isfinite(outL[i]) && isfinite(outR[i]));
        }
    }
    return 0;
}

static double deterministic_energy(uint32_t seed) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    Dimension_SetMode(&d, DIMENSION_MODE_IV);
    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    uint32_t rng = seed;
    double energy = 0.0;
    for (uint32_t n = 0; n < TEST_SR; n += BLOCK) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            inL[i] = 0.25f * frand_uniform(&rng);
            inR[i] = 0.25f * frand_uniform(&rng);
        }
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
        for (uint32_t i = 0; i < BLOCK; ++i) energy += (double)outL[i] * (double)outL[i] + (double)outR[i] * (double)outR[i];
    }
    return energy;
}

static int test_determinism_between_runs(void) {
    const double a = deterministic_energy(0xabcdef01u);
    const double b = deterministic_energy(0xabcdef01u);
    CHECK(fabs(a - b) < 1e-9);
    return 0;
}

static int test_extreme_and_nonfinite_params_are_clamped(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    DimensionParams p;
    Dimension_GetParams(&d, &p);
    p.mode = DIMENSION_MODE_CUSTOM;
    p.sampleRate = NAN;
    p.inputGain = INFINITY;
    p.outputGain = INFINITY;
    p.baseDelayMs = -1000.0f;
    p.depthMs = INFINITY;
    p.rateHz = NAN;
    p.hpfHz = -1.0f;
    p.lpfHz = INFINITY;
    p.analogAmount = NAN;
    p.companderAmount = INFINITY;
    p.width = INFINITY;
    Dimension_SetParams(&d, &p);
    Dimension_GetParams(&d, &p);
    CHECK(isfinite(p.sampleRate) && p.sampleRate == DIMENSION_SAMPLE_RATE_DEFAULT);
    CHECK(p.baseDelayMs >= 1.0f && p.baseDelayMs <= DIMENSION_DELAY_MAX_MS);
    CHECK(p.depthMs >= 0.0f && p.depthMs <= DIMENSION_DELAY_MAX_MS);
    float peak = 0.0f, dc = 0.0f;
    CHECK(process_constant(&d, 0.5f, TEST_SR / 10U, &peak, &dc) == 0);
    CHECK(isfinite(peak));
    return 0;
}

static int test_regression_signal_metrics(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    uint32_t rng = 0x31415926u;
    double sum = 0.0, energy = 0.0;
    float peak = 0.0f;
    const uint32_t total = TEST_SR * 2U;
    for (uint32_t n = 0; n < total; n += BLOCK) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            const uint32_t t = n + i;
            float x = 0.12f * sinf(2.0f * PI_F * 330.0f * (float)t / TEST_SR) + 0.03f * frand_uniform(&rng);
            if (t == 200U) x += 0.5f;
            inL[i] = x;
            inR[i] = x;
        }
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
        for (uint32_t i = 0; i < BLOCK; ++i) {
            const float m = 0.5f * (outL[i] + outR[i]);
            const float a = fabsf(m);
            if (a > peak) peak = a;
            sum += (double)m;
            energy += (double)m * (double)m;
        }
    }
    const double dc = sum / (double)total;
    const double rms = sqrt(energy / (double)total);
    CHECK(peak > 0.02f && peak < 1.0f);
    CHECK(fabs(dc) < 0.01);
    CHECK(rms > 0.02 && rms < 0.30);
    return 0;
}

static int test_parameter_schema_is_stable(void) {
    CHECK(DIMENSION_PARAM_SCHEMA_VERSION == 3U);
    CHECK(DIMENSION_PARAM_COUNT == 16);
    CHECK(DIMENSION_PARAM_INPUT_GAIN == 0);
    CHECK(DIMENSION_PARAM_WIDTH == 12);
    CHECK(DIMENSION_PARAM_STEREO_WIDTH == 13);
    CHECK(DIMENSION_PARAM_BASS_FOCUS_HZ == 14);
    CHECK(DIMENSION_PARAM_BBD_CHARACTER == 15);

    for (uint32_t i = 0U; i < (uint32_t)DIMENSION_PARAM_COUNT; ++i) {
        const DimensionParamDescriptor* desc = Dimension_GetParamDescriptor((DimensionParamId)i);
        CHECK(desc != NULL);
        CHECK((uint32_t)desc->id == i);
        CHECK(desc->stableId != NULL);
        CHECK(desc->displayName != NULL);
        CHECK(desc->minValue <= desc->defaultValue);
        CHECK(desc->defaultValue <= desc->maxValue);
        CHECK(desc->automatable == 1U);
    }

    CHECK(Dimension_GetParamDescriptor((DimensionParamId)DIMENSION_PARAM_COUNT) == NULL);
    return 0;
}

static int test_block_size_smoothing_invariance(void) {
    DimensionDSP d16, d64, dirreg;
    Dimension_Init(&d16, TEST_SR);
    Dimension_Init(&d64, TEST_SR);
    Dimension_Init(&dirreg, TEST_SR);

    DimensionParams p;
    Dimension_GetParams(&d16, &p);
    p.mode = DIMENSION_MODE_CUSTOM;
    p.hpfHz = 350.0f;
    p.lpfHz = 3000.0f;
    p.analogAmount = 0.90f;

    Dimension_SetParams(&d16, &p);
    Dimension_SetParams(&d64, &p);
    Dimension_SetParams(&dirreg, &p);

    float inL[64] = {0}, inR[64] = {0}, outL[64], outR[64];
    for (uint32_t i = 0; i < 64; ++i) {
        inL[i] = inR[i] = 0.1f;
    }

    /* Process 1440 samples (30 ms = tau) */
    /* d16: 90 blocks of 16 */
    for (int b = 0; b < 90; ++b) {
        Dimension_ProcessBlock(&d16, inL, inR, outL, outR, 16);
    }
    /* d64: 22 blocks of 64 + 1 block of 32 = 1440 */
    for (int b = 0; b < 22; ++b) {
        Dimension_ProcessBlock(&d64, inL, inR, outL, outR, 64);
    }
    Dimension_ProcessBlock(&d64, inL, inR, outL, outR, 32);

    /* dirreg: irregular block sizes summing to 1440 */
    uint32_t irrSum = 0;
    while (irrSum < 1440U) {
        const uint32_t rem = 1440U - irrSum;
        const uint32_t chunk = (rem > 64U) ? ((rem % 3U == 0U) ? 27U : ((rem % 2U == 0U) ? 32U : 19U)) : rem;
        Dimension_ProcessBlock(&dirreg, inL, inR, outL, outR, chunk);
        irrSum += chunk;
    }
    CHECK(irrSum == 1440U);

    /* At t = 30 ms (tau = 30 ms), expected progress is 1 - 1/e ~ 0.63212.
     * hpf: 120 -> 350. Target diff = 230. Expected = 120 + 230 * 0.63212 = 265.4 Hz. */
    const float expectedHpfAtTau = 120.0f + 230.0f * (1.0f - 1.0f / 2.718281828f);
    CHECK(fabsf(d16.smoothHpfHz - expectedHpfAtTau) < 3.0f);
    CHECK(fabsf(d64.smoothHpfHz - expectedHpfAtTau) < 3.0f);
    CHECK(fabsf(dirreg.smoothHpfHz - expectedHpfAtTau) < 3.0f);

    /* Across blocks of 16 vs 64 vs irregular at 30 ms, difference must be minimal (< 1 Hz) */
    CHECK(fabsf(d16.smoothHpfHz - d64.smoothHpfHz) < 1.0f);
    CHECK(fabsf(d16.smoothHpfHz - dirreg.smoothHpfHz) < 1.0f);
    CHECK(fabsf(d16.smoothLpfHz - d64.smoothLpfHz) < 15.0f);
    CHECK(fabsf(d16.smoothAnalogAmount - d64.smoothAnalogAmount) < 0.005f);

    /* Process further to 100 ms (total 4800 frames) */
    for (int b = 0; b < 210; ++b) {
        Dimension_ProcessBlock(&d16, inL, inR, outL, outR, 16);
    }
    for (int b = 0; b < 52; ++b) {
        Dimension_ProcessBlock(&d64, inL, inR, outL, outR, 64);
    }
    Dimension_ProcessBlock(&d64, inL, inR, outL, outR, 32);

    CHECK(fabsf(d16.smoothHpfHz - d64.smoothHpfHz) < 0.5f);
    CHECK(fabsf(d16.smoothLpfHz - d64.smoothLpfHz) < 5.0f);
    CHECK(fabsf(d16.smoothAnalogAmount - d64.smoothAnalogAmount) < 0.002f);
    return 0;
}

static float measure_max_sample_delta(DimensionDSP* d, uint32_t numSamples, uint32_t startSampleIdx) {
    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    float prevL = 0.0f, prevR = 0.0f;
    float maxDelta = 0.0f;
    bool first = true;

    for (uint32_t n = 0; n < numSamples; n += BLOCK) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            const float s = 0.2f * sinf(2.0f * PI_F * 1000.0f * (float)(startSampleIdx + n + i) / TEST_SR);
            inL[i] = inR[i] = s;
        }
        Dimension_ProcessBlock(d, inL, inR, outL, outR, BLOCK);
        for (uint32_t i = 0; i < BLOCK; ++i) {
            if (!first) {
                const float dL = fabsf(outL[i] - prevL);
                const float dR = fabsf(outR[i] - prevR);
                if (dL > maxDelta) maxDelta = dL;
                if (dR > maxDelta) maxDelta = dR;
            }
            prevL = outL[i];
            prevR = outR[i];
            first = false;
        }
    }
    return maxDelta;
}

static int test_smoothed_controls_transitions(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);

    /* Verify smoothers start at exact target with zero initial ramp */
    CHECK(fabsf(d.smoothInputGain - 1.0f) < 1e-6f);
    CHECK(fabsf(d.smoothOutputGain - 1.0f) < 1e-6f);
    CHECK(fabsf(d.smoothDryGain - 0.83f) < 1e-6f);
    CHECK(fabsf(d.smoothWidth - 1.0f) < 1e-6f);

    /* Test 1: inputGain transition (1.0 -> 2.5) */
    {
        DimensionParams pBase;
        Dimension_GetParams(&d, &pBase);
        pBase.mode = DIMENSION_MODE_CUSTOM;
        pBase.inputGain = 2.5f;

        /* Static baseline: parameters and smoothers already aligned at target */
        DimensionDSP dBase;
        Dimension_Init(&dBase, TEST_SR);
        Dimension_SetParams(&dBase, &pBase);
        Dimension_Reset(&dBase);
        measure_max_sample_delta(&dBase, 9600, 0); /* Stabilize steady state */
        const float baseMaxDelta = measure_max_sample_delta(&dBase, 9600, 9600);

        /* Transition run: first stabilize at initial configuration, then transition */
        DimensionDSP dTrans;
        Dimension_Init(&dTrans, TEST_SR);
        const float initialMaxDelta = measure_max_sample_delta(&dTrans, 9600, 0); /* Stabilize initial state */
        Dimension_SetParams(&dTrans, &pBase); /* Apply change */
        const float transMaxDelta = measure_max_sample_delta(&dTrans, 9600, 9600);
        const float naturalDelta = (initialMaxDelta > baseMaxDelta) ? initialMaxDelta : baseMaxDelta;
        CHECK((transMaxDelta - naturalDelta) < 0.005f);
        CHECK(fabsf(dTrans.smoothInputGain - 2.5f) < 0.01f);
    }

    /* Test 2: dryGain transition (0.83 -> 0.20) */
    {
        DimensionParams pBase;
        Dimension_GetParams(&d, &pBase);
        pBase.mode = DIMENSION_MODE_CUSTOM;
        pBase.dryGain = 0.20f;

        DimensionDSP dBase;
        Dimension_Init(&dBase, TEST_SR);
        Dimension_SetParams(&dBase, &pBase);
        Dimension_Reset(&dBase);
        measure_max_sample_delta(&dBase, 9600, 0);
        const float baseMaxDelta = measure_max_sample_delta(&dBase, 9600, 9600);

        DimensionDSP dTrans;
        Dimension_Init(&dTrans, TEST_SR);
        const float initialMaxDelta = measure_max_sample_delta(&dTrans, 9600, 0);
        Dimension_SetParams(&dTrans, &pBase);
        const float transMaxDelta = measure_max_sample_delta(&dTrans, 9600, 9600);
        const float naturalDelta = (initialMaxDelta > baseMaxDelta) ? initialMaxDelta : baseMaxDelta;
        CHECK((transMaxDelta - naturalDelta) < 0.005f);
        CHECK(fabsf(dTrans.smoothDryGain - 0.20f) < 0.01f);
    }

    /* Test 3: width transition (1.0 -> 1.8) */
    {
        DimensionParams pBase;
        Dimension_GetParams(&d, &pBase);
        pBase.mode = DIMENSION_MODE_CUSTOM;
        pBase.width = 1.8f;

        DimensionDSP dBase;
        Dimension_Init(&dBase, TEST_SR);
        Dimension_SetParams(&dBase, &pBase);
        Dimension_Reset(&dBase);
        measure_max_sample_delta(&dBase, 9600, 0);
        const float baseMaxDelta = measure_max_sample_delta(&dBase, 9600, 9600);

        DimensionDSP dTrans;
        Dimension_Init(&dTrans, TEST_SR);
        const float initialMaxDelta = measure_max_sample_delta(&dTrans, 9600, 0);
        Dimension_SetParams(&dTrans, &pBase);
        const float transMaxDelta = measure_max_sample_delta(&dTrans, 9600, 9600);
        const float naturalDelta = (initialMaxDelta > baseMaxDelta) ? initialMaxDelta : baseMaxDelta;
        CHECK((transMaxDelta - naturalDelta) < 0.005f);
        CHECK(fabsf(dTrans.smoothWidth - 1.8f) < 0.01f);
    }

    /* Test 4: multi-parameter transition */
    {
        DimensionParams pTarget;
        Dimension_GetParams(&d, &pTarget);
        pTarget.mode = DIMENSION_MODE_CUSTOM;
        pTarget.inputGain = 2.5f;
        pTarget.outputGain = 0.5f;
        pTarget.dryGain = 0.3f;
        pTarget.width = 1.8f;
        pTarget.wetDirectGain = 0.7f;
        pTarget.wetCrossGain = 0.5f;

        DimensionDSP dBase;
        Dimension_Init(&dBase, TEST_SR);
        Dimension_SetParams(&dBase, &pTarget);
        Dimension_Reset(&dBase);
        measure_max_sample_delta(&dBase, 9600, 0);
        const float baseMaxDelta = measure_max_sample_delta(&dBase, 9600, 9600);

        DimensionDSP dTrans;
        Dimension_Init(&dTrans, TEST_SR);
        const float initialMaxDelta = measure_max_sample_delta(&dTrans, 9600, 0);
        Dimension_SetParams(&dTrans, &pTarget);
        const float transMaxDelta = measure_max_sample_delta(&dTrans, 9600, 9600);
        const float naturalDelta = (initialMaxDelta > baseMaxDelta) ? initialMaxDelta : baseMaxDelta;
        CHECK((transMaxDelta - naturalDelta) < 0.005f);
        CHECK(fabsf(dTrans.smoothInputGain - 2.5f) < 0.01f);
        CHECK(fabsf(dTrans.smoothOutputGain - 0.5f) < 0.01f);
        CHECK(fabsf(dTrans.smoothDryGain - 0.3f) < 0.01f);
        CHECK(fabsf(dTrans.smoothWidth - 1.8f) < 0.01f);
    }

    return 0;
}

static int test_mode_to_custom_preserves_audio_and_params(void) {
    /* Test Mode II transition to Custom */
    DimensionDSP dMode, dCustom;
    Dimension_Init(&dMode, TEST_SR);
    Dimension_SetMode(&dMode, DIMENSION_MODE_II);

    /* Run Mode II for 500 ms */
    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    for (uint32_t n = 0; n < (uint32_t)TEST_SR / 2U; n += BLOCK) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            inL[i] = inR[i] = 0.15f * sinf(2.0f * PI_F * 220.0f * (float)(n + i) / TEST_SR);
        }
        Dimension_ProcessBlock(&dMode, inL, inR, outL, outR, BLOCK);
    }

    /* Clone state to dCustom, then switch to Custom with Mode II's exact parameters */
    dCustom = dMode;
    DimensionParams p;
    Dimension_GetParams(&dCustom, &p);
    CHECK(p.mode == DIMENSION_MODE_II);
    CHECK(fabsf(p.baseDelayMs - 8.5f) < 1e-4f);
    CHECK(fabsf(p.depthMs - 1.4f) < 1e-4f);
    CHECK(fabsf(p.lpfHz - 7500.0f) < 1e-4f);

    p.mode = DIMENSION_MODE_CUSTOM;
    Dimension_SetParams(&dCustom, &p);

    /* Process the next block through both and compare outputs */
    float outLMode[BLOCK], outRMode[BLOCK];
    float outLCustom[BLOCK], outRCustom[BLOCK];
    for (uint32_t i = 0; i < BLOCK; ++i) {
        inL[i] = inR[i] = 0.15f * sinf(2.0f * PI_F * 220.0f * (float)(TEST_SR / 2U + i) / TEST_SR);
    }
    Dimension_ProcessBlock(&dMode, inL, inR, outLMode, outRMode, BLOCK);
    Dimension_ProcessBlock(&dCustom, inL, inR, outLCustom, outRCustom, BLOCK);

    for (uint32_t i = 0; i < BLOCK; ++i) {
        CHECK(fabsf(outLMode[i] - outLCustom[i]) < 1e-5f);
        CHECK(fabsf(outRMode[i] - outRCustom[i]) < 1e-5f);
    }

    /* Repeat verification for Mode IV */
    DimensionDSP dMode4, dCustom4;
    Dimension_Init(&dMode4, TEST_SR);
    Dimension_SetMode(&dMode4, DIMENSION_MODE_IV);
    for (uint32_t n = 0; n < (uint32_t)TEST_SR / 2U; n += BLOCK) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            inL[i] = inR[i] = 0.15f * sinf(2.0f * PI_F * 220.0f * (float)(n + i) / TEST_SR);
        }
        Dimension_ProcessBlock(&dMode4, inL, inR, outL, outR, BLOCK);
    }
    dCustom4 = dMode4;
    Dimension_GetParams(&dCustom4, &p);
    CHECK(p.mode == DIMENSION_MODE_IV);
    CHECK(fabsf(p.baseDelayMs - 11.5f) < 1e-4f);
    CHECK(fabsf(p.depthMs - 2.4f) < 1e-4f);
    CHECK(fabsf(p.lpfHz - 6500.0f) < 1e-4f);
    p.mode = DIMENSION_MODE_CUSTOM;
    Dimension_SetParams(&dCustom4, &p);
    Dimension_ProcessBlock(&dMode4, inL, inR, outLMode, outRMode, BLOCK);
    Dimension_ProcessBlock(&dCustom4, inL, inR, outLCustom, outRCustom, BLOCK);
    for (uint32_t i = 0; i < BLOCK; ++i) {
        CHECK(fabsf(outLMode[i] - outLCustom[i]) < 1e-5f);
        CHECK(fabsf(outRMode[i] - outRCustom[i]) < 1e-5f);
    }
    return 0;
}

static int test_mono_in_stereo_out_decorrelation(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    Dimension_SetMode(&d, DIMENSION_MODE_I);

    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    double diffEnergy = 0.0;
    const uint32_t total = (uint32_t)TEST_SR * 2U;

    /* Feed identical mono signal into both channels */
    for (uint32_t n = 0; n < total; n += BLOCK) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            const float s = 0.2f * sinf(2.0f * PI_F * 440.0f * (float)(n + i) / TEST_SR);
            inL[i] = inR[i] = s;
        }
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
        for (uint32_t i = 0; i < BLOCK; ++i) {
            const float diff = outL[i] - outR[i];
            diffEnergy += (double)diff * (double)diff;
        }
    }

    /* Verify that left and right outputs are distinct (stereo widening active) */
    CHECK(diffEnergy > 0.01);
    return 0;
}

static int test_spatial_engine_neutrality(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    Dimension_SetMode(&d, DIMENSION_MODE_I);

    DimensionParams p = d.params;
    CHECK(p.stereoWidth == 1.0f);
    CHECK(p.bassFocusHz == 0.0f);

    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    for (uint32_t i = 0; i < BLOCK; ++i) {
        inL[i] = 0.1f * sinf(2.0f * PI_F * 440.0f * (float)i / TEST_SR);
        inR[i] = 0.05f * cosf(2.0f * PI_F * 880.0f * (float)i / TEST_SR);
    }
    Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
    for (uint32_t i = 0; i < BLOCK; ++i) {
        CHECK(isfinite(outL[i]) && isfinite(outR[i]));
    }
    return 0;
}

static int test_spatial_engine_side_control(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    Dimension_SetMode(&d, DIMENSION_MODE_I);
    DimensionParams p;
    Dimension_GetParams(&d, &p);
    p.stereoWidth = 0.0f;
    Dimension_SetParams(&d, &p);
    Dimension_Reset(&d);

    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    for (uint32_t n = 0; n < 4800; n += BLOCK) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            inL[i] = 0.15f * sinf(2.0f * PI_F * 440.0f * (float)(n + i) / TEST_SR);
            inR[i] = 0.10f * cosf(2.0f * PI_F * 330.0f * (float)(n + i) / TEST_SR);
        }
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
        for (uint32_t i = 0; i < BLOCK; ++i) {
            const float side = 0.5f * (outL[i] - outR[i]);
            CHECK(fabsf(side) < 1e-4f);
        }
    }

    DimensionDSP d1, d2;
    Dimension_Init(&d1, TEST_SR);
    Dimension_SetMode(&d1, DIMENSION_MODE_I);
    p.stereoWidth = 1.0f;
    Dimension_SetParams(&d1, &p);
    Dimension_Reset(&d1);

    Dimension_Init(&d2, TEST_SR);
    Dimension_SetMode(&d2, DIMENSION_MODE_I);
    p.stereoWidth = 2.0f;
    Dimension_SetParams(&d2, &p);
    Dimension_Reset(&d2);

    double sideEnergy1 = 0.0, sideEnergy2 = 0.0;
    for (uint32_t n = 0; n < 4800; n += BLOCK) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            inL[i] = 0.15f * sinf(2.0f * PI_F * 440.0f * (float)(n + i) / TEST_SR);
            inR[i] = 0.10f * cosf(2.0f * PI_F * 330.0f * (float)(n + i) / TEST_SR);
        }
        float outL1[BLOCK], outR1[BLOCK], outL2[BLOCK], outR2[BLOCK];
        Dimension_ProcessBlock(&d1, inL, inR, outL1, outR1, BLOCK);
        Dimension_ProcessBlock(&d2, inL, inR, outL2, outR2, BLOCK);
        for (uint32_t i = 0; i < BLOCK; ++i) {
            const float side1 = 0.5f * (outL1[i] - outR1[i]);
            const float side2 = 0.5f * (outL2[i] - outR2[i]);
            sideEnergy1 += (double)side1 * (double)side1;
            sideEnergy2 += (double)side2 * (double)side2;
        }
    }
    CHECK(sideEnergy1 > 0.001);
    const double ratio = sideEnergy2 / sideEnergy1;
    CHECK(ratio > 3.9 && ratio < 4.1);
    return 0;
}

static int test_spatial_engine_mid_integrity(void) {
    DimensionDSP dRef, dWidth0, dWidth2, dBassFocus;
    Dimension_Init(&dRef, TEST_SR);
    Dimension_SetMode(&dRef, DIMENSION_MODE_II);

    Dimension_Init(&dWidth0, TEST_SR);
    Dimension_SetMode(&dWidth0, DIMENSION_MODE_II);
    DimensionParams p;
    Dimension_GetParams(&dWidth0, &p);
    p.stereoWidth = 0.0f;
    Dimension_SetParams(&dWidth0, &p);

    Dimension_Init(&dWidth2, TEST_SR);
    Dimension_SetMode(&dWidth2, DIMENSION_MODE_II);
    p.stereoWidth = 2.0f;
    Dimension_SetParams(&dWidth2, &p);

    Dimension_Init(&dBassFocus, TEST_SR);
    Dimension_SetMode(&dBassFocus, DIMENSION_MODE_II);
    p.stereoWidth = 1.5f;
    p.bassFocusHz = 150.0f;
    Dimension_SetParams(&dBassFocus, &p);

    float inL[BLOCK], inR[BLOCK];
    float oLRef[BLOCK], oRRef[BLOCK];
    float oL0[BLOCK], oR0[BLOCK];
    float oL2[BLOCK], oR2[BLOCK];
    float oLBF[BLOCK], oRBF[BLOCK];

    for (uint32_t n = 0; n < 4800; n += BLOCK) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            inL[i] = 0.12f * sinf(2.0f * PI_F * 500.0f * (float)(n + i) / TEST_SR);
            inR[i] = 0.08f * cosf(2.0f * PI_F * 750.0f * (float)(n + i) / TEST_SR);
        }
        Dimension_ProcessBlock(&dRef, inL, inR, oLRef, oRRef, BLOCK);
        Dimension_ProcessBlock(&dWidth0, inL, inR, oL0, oR0, BLOCK);
        Dimension_ProcessBlock(&dWidth2, inL, inR, oL2, oR2, BLOCK);
        Dimension_ProcessBlock(&dBassFocus, inL, inR, oLBF, oRBF, BLOCK);

        for (uint32_t i = 0; i < BLOCK; ++i) {
            const float midRef = 0.5f * (oLRef[i] + oRRef[i]);
            const float mid0   = 0.5f * (oL0[i] + oR0[i]);
            const float mid2   = 0.5f * (oL2[i] + oR2[i]);
            const float midBF  = 0.5f * (oLBF[i] + oRBF[i]);
            CHECK(fabsf(midRef - mid0) < 1e-5f);
            CHECK(fabsf(midRef - mid2) < 1e-5f);
            CHECK(fabsf(midRef - midBF) < 1e-5f);
        }
    }
    return 0;
}

static int test_spatial_engine_bass_focus_response(void) {
    const float freqs[] = { 40.0f, 60.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 5000.0f };
    const size_t numFreqs = sizeof(freqs) / sizeof(freqs[0]);

    for (size_t f = 0; f < numFreqs; ++f) {
        const float freq = freqs[f];
        DimensionDSP dOff, dOn;
        Dimension_Init(&dOff, TEST_SR);
        Dimension_SetMode(&dOff, DIMENSION_MODE_CUSTOM);
        DimensionParams p;
        Dimension_GetParams(&dOff, &p);
        p.dryGain = 0.0f;
        p.wetDirectGain = 1.0f;
        p.wetCrossGain = 0.0f;
        p.depthMs = 0.0f;
        p.analogAmount = 0.0f;
        p.companderAmount = 0.0f;
        p.bassFocusHz = 0.0f;
        Dimension_SetParams(&dOff, &p);
        Dimension_Reset(&dOff);

        Dimension_Init(&dOn, TEST_SR);
        p.bassFocusHz = 120.0f;
        Dimension_SetParams(&dOn, &p);
        Dimension_Reset(&dOn);

        double sideEnergyOff = 0.0, sideEnergyOn = 0.0;
        float inL[BLOCK], inR[BLOCK], oLOff[BLOCK], oROff[BLOCK], oLOn[BLOCK], oROn[BLOCK];

        for (uint32_t n = 0; n < 4800; n += BLOCK) {
            for (uint32_t i = 0; i < BLOCK; ++i) {
                const float s = 0.15f * sinf(2.0f * PI_F * freq * (float)(n + i) / TEST_SR);
                inL[i] = s;
                inR[i] = -s;
            }
            Dimension_ProcessBlock(&dOff, inL, inR, oLOff, oROff, BLOCK);
            Dimension_ProcessBlock(&dOn, inL, inR, oLOn, oROn, BLOCK);
            if (n >= 1920) {
                for (uint32_t i = 0; i < BLOCK; ++i) {
                    const float sOff = 0.5f * (oLOff[i] - oROff[i]);
                    const float sOn  = 0.5f * (oLOn[i] - oROn[i]);
                    sideEnergyOff += (double)sOff * (double)sOff;
                    sideEnergyOn  += (double)sOn * (double)sOn;
                }
            }
        }
        const double powerRatio = sideEnergyOn / sideEnergyOff;
        const double db = 10.0 * log10(powerRatio);

        if (freq == 40.0f) {
            CHECK(db < -8.0);
        } else if (freq == 60.0f) {
            CHECK(db < -5.5 && db > -9.0);
        } else if (freq >= 1000.0f) {
            CHECK(db > -0.2);
        }
    }
    return 0;
}

static int test_spatial_engine_automation_smoothness(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    Dimension_SetMode(&d, DIMENSION_MODE_I);

    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    for (uint32_t i = 0; i < BLOCK; ++i) {
        inL[i] = 0.15f;
        inR[i] = 0.10f;
    }

    Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);

    DimensionParams p = d.params;
    p.stereoWidth = 2.0f;
    p.bassFocusHz = 250.0f;
    Dimension_SetParams(&d, &p);

    float prevL = outL[BLOCK - 1];
    float prevR = outR[BLOCK - 1];
    float maxDelta = 0.0f;

    for (uint32_t b = 0; b < 20; ++b) {
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
        for (uint32_t i = 0; i < BLOCK; ++i) {
            const float dL = fabsf(outL[i] - prevL);
            const float dR = fabsf(outR[i] - prevR);
            if (dL > maxDelta) maxDelta = dL;
            if (dR > maxDelta) maxDelta = dR;
            prevL = outL[i];
            prevR = outR[i];
        }
    }
    CHECK(maxDelta < 0.02f);
    return 0;
}

/* =========================================================================
 * BBD Character Engine 2.1 — Phase A Test Suite
 * ========================================================================= */

static int test_bbd_character_neutrality_zero(void) {
    DimensionDSP dOld, dNew;
    Dimension_Init(&dOld, TEST_SR);
    Dimension_Init(&dNew, TEST_SR);

    DimensionParams p = dNew.params;
    p.bbdCharacter = 0.0f;
    Dimension_SetParams(&dNew, &p);

    float inL[BLOCK], inR[BLOCK];
    float outOldL[BLOCK], outOldR[BLOCK];
    float outNewL[BLOCK], outNewR[BLOCK];

    for (uint32_t b = 0; b < 200; ++b) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            inL[i] = 0.25f * sinf(2.0f * 3.14159f * 440.0f * (float)(b * BLOCK + i) / TEST_SR);
            inR[i] = 0.25f * cosf(2.0f * 3.14159f * 660.0f * (float)(b * BLOCK + i) / TEST_SR);
        }
        Dimension_ProcessBlock(&dOld, inL, inR, outOldL, outOldR, BLOCK);
        Dimension_ProcessBlock(&dNew, inL, inR, outNewL, outNewR, BLOCK);

        for (uint32_t i = 0; i < BLOCK; ++i) {
            CHECK(fabsf(outNewL[i] - outOldL[i]) == 0.0f);
            CHECK(fabsf(outNewR[i] - outOldR[i]) == 0.0f);
        }
    }
    return 0;
}

static int test_bbd_character_progressive_response(void) {
    const float levels[5] = { 0.0f, 0.25f, 0.50f, 0.75f, 1.0f };
    double diffRms[5] = { 0.0 };

    float inL[BLOCK], inR[BLOCK];
    float outRefL[BLOCK], outRefR[BLOCK];
    float outTestL[BLOCK], outTestR[BLOCK];

    DimensionDSP dRef;
    Dimension_Init(&dRef, TEST_SR);

    for (int k = 0; k < 5; ++k) {
        DimensionDSP dTest;
        Dimension_Init(&dTest, TEST_SR);
        DimensionParams p = dTest.params;
        p.bbdCharacter = levels[k];
        Dimension_SetParams(&dTest, &p);

        Dimension_Reset(&dRef);
        Dimension_Reset(&dTest);

        double accumDiff = 0.0;
        uint32_t totalSamples = 0;

        for (uint32_t b = 0; b < 100; ++b) {
            for (uint32_t i = 0; i < BLOCK; ++i) {
                inL[i] = 0.30f * sinf(2.0f * 3.14159f * 1000.0f * (float)(b * BLOCK + i) / TEST_SR);
                inR[i] = 0.30f * cosf(2.0f * 3.14159f * 1000.0f * (float)(b * BLOCK + i) / TEST_SR);
            }
            Dimension_ProcessBlock(&dRef, inL, inR, outRefL, outRefR, BLOCK);
            Dimension_ProcessBlock(&dTest, inL, inR, outTestL, outTestR, BLOCK);

            if (b >= 20) { /* after smoother settling */
                for (uint32_t i = 0; i < BLOCK; ++i) {
                    const float dL = outTestL[i] - outRefL[i];
                    const float dR = outTestR[i] - outRefR[i];
                    accumDiff += (double)(dL * dL + dR * dR);
                    totalSamples += 2;
                }
            }
        }
        diffRms[k] = sqrt(accumDiff / (double)totalSamples);
    }

    CHECK(diffRms[0] == 0.0);
    CHECK(diffRms[1] > 0.0001);
    CHECK(diffRms[2] > diffRms[1]);
    CHECK(diffRms[3] > diffRms[2]);
    CHECK(diffRms[4] > diffRms[3]);
    return 0;
}

static int test_bbd_character_determinism(void) {
    DimensionDSP d1, d2;
    Dimension_Init(&d1, TEST_SR);
    Dimension_Init(&d2, TEST_SR);

    DimensionParams p = d1.params;
    p.bbdCharacter = 0.75f;
    Dimension_SetParams(&d1, &p);
    Dimension_SetParams(&d2, &p);

    float inL[BLOCK], inR[BLOCK];
    float out1L[BLOCK], out1R[BLOCK];
    float out2L[BLOCK], out2R[BLOCK];

    for (uint32_t b = 0; b < 100; ++b) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            inL[i] = (float)((b * BLOCK + i) % 17) * 0.05f - 0.4f;
            inR[i] = (float)((b * BLOCK + i) % 23) * 0.04f - 0.4f;
        }
        Dimension_ProcessBlock(&d1, inL, inR, out1L, out1R, BLOCK);
        Dimension_ProcessBlock(&d2, inL, inR, out2L, out2R, BLOCK);

        for (uint32_t i = 0; i < BLOCK; ++i) {
            CHECK(out1L[i] == out2L[i]);
            CHECK(out1R[i] == out2R[i]);
            CHECK(isfinite(out1L[i]));
            CHECK(isfinite(out1R[i]));
        }
    }
    return 0;
}

static int test_bbd_character_automation_smoothness(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);

    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    for (uint32_t i = 0; i < BLOCK; ++i) {
        inL[i] = 0.20f;
        inR[i] = 0.15f;
    }

    Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);

    float prevL = outL[BLOCK - 1];
    float prevR = outR[BLOCK - 1];
    float maxDelta = 0.0f;

    for (uint32_t b = 0; b < 100; ++b) {
        if (b % 10 == 0) {
            DimensionParams p = d.params;
            p.bbdCharacter = (b % 20 == 0) ? 1.0f : 0.0f;
            Dimension_SetParams(&d, &p);
        }
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
        for (uint32_t i = 0; i < BLOCK; ++i) {
            const float dL = fabsf(outL[i] - prevL);
            const float dR = fabsf(outR[i] - prevR);
            if (dL > maxDelta) maxDelta = dL;
            if (dR > maxDelta) maxDelta = dR;
            prevL = outL[i];
            prevR = outR[i];
            CHECK(isfinite(outL[i]));
            CHECK(isfinite(outR[i]));
        }
    }
    CHECK(maxDelta < 0.05f);
    return 0;
}

static int test_bbd_character_fast_slow_modulation(void) {
    const float rates[2] = { 0.02f, 4.0f };
    for (int r = 0; r < 2; ++r) {
        DimensionDSP d;
        Dimension_Init(&d, TEST_SR);
        DimensionParams p = d.params;
        p.mode = DIMENSION_MODE_CUSTOM;
        p.rateHz = rates[r];
        p.depthMs = 2.5f;
        p.bbdCharacter = 1.0f;
        Dimension_SetParams(&d, &p);

        float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
        for (uint32_t b = 0; b < 100; ++b) {
            for (uint32_t i = 0; i < BLOCK; ++i) {
                inL[i] = 0.25f * sinf(2.0f * 3.14159f * 300.0f * (float)(b * BLOCK + i) / TEST_SR);
                inR[i] = 0.25f * cosf(2.0f * 3.14159f * 300.0f * (float)(b * BLOCK + i) / TEST_SR);
            }
            Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
            for (uint32_t i = 0; i < BLOCK; ++i) {
                CHECK(isfinite(outL[i]));
                CHECK(isfinite(outR[i]));
                CHECK(fabsf(outL[i]) < 1.5f);
                CHECK(fabsf(outR[i]) < 1.5f);
            }
        }
    }
    return 0;
}

static int test_bbd_character_sample_rates(void) {
    const float rates[3] = { 44100.0f, 48000.0f, 96000.0f };
    for (int k = 0; k < 3; ++k) {
        DimensionDSP d;
        Dimension_Init(&d, rates[k]);
        DimensionParams p = d.params;
        p.bbdCharacter = 0.85f;
        Dimension_SetParams(&d, &p);

        float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
        for (uint32_t b = 0; b < 80; ++b) {
            for (uint32_t i = 0; i < BLOCK; ++i) {
                inL[i] = 0.20f * sinf(2.0f * 3.14159f * 1000.0f * (float)(b * BLOCK + i) / rates[k]);
                inR[i] = 0.20f * sinf(2.0f * 3.14159f * 1000.0f * (float)(b * BLOCK + i) / rates[k]);
            }
            Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
            for (uint32_t i = 0; i < BLOCK; ++i) {
                CHECK(isfinite(outL[i]));
                CHECK(isfinite(outR[i]));
            }
        }
    }
    return 0;
}

static int test_bbd_character_block_sizes(void) {
    const uint32_t blockSizes[3] = { 16, 32, 64 };
    for (int k = 0; k < 3; ++k) {
        DimensionDSP d;
        Dimension_Init(&d, TEST_SR);
        DimensionParams p = d.params;
        p.bbdCharacter = 0.60f;
        Dimension_SetParams(&d, &p);

        const uint32_t bs = blockSizes[k];
        float inL[64], inR[64], outL[64], outR[64];
        for (uint32_t b = 0; b < 50; ++b) {
            for (uint32_t i = 0; i < bs; ++i) {
                inL[i] = 0.18f;
                inR[i] = 0.18f;
            }
            Dimension_ProcessBlock(&d, inL, inR, outL, outR, bs);
            for (uint32_t i = 0; i < bs; ++i) {
                CHECK(isfinite(outL[i]));
                CHECK(isfinite(outR[i]));
            }
        }
    }
    return 0;
}

static int test_bbd_character_reset_clean(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    DimensionParams p = d.params;
    p.bbdCharacter = 1.0f;
    Dimension_SetParams(&d, &p);

    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    for (uint32_t i = 0; i < BLOCK; ++i) {
        inL[i] = 0.5f;
        inR[i] = 0.5f;
    }
    for (uint32_t b = 0; b < 20; ++b) {
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
    }

    Dimension_Reset(&d);
    CHECK(d.bbdClock.chL.dispState == 0.0f);
    CHECK(d.bbdClock.chR.dispState == 0.0f);

    /* Zero input produces zero output */
    for (uint32_t i = 0; i < BLOCK; ++i) {
        inL[i] = 0.0f;
        inR[i] = 0.0f;
    }
    Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
    for (uint32_t i = 0; i < BLOCK; ++i) {
        CHECK(fabsf(outL[i]) < 1e-6f);
        CHECK(fabsf(outR[i]) < 1e-6f);
    }
    return 0;
}

static int test_bbd_character_silence_input(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    DimensionParams p = d.params;
    p.bbdCharacter = 1.0f;
    Dimension_SetParams(&d, &p);

    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    memset(inL, 0, sizeof(inL));
    memset(inR, 0, sizeof(inR));

    for (uint32_t b = 0; b < 100; ++b) {
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
        for (uint32_t i = 0; i < BLOCK; ++i) {
            CHECK(outL[i] == 0.0f);
            CHECK(outR[i] == 0.0f);
        }
    }
    return 0;
}

static int test_bbd_character_aliasing_and_dc_protection(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    DimensionParams p = d.params;
    p.bbdCharacter = 1.0f;
    Dimension_SetParams(&d, &p);

    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    double sumL = 0.0, sumR = 0.0;
    uint32_t totalSamples = 0;

    for (uint32_t b = 0; b < 200; ++b) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            inL[i] = 0.25f * sinf(2.0f * 3.14159f * 10000.0f * (float)(b * BLOCK + i) / TEST_SR);
            inR[i] = 0.25f * cosf(2.0f * 3.14159f * 10000.0f * (float)(b * BLOCK + i) / TEST_SR);
        }
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
        if (b >= 50) {
            for (uint32_t i = 0; i < BLOCK; ++i) {
                sumL += (double)outL[i];
                sumR += (double)outR[i];
                totalSamples++;
            }
        }
    }
    const double dcL = sumL / (double)totalSamples;
    const double dcR = sumR / (double)totalSamples;
    CHECK(fabs(dcL) < 0.005);
    CHECK(fabs(dcR) < 0.005);
    return 0;
}

static int test_bbd_character_lr_integrity_and_mono_compat(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    DimensionParams p = d.params;
    p.mode = DIMENSION_MODE_CUSTOM;
    p.rateHz = 1.0f;
    p.bbdCharacter = 0.70f;
    Dimension_SetParams(&d, &p);

    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    double energyL = 0.0, energyR = 0.0;
    double energyMid = 0.0;
    const uint32_t totalBlocks = (uint32_t)(TEST_SR / BLOCK); /* Exactly 1 full LFO cycle */

    for (uint32_t b = 0; b < totalBlocks + 50; ++b) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            const float monoSig = 0.25f * sinf(2.0f * 3.14159f * 250.0f * (float)(b * BLOCK + i) / TEST_SR);
            inL[i] = monoSig;
            inR[i] = monoSig;
        }
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
        if (b >= 50) { /* Exactly 1 full period after smoother settling */
            for (uint32_t i = 0; i < BLOCK; ++i) {
                energyL += (double)(outL[i] * outL[i]);
                energyR += (double)(outR[i] * outR[i]);
                const float mid = 0.5f * (outL[i] + outR[i]);
                energyMid += (double)(mid * mid);
            }
        }
    }
    /* Symmetrical energy balance over a complete modulation cycle */
    const double ratio = energyL / energyR;
    CHECK(ratio > 0.95 && ratio < 1.05);
    /* Robust mid presence for mono compatibility */
    CHECK(energyMid > 0.5 * energyL);
    return 0;
}

static int test_bbd_character_modes_regression(void) {
    for (int m = 0; m <= 3; ++m) {
        DimensionDSP d;
        Dimension_Init(&d, TEST_SR);
        Dimension_SetMode(&d, (DimensionMode)m);
        CHECK(d.params.bbdCharacter == 0.0f);

        float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
        for (uint32_t b = 0; b < 30; ++b) {
            for (uint32_t i = 0; i < BLOCK; ++i) {
                inL[i] = 0.2f;
                inR[i] = 0.2f;
            }
            Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
            for (uint32_t i = 0; i < BLOCK; ++i) {
                CHECK(isfinite(outL[i]));
                CHECK(isfinite(outR[i]));
            }
        }
    }
    return 0;
}

int main(void) {
#define RUN_TEST(fn) do { if ((fn)() != 0) return 1; } while (0)
    RUN_TEST(test_zero_input_long_no_nan_low_dc);
    RUN_TEST(test_impulse_delay_correct);
    RUN_TEST(test_sine_1khz_low_distortion_analog_zero);
    RUN_TEST(test_sine_100hz_center_preserved);
    RUN_TEST(test_input_plusminus2_safe_saturation);
    RUN_TEST(test_mode_switch_1s_no_clicks);
    RUN_TEST(test_noise_stability);
    RUN_TEST(test_sample_rates_and_block_limits);
    RUN_TEST(test_determinism_between_runs);
    RUN_TEST(test_extreme_and_nonfinite_params_are_clamped);
    RUN_TEST(test_regression_signal_metrics);
    RUN_TEST(test_parameter_schema_is_stable);
    RUN_TEST(test_block_size_smoothing_invariance);
    RUN_TEST(test_smoothed_controls_transitions);
    RUN_TEST(test_mode_to_custom_preserves_audio_and_params);
    RUN_TEST(test_mono_in_stereo_out_decorrelation);
    RUN_TEST(test_spatial_engine_neutrality);
    RUN_TEST(test_spatial_engine_side_control);
    RUN_TEST(test_spatial_engine_mid_integrity);
    RUN_TEST(test_spatial_engine_bass_focus_response);
    RUN_TEST(test_spatial_engine_automation_smoothness);
    RUN_TEST(test_bbd_character_neutrality_zero);
    RUN_TEST(test_bbd_character_progressive_response);
    RUN_TEST(test_bbd_character_determinism);
    RUN_TEST(test_bbd_character_automation_smoothness);
    RUN_TEST(test_bbd_character_fast_slow_modulation);
    RUN_TEST(test_bbd_character_sample_rates);
    RUN_TEST(test_bbd_character_block_sizes);
    RUN_TEST(test_bbd_character_reset_clean);
    RUN_TEST(test_bbd_character_silence_input);
    RUN_TEST(test_bbd_character_aliasing_and_dc_protection);
    RUN_TEST(test_bbd_character_lr_integrity_and_mono_compat);
    RUN_TEST(test_bbd_character_modes_regression);
    puts("test_dimension_core: OK");
    return 0;
}
