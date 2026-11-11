#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/dimension_dsp.h"
#include "../src/dimension_params.h"

#define TEST_SR 48000.0f
#define BLOCK 64U
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "CHECK failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); return 1; } } while (0)

/* Mock representation of plugin parameter state corresponding to APVTS */
typedef struct MockPluginState {
    float mode;
    float mix;
    float bypass;
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
    float width;
    float stereoWidth;
    float bassFocusHz;
    float bbdCharacter;
} MockPluginState;

static void mock_init_defaults(MockPluginState* s) {
    DimensionParams p;
    Dimension_GetModeParams(DIMENSION_MODE_I, &p);
    s->mode = (float)p.mode;
    s->mix = 1.0f;
    s->bypass = 0.0f;
    s->inputGain = p.inputGain;
    s->outputGain = p.outputGain;
    s->dryGain = p.dryGain;
    s->wetDirectGain = p.wetDirectGain;
    s->wetCrossGain = p.wetCrossGain;
    s->baseDelayMs = p.baseDelayMs;
    s->depthMs = p.depthMs;
    s->rateHz = p.rateHz;
    s->hpfHz = p.hpfHz;
    s->lpfHz = p.lpfHz;
    s->analogAmount = p.analogAmount;
    s->companderAmount = p.companderAmount;
    s->width = p.width;
    s->stereoWidth = p.stereoWidth;
    s->bassFocusHz = p.bassFocusHz;
    s->bbdCharacter = 0.0f;
}

static void mock_apply_mode(MockPluginState* s, DimensionMode mode) {
    s->mode = (float)mode;
    if (mode == DIMENSION_MODE_CUSTOM) {
        return;
    }
    DimensionParams p;
    Dimension_GetModeParams(mode, &p);

    s->dryGain = p.dryGain;
    s->wetDirectGain = p.wetDirectGain;
    s->wetCrossGain = p.wetCrossGain;
    s->baseDelayMs = p.baseDelayMs;
    s->depthMs = p.depthMs;
    s->rateHz = p.rateHz;
    s->hpfHz = p.hpfHz;
    s->lpfHz = p.lpfHz;
    s->analogAmount = p.analogAmount;
    s->companderAmount = p.companderAmount;
}

static void mock_edit_advanced_param(MockPluginState* s, float* targetField, float newValue) {
    *targetField = newValue;
    /* Automatic transition to Custom if in Modes I-IV */
    if ((int)roundf(s->mode) != (int)DIMENSION_MODE_CUSTOM) {
        s->mode = (float)DIMENSION_MODE_CUSTOM;
    }
}

static int test_mode_preset_values_exact(void) {
    DimensionParams p;

    Dimension_GetModeParams(DIMENSION_MODE_I, &p);
    CHECK(fabsf(p.baseDelayMs - 7.0f) < 1e-5f);
    CHECK(fabsf(p.depthMs - 0.9f) < 1e-5f);
    CHECK(fabsf(p.rateHz - 0.25f) < 1e-5f);
    CHECK(fabsf(p.hpfHz - 120.0f) < 1e-5f);
    CHECK(fabsf(p.lpfHz - 8000.0f) < 1e-5f);
    CHECK(fabsf(p.wetDirectGain - 0.40f) < 1e-5f);
    CHECK(fabsf(p.wetCrossGain - 0.28f) < 1e-5f);
    CHECK(fabsf(p.analogAmount - 0.35f) < 1e-5f);
    CHECK(fabsf(p.companderAmount - 0.35f) < 1e-5f);
    CHECK(fabsf(p.dryGain - 0.83f) < 1e-5f);

    Dimension_GetModeParams(DIMENSION_MODE_II, &p);
    CHECK(fabsf(p.baseDelayMs - 8.5f) < 1e-5f);
    CHECK(fabsf(p.depthMs - 1.4f) < 1e-5f);
    CHECK(fabsf(p.rateHz - 0.25f) < 1e-5f);
    CHECK(fabsf(p.hpfHz - 120.0f) < 1e-5f);
    CHECK(fabsf(p.lpfHz - 7500.0f) < 1e-5f);
    CHECK(fabsf(p.wetDirectGain - 0.475f) < 1e-5f);
    CHECK(fabsf(p.wetCrossGain - 0.3325f) < 1e-5f);
    CHECK(fabsf(p.analogAmount - 0.45f) < 1e-5f);
    CHECK(fabsf(p.companderAmount - 0.45f) < 1e-5f);
    CHECK(fabsf(p.dryGain - 0.83f) < 1e-5f);

    Dimension_GetModeParams(DIMENSION_MODE_III, &p);
    CHECK(fabsf(p.baseDelayMs - 10.0f) < 1e-5f);
    CHECK(fabsf(p.depthMs - 1.8f) < 1e-5f);
    CHECK(fabsf(p.rateHz - 0.50f) < 1e-5f);
    CHECK(fabsf(p.hpfHz - 130.0f) < 1e-5f);
    CHECK(fabsf(p.lpfHz - 7000.0f) < 1e-5f);
    CHECK(fabsf(p.wetDirectGain - 0.50f) < 1e-5f);
    CHECK(fabsf(p.wetCrossGain - 0.35f) < 1e-5f);
    CHECK(fabsf(p.analogAmount - 0.50f) < 1e-5f);
    CHECK(fabsf(p.companderAmount - 0.50f) < 1e-5f);
    CHECK(fabsf(p.dryGain - 0.83f) < 1e-5f);

    Dimension_GetModeParams(DIMENSION_MODE_IV, &p);
    CHECK(fabsf(p.baseDelayMs - 11.5f) < 1e-5f);
    CHECK(fabsf(p.depthMs - 2.4f) < 1e-5f);
    CHECK(fabsf(p.rateHz - 0.50f) < 1e-5f);
    CHECK(fabsf(p.hpfHz - 140.0f) < 1e-5f);
    CHECK(fabsf(p.lpfHz - 6500.0f) < 1e-5f);
    CHECK(fabsf(p.wetDirectGain - 0.55f) < 1e-5f);
    CHECK(fabsf(p.wetCrossGain - 0.385f) < 1e-5f);
    CHECK(fabsf(p.analogAmount - 0.60f) < 1e-5f);
    CHECK(fabsf(p.companderAmount - 0.60f) < 1e-5f);
    CHECK(fabsf(p.dryGain - 0.83f) < 1e-5f);
    return 0;
}

static int test_mode_to_custom_preserves_values_mode_ii(void) {
    MockPluginState state;
    mock_init_defaults(&state);

    /* Select Mode II */
    mock_apply_mode(&state, DIMENSION_MODE_II);
    CHECK((int)roundf(state.mode) == (int)DIMENSION_MODE_II);
    CHECK(fabsf(state.baseDelayMs - 8.5f) < 1e-5f);
    CHECK(fabsf(state.depthMs - 1.4f) < 1e-5f);
    CHECK(fabsf(state.lpfHz - 7500.0f) < 1e-5f);

    /* Switch directly to Custom without editing values */
    state.mode = (float)DIMENSION_MODE_CUSTOM;

    /* Values must be preserved completely */
    CHECK(fabsf(state.baseDelayMs - 8.5f) < 1e-5f);
    CHECK(fabsf(state.depthMs - 1.4f) < 1e-5f);
    CHECK(fabsf(state.lpfHz - 7500.0f) < 1e-5f);
    CHECK(fabsf(state.wetDirectGain - 0.475f) < 1e-5f);
    CHECK(fabsf(state.wetCrossGain - 0.3325f) < 1e-5f);
    CHECK(fabsf(state.analogAmount - 0.45f) < 1e-5f);
    CHECK(fabsf(state.companderAmount - 0.45f) < 1e-5f);

    /* First movement test: in Mode II, edit depthMs from 1.4 to 2.2 */
    mock_apply_mode(&state, DIMENSION_MODE_II);
    mock_edit_advanced_param(&state, &state.depthMs, 2.2f);

    CHECK((int)roundf(state.mode) == (int)DIMENSION_MODE_CUSTOM);
    CHECK(fabsf(state.depthMs - 2.2f) < 1e-5f);
    /* Other parameters retain Mode II values */
    CHECK(fabsf(state.baseDelayMs - 8.5f) < 1e-5f);
    CHECK(fabsf(state.lpfHz - 7500.0f) < 1e-5f);
    CHECK(fabsf(state.analogAmount - 0.45f) < 1e-5f);
    return 0;
}

static int test_mode_to_custom_preserves_values_mode_iv(void) {
    MockPluginState state;
    mock_init_defaults(&state);

    mock_apply_mode(&state, DIMENSION_MODE_IV);
    CHECK((int)roundf(state.mode) == (int)DIMENSION_MODE_IV);
    CHECK(fabsf(state.baseDelayMs - 11.5f) < 1e-5f);
    CHECK(fabsf(state.depthMs - 2.4f) < 1e-5f);
    CHECK(fabsf(state.lpfHz - 6500.0f) < 1e-5f);

    /* First movement test: edit rateHz from 0.50 to 1.5 */
    mock_edit_advanced_param(&state, &state.rateHz, 1.5f);

    CHECK((int)roundf(state.mode) == (int)DIMENSION_MODE_CUSTOM);
    CHECK(fabsf(state.rateHz - 1.5f) < 1e-5f);
    /* Other parameters retain Mode IV values */
    CHECK(fabsf(state.baseDelayMs - 11.5f) < 1e-5f);
    CHECK(fabsf(state.depthMs - 2.4f) < 1e-5f);
    CHECK(fabsf(state.lpfHz - 6500.0f) < 1e-5f);
    CHECK(fabsf(state.wetDirectGain - 0.55f) < 1e-5f);
    CHECK(fabsf(state.wetCrossGain - 0.385f) < 1e-5f);
    return 0;
}

static int test_compare_ab_slots(void) {
    MockPluginState slotA, slotB, current;
    mock_init_defaults(&current);

    /* Slot A setup: Mode II */
    mock_apply_mode(&current, DIMENSION_MODE_II);
    slotA = current;
    slotB = current;

    /* Switch to Slot B and change to Mode IV */
    mock_apply_mode(&current, DIMENSION_MODE_IV);
    slotB = current;

    /* Recall Slot A */
    current = slotA;
    CHECK((int)roundf(current.mode) == (int)DIMENSION_MODE_II);
    CHECK(fabsf(current.baseDelayMs - 8.5f) < 1e-5f);

    /* Recall Slot B */
    current = slotB;
    CHECK((int)roundf(current.mode) == (int)DIMENSION_MODE_IV);
    CHECK(fabsf(current.baseDelayMs - 11.5f) < 1e-5f);

    /* Copy Slot B to Slot A */
    slotA = slotB;
    current = slotA;
    CHECK((int)roundf(current.mode) == (int)DIMENSION_MODE_IV);
    CHECK(fabsf(current.baseDelayMs - 11.5f) < 1e-5f);
    return 0;
}

static int test_channel_routing_mono_to_stereo(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    Dimension_SetMode(&d, DIMENSION_MODE_I);

    float monoSource[BLOCK];
    for (uint32_t i = 0; i < BLOCK; ++i) {
        monoSource[i] = 0.25f * sinf(2.0f * 3.14159f * 500.0f * (float)i / TEST_SR);
    }

    /* Mono -> Stereo routing:
     * Read mono once, feed both inL and inR */
    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    for (uint32_t i = 0; i < BLOCK; ++i) {
        inL[i] = monoSource[i];
        inR[i] = monoSource[i];
    }
    Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);

    /* 1. In bypass (effectiveMix = 0.0), both channels receive the mono source equally */
    float effectiveMix = 0.0f;
    for (uint32_t i = 0; i < BLOCK; ++i) {
        const float mixedL = inL[i] + effectiveMix * (outL[i] - inL[i]);
        const float mixedR = inR[i] + effectiveMix * (outR[i] - inR[i]);
        CHECK(fabsf(mixedL - monoSource[i]) < 1e-6f);
        CHECK(fabsf(mixedR - monoSource[i]) < 1e-6f);
    }

    /* Warm up delay line so wet path outputs audio (delay is ~7 ms) */
    for (uint32_t b = 0; b < 64; ++b) {
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
    }

    /* 2. Under effect (effectiveMix = 1.0), outL and outR are distinct stereo signals */
    effectiveMix = 1.0f;
    double diffEnergy = 0.0;
    for (uint32_t i = 0; i < BLOCK; ++i) {
        const float mixedL = inL[i] + effectiveMix * (outL[i] - inL[i]);
        const float mixedR = inR[i] + effectiveMix * (outR[i] - inR[i]);
        const float diff = mixedL - mixedR;
        diffEnergy += (double)diff * (double)diff;
    }
    CHECK(diffEnergy > 0.0001);
    return 0;
}

static int test_channel_routing_mono_to_mono(void) {
    DimensionDSP d;
    Dimension_Init(&d, TEST_SR);
    Dimension_SetMode(&d, DIMENSION_MODE_I);

    float monoSource[BLOCK];
    for (uint32_t i = 0; i < BLOCK; ++i) {
        monoSource[i] = 0.25f * sinf(2.0f * 3.14159f * 500.0f * (float)i / TEST_SR);
    }

    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    for (uint32_t i = 0; i < BLOCK; ++i) {
        inL[i] = monoSource[i];
        inR[i] = monoSource[i];
    }
    Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);

    /* Mono downmix policy: 0.5 * (mixedL + mixedR) */
    /* In bypass (effectiveMix = 0.0), output must be identical to monoSource (exact unity gain) */
    float effectiveMix = 0.0f;
    for (uint32_t i = 0; i < BLOCK; ++i) {
        const float mixedL = inL[i] + effectiveMix * (outL[i] - inL[i]);
        const float mixedR = inR[i] + effectiveMix * (outR[i] - inR[i]);
        const float monoOut = 0.5f * (mixedL + mixedR);
        CHECK(fabsf(monoOut - monoSource[i]) < 1e-6f);
    }

    /* Under effect, mono downmix remains stable and finite without clipping or explosive gain */
    effectiveMix = 1.0f;
    for (uint32_t i = 0; i < BLOCK; ++i) {
        const float mixedL = inL[i] + effectiveMix * (outL[i] - inL[i]);
        const float mixedR = inR[i] + effectiveMix * (outR[i] - inR[i]);
        const float monoOut = 0.5f * (mixedL + mixedR);
        CHECK(isfinite(monoOut));
        CHECK(fabsf(monoOut) <= 1.5f);
    }
    return 0;
}

static int test_mix_and_bypass_smoothing(void) {
    /* Test per-sample smoothing of mix parameter */
    float smoothMix = 1.0f;
    const float targetMix = 0.0f;
    const float sr = 48000.0f;
    const float mixSmoothCoeff = 1.0f - expf(-1.0f / (0.020f * sr)); /* tau = 20 ms */

    /* Process 960 samples (20 ms = tau) */
    for (int i = 0; i < 960; ++i) {
        smoothMix += mixSmoothCoeff * (targetMix - smoothMix);
    }

    /* After tau (20 ms = 960 samples), expected value is 1/e ~ 0.368 */
    CHECK(fabsf(smoothMix - 0.368f) < 0.02f);

    /* After another 15000 samples (>15 tau), converged to target */
    for (int i = 0; i < 15000; ++i) {
        smoothMix += mixSmoothCoeff * (targetMix - smoothMix);
    }
    CHECK(fabsf(smoothMix - 0.0f) < 1e-4f);
    return 0;
}

static int test_spatial_engine_wrapper_parameters_and_legacy_recall(void) {
    MockPluginState s;
    mock_init_defaults(&s);
    CHECK(s.stereoWidth == 1.0f);
    CHECK(s.bassFocusHz == 0.0f);
    CHECK(s.bbdCharacter == 0.0f);

    /* Changing mode does not alter stereoWidth, bassFocusHz, or bbdCharacter */
    s.stereoWidth = 1.6f;
    s.bassFocusHz = 120.0f;
    s.bbdCharacter = 0.75f;
    mock_apply_mode(&s, DIMENSION_MODE_III);
    CHECK(s.stereoWidth == 1.6f);
    CHECK(s.bassFocusHz == 120.0f);
    CHECK(s.bbdCharacter == 0.75f);

    /* Simulate legacy state restore without stereoWidth, bassFocusHz, or bbdCharacter:
     * In APVTS / Wrapper, missing keys fall back to descriptor defaults */
    const DimensionParamDescriptor* dWidth = Dimension_GetParamDescriptor(DIMENSION_PARAM_STEREO_WIDTH);
    const DimensionParamDescriptor* dBF = Dimension_GetParamDescriptor(DIMENSION_PARAM_BASS_FOCUS_HZ);
    const DimensionParamDescriptor* dBBD = Dimension_GetParamDescriptor(DIMENSION_PARAM_BBD_CHARACTER);
    CHECK(dWidth != NULL && dWidth->defaultValue == 1.0f);
    CHECK(dBF != NULL && dBF->defaultValue == 0.0f);
    CHECK(dBBD != NULL && dBBD->defaultValue == 0.0f);

    /* A/B compare slots preserve spatial & BBD parameters */
    MockPluginState slotA = s;
    MockPluginState slotB;
    mock_init_defaults(&slotB);
    slotB.stereoWidth = 0.5f;
    slotB.bassFocusHz = 80.0f;
    slotB.bbdCharacter = 0.40f;

    CHECK(slotA.stereoWidth == 1.6f);
    CHECK(slotA.bassFocusHz == 120.0f);
    CHECK(slotA.bbdCharacter == 0.75f);
    CHECK(slotB.stereoWidth == 0.5f);
    CHECK(slotB.bassFocusHz == 80.0f);
    CHECK(slotB.bbdCharacter == 0.40f);
    return 0;
}

int main(void) {
#define RUN_TEST(fn) do { if ((fn)() != 0) return 1; } while (0)
    RUN_TEST(test_mode_preset_values_exact);
    RUN_TEST(test_mode_to_custom_preserves_values_mode_ii);
    RUN_TEST(test_mode_to_custom_preserves_values_mode_iv);
    RUN_TEST(test_compare_ab_slots);
    RUN_TEST(test_channel_routing_mono_to_stereo);
    RUN_TEST(test_channel_routing_mono_to_mono);
    RUN_TEST(test_mix_and_bypass_smoothing);
    RUN_TEST(test_spatial_engine_wrapper_parameters_and_legacy_recall);
    puts("test_dimension_wrapper: OK");
    return 0;
}
