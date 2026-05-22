#include <stdint.h>

#include "../../src/dimension_dsp.h"

static DimensionDSP g_dimension;

void Dimension_ExampleInit(void) {
    Dimension_Init(&g_dimension, DIMENSION_SAMPLE_RATE_DEFAULT);
}

void Audio_ProcessHalfBuffer(int16_t* rx, int16_t* tx, uint32_t frames) {
    static float inL[DIMENSION_MAX_BLOCK_SIZE];
    static float inR[DIMENSION_MAX_BLOCK_SIZE];
    static float outL[DIMENSION_MAX_BLOCK_SIZE];
    static float outR[DIMENSION_MAX_BLOCK_SIZE];

    while (frames > 0U) {
        uint32_t n = (frames > DIMENSION_MAX_BLOCK_SIZE) ? DIMENSION_MAX_BLOCK_SIZE : frames;

        for (uint32_t i = 0U; i < n; ++i) {
            inL[i] = rx[2U * i + 0U] * (1.0f / 32768.0f);
            inR[i] = rx[2U * i + 1U] * (1.0f / 32768.0f);
        }

        Dimension_ProcessBlock(&g_dimension, inL, inR, outL, outR, n);

        for (uint32_t i = 0U; i < n; ++i) {
            float l = outL[i];
            float r = outR[i];

            if (l > 0.999f) l = 0.999f;
            if (l < -1.0f) l = -1.0f;
            if (r > 0.999f) r = 0.999f;
            if (r < -1.0f) r = -1.0f;

            tx[2U * i + 0U] = (int16_t)(l * 32767.0f);
            tx[2U * i + 1U] = (int16_t)(r * 32767.0f);
        }

        rx += 2U * n;
        tx += 2U * n;
        frames -= n;
    }
}
