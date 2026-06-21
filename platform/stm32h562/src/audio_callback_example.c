#include <stdint.h>
#include <math.h>

#include "dimension_dsp.h"

#define GPIOA_BASE    0x42020000U
#define GPIOA_BSRR    (*(volatile uint32_t*)(GPIOA_BASE + 0x18U))

static DimensionDSP g_dimension;

static inline float dimension_int32_to_float32(int32_t x) {
    return (float)x * (1.0f / 2147483648.0f);
}

static inline int32_t dimension_float32_to_int32(float x) {
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    return (int32_t)(x * 2147483647.0f);
}

void Dimension_ExampleInit(void) {
    Dimension_Init(&g_dimension, DIMENSION_SAMPLE_RATE_DEFAULT);
}

void Audio_ProcessHalfBuffer(int32_t* rx, int32_t* tx, uint32_t frames) {
    static float inL[DIMENSION_MAX_BLOCK_SIZE] DIMENSION_ALIGN_32;
    static float inR[DIMENSION_MAX_BLOCK_SIZE] DIMENSION_ALIGN_32;
    static float outL[DIMENSION_MAX_BLOCK_SIZE] DIMENSION_ALIGN_32;
    static float outR[DIMENSION_MAX_BLOCK_SIZE] DIMENSION_ALIGN_32;

    while (frames > 0U) {
        uint32_t n = (frames > DIMENSION_MAX_BLOCK_SIZE) ? DIMENSION_MAX_BLOCK_SIZE : frames;

        for (uint32_t i = 0U; i < n; ++i) {
            inL[i] = dimension_int32_to_float32(rx[2U * i + 0U]);
            inR[i] = dimension_int32_to_float32(rx[2U * i + 1U]);
        }

        GPIOA_BSRR = (1 << 0); // Set PA0 High

        Dimension_ProcessBlock(&g_dimension, inL, inR, outL, outR, n);

        GPIOA_BSRR = (1 << 16); // Set PA0 Low

        for (uint32_t i = 0U; i < n; ++i) {
            tx[2U * i + 0U] = dimension_float32_to_int32(outL[i]);
            tx[2U * i + 1U] = dimension_float32_to_int32(outR[i]);
        }

        rx += 2U * n;
        tx += 2U * n;
        frames -= n;
    }
}
