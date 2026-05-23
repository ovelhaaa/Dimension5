#include <assert.h>
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

static float frand_uniform(uint32_t* state) {
    *state = (*state * 1664525u) + 1013904223u;
    return ((float)((*state >> 8) & 0x00FFFFFFu) / 8388608.0f) - 1.0f;
}

static void process_constant(DimensionDSP* d, float value, uint32_t total, float* peak, float* dc_acc) {
    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    uint32_t rem = total;
    while (rem > 0U) {
        const uint32_t n = (rem > BLOCK) ? BLOCK : rem;
        for (uint32_t i = 0; i < n; ++i) inL[i] = inR[i] = value;
        Dimension_ProcessBlock(d, inL, inR, outL, outR, n);
        for (uint32_t i = 0; i < n; ++i) {
            assert(isfinite(outL[i]) && isfinite(outR[i]));
            const float a = fabsf(outL[i]);
            const float b = fabsf(outR[i]);
            if (a > *peak) *peak = a;
            if (b > *peak) *peak = b;
            *dc_acc += 0.5f * (outL[i] + outR[i]);
        }
        rem -= n;
    }
}

static void test_zero_input_long_no_nan_low_dc(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    Dimension_SetMode(&d, DIMENSION_MODE_II);
    float peak = 0.0f, dc = 0.0f;
    const uint32_t total = (uint32_t)TEST_SR * 8U;
    process_constant(&d, 0.0f, total, &peak, &dc);
    dc /= (float)total;
    assert(fabsf(dc) < 1e-4f);
    assert(peak < 1e-6f);
}

static void test_impulse_delay_correct(void) {
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
    assert(idx != 0xFFFFFFFFu);
    assert((int32_t)idx >= (int32_t)expected - 8);
    assert((int32_t)idx <= (int32_t)expected + 24);
}

static void test_sine_1khz_low_distortion_analog_zero(void) {
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
            ein += inL[i] * inL[i];
            eerr += e * e;
        }
    }
    const double nrmse = sqrt(eerr / (ein + DBL_MIN));
    assert(nrmse < 0.35);
}

static void test_sine_100hz_center_preserved(void) {
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
            inMid += inL[i] * inL[i];
            const float m = 0.5f * (outL[i] + outR[i]);
            outMid += m * m;
        }
    }
    const double ratio = sqrt(outMid / (inMid + DBL_MIN));
    assert(ratio > 0.55 && ratio < 1.25);
}

static void test_input_plusminus2_safe_saturation(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    float peak = 0.0f, dc = 0.0f;
    process_constant(&d, 2.0f, TEST_SR, &peak, &dc);
    process_constant(&d, -2.0f, TEST_SR, &peak, &dc);
    assert(peak <= 2.0f);
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

static void test_mode_switch_1s_no_clicks(void) {
    const float baselineMaxStep = run_mode_switch_step(false);
    const float switchedMaxStep = run_mode_switch_step(true);

    assert(switchedMaxStep < 0.01f);
    assert((switchedMaxStep - baselineMaxStep) < 0.003f);
}

static void test_noise_stability(void) {
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
            assert(isfinite(outL[i]) && isfinite(outR[i]));
            float a = fabsf(outL[i]); if (a > peak) peak = a;
            float b = fabsf(outR[i]); if (b > peak) peak = b;
        }
    }
    assert(peak < 2.0f);
}

int main(void) {
    test_zero_input_long_no_nan_low_dc();
    test_impulse_delay_correct();
    test_sine_1khz_low_distortion_analog_zero();
    test_sine_100hz_center_preserved();
    test_input_plusminus2_safe_saturation();
    test_mode_switch_1s_no_clicks();
    test_noise_stability();
    puts("test_dimension_core: OK");
    return 0;
}
