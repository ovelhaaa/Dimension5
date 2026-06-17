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
    puts("test_dimension_core: OK");
    return 0;
}
