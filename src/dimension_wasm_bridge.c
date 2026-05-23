#include "dimension_dsp.h"
#include <stdint.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define DIM_WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define DIM_WASM_EXPORT
#endif

static DimensionDSP g_dsp;
static int g_initialized = 0;

DIM_WASM_EXPORT void DimensionWasm_Init(float sampleRate) {
    Dimension_Init(&g_dsp, sampleRate);
    g_initialized = 1;
}

DIM_WASM_EXPORT void DimensionWasm_Reset(void) {
    if (!g_initialized) {
        Dimension_Init(&g_dsp, DIMENSION_SAMPLE_RATE_DEFAULT);
        g_initialized = 1;
    }
    Dimension_Reset(&g_dsp);
}

DIM_WASM_EXPORT void DimensionWasm_SetMode(int mode) {
    if (!g_initialized) return;
    if (mode < DIMENSION_MODE_I || mode > DIMENSION_MODE_CUSTOM) return;
    Dimension_SetMode(&g_dsp, (DimensionMode)mode);
}


DIM_WASM_EXPORT void DimensionWasm_SetParam(int paramId, float value) {
    if (!g_initialized) return;
    DimensionParams p;
    Dimension_GetParams(&g_dsp, &p);
    switch (paramId) {
        case 0: p.inputGain = value; break;
        case 1: p.outputGain = value; break;
        case 2: p.dryGain = value; break;
        case 3: p.wetDirectGain = value; break;
        case 4: p.wetCrossGain = value; break;
        case 5: p.baseDelayMs = value; p.mode = DIMENSION_MODE_CUSTOM; break;
        case 6: p.depthMs = value; p.mode = DIMENSION_MODE_CUSTOM; break;
        case 7: p.rateHz = value; p.mode = DIMENSION_MODE_CUSTOM; break;
        case 8: p.hpfHz = value; p.mode = DIMENSION_MODE_CUSTOM; break;
        case 9: p.lpfHz = value; p.mode = DIMENSION_MODE_CUSTOM; break;
        case 10: p.analogAmount = value; p.mode = DIMENSION_MODE_CUSTOM; break;
        case 11: p.companderAmount = value; p.mode = DIMENSION_MODE_CUSTOM; break;
        case 12: p.width = value; break;
        default: return;
    }
    Dimension_SetParams(&g_dsp, &p);
}

DIM_WASM_EXPORT void DimensionWasm_Process(
    const float* inL,
    const float* inR,
    float* outL,
    float* outR,
    uint32_t frames,
    int bypass) {
    if (!g_initialized || !inL || !inR || !outL || !outR) return;
    if (bypass) {
        for (uint32_t i = 0; i < frames; ++i) {
            outL[i] = inL[i];
            outR[i] = inR[i];
        }
        return;
    }

    uint32_t pos = 0;
    while (pos < frames) {
        const uint32_t n = (frames - pos) > DIMENSION_MAX_BLOCK_SIZE
            ? DIMENSION_MAX_BLOCK_SIZE
            : (frames - pos);

        Dimension_ProcessBlock(&g_dsp, inL + pos, inR + pos, outL + pos, outR + pos, n);
        pos += n;
    }
}
