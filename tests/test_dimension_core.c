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
    CHECK(DIMENSION_PARAM_SCHEMA_VERSION == 1U);
    CHECK(DIMENSION_PARAM_COUNT == 13);
    CHECK(DIMENSION_PARAM_INPUT_GAIN == 0);
    CHECK(DIMENSION_PARAM_WIDTH == 12);

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

static int test_smoothed_controls_transitions(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);

    /* Verify smoothers start at exact target with zero initial ramp */
    CHECK(fabsf(d.smoothInputGain - 1.0f) < 1e-6f);
    CHECK(fabsf(d.smoothOutputGain - 1.0f) < 1e-6f);
    CHECK(fabsf(d.smoothDryGain - 0.83f) < 1e-6f);
    CHECK(fabsf(d.smoothWidth - 1.0f) < 1e-6f);

    /* Change parameters abruptly to stress test smoothing */
    DimensionParams p;
    Dimension_GetParams(&d, &p);
    p.mode = DIMENSION_MODE_CUSTOM;
    p.inputGain = 2.5f;
    p.outputGain = 0.5f;
    p.dryGain = 0.3f;
    p.width = 1.8f;
    p.wetDirectGain = 0.7f;
    p.wetCrossGain = 0.5f;
    Dimension_SetParams(&d, &p);

    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    float prevL = 0.0f, prevR = 0.0f;
    float maxDelta = 0.0f;
    bool first = true;

    /* Process audio while parameters smooth */
    for (uint32_t n = 0; n < (uint32_t)TEST_SR / 2U; n += BLOCK) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            const float s = 0.2f * sinf(2.0f * PI_F * 1000.0f * (float)(n + i) / TEST_SR);
            inL[i] = inR[i] = s;
        }
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
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

    /* Measure baseline natural delta with fixed target parameters */
    DimensionDSP dBase;
    Dimension_Init(&dBase, TEST_SR);
    Dimension_SetParams(&dBase, &p);
    float basePrev = 0.0f;
    float baseMaxDelta = 0.0f;
    bool baseFirst = true;
    for (uint32_t n = 0; n < (uint32_t)TEST_SR / 2U; n += BLOCK) {
        for (uint32_t i = 0; i < BLOCK; ++i) {
            const float s = 0.2f * sinf(2.0f * PI_F * 1000.0f * (float)(n + i) / TEST_SR);
            inL[i] = inR[i] = s;
        }
        Dimension_ProcessBlock(&dBase, inL, inR, outL, outR, BLOCK);
        for (uint32_t i = 0; i < BLOCK; ++i) {
            if (!baseFirst) {
                const float dL = fabsf(outL[i] - basePrev);
                if (dL > baseMaxDelta) baseMaxDelta = dL;
            }
            basePrev = outL[i];
            baseFirst = false;
        }
    }
    /* Discontinuity added by parameter smoothing above natural signal variation must be negligible (< 0.005) */
    CHECK((maxDelta - baseMaxDelta) < 0.005f);

    /* Confirm parameters converged close to targets */
    CHECK(fabsf(d.smoothInputGain - 2.5f) < 0.01f);
    CHECK(fabsf(d.smoothOutputGain - 0.5f) < 0.01f);
    CHECK(fabsf(d.smoothDryGain - 0.3f) < 0.01f);
    CHECK(fabsf(d.smoothWidth - 1.8f) < 0.01f);
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
    puts("test_dimension_core: OK");
    return 0;
}
