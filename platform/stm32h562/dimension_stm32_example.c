#include <stdint.h>

#include "../../src/dimension_dsp.h"

static DimensionDSP g_dimension;

#if defined(DIMENSION_STM32_ENABLE_BENCH_GPIO) && (DIMENSION_STM32_ENABLE_BENCH_GPIO != 0)
#include "stm32h5xx.h"

#ifndef DIMENSION_BENCH_GPIO_PORT
#define DIMENSION_BENCH_GPIO_PORT GPIOA
#endif
#ifndef DIMENSION_BENCH_GPIO_PIN
#define DIMENSION_BENCH_GPIO_PIN (1U << 0)
#endif

#define DIMENSION_BENCH_ENTER() (DIMENSION_BENCH_GPIO_PORT->BSRR = DIMENSION_BENCH_GPIO_PIN)
#define DIMENSION_BENCH_EXIT()  (DIMENSION_BENCH_GPIO_PORT->BRR = DIMENSION_BENCH_GPIO_PIN)
#elif defined(DIMENSION_STM32_ENABLE_BENCH_DWT) && (DIMENSION_STM32_ENABLE_BENCH_DWT != 0)
#include "stm32h5xx.h"

volatile uint32_t g_dimension_last_cycles = 0U;
volatile uint32_t g_dimension_peak_cycles = 0U;

#define DIMENSION_BENCH_ENTER() uint32_t __dimension_bench_start = DWT->CYCCNT
#define DIMENSION_BENCH_EXIT()                            \
    do {                                                  \
        const uint32_t _cycles = DWT->CYCCNT - __dimension_bench_start; \
        g_dimension_last_cycles = _cycles;                \
        if (_cycles > g_dimension_peak_cycles) {          \
            g_dimension_peak_cycles = _cycles;            \
        }                                                 \
    } while (0)
#else
#define DIMENSION_BENCH_ENTER() ((void)0)
#define DIMENSION_BENCH_EXIT()  ((void)0)
#endif

static inline float dimension_int16_to_float32(int16_t x) {
    return (float)x * (1.0f / 32768.0f);
}

static inline int16_t dimension_float32_to_int16(float x) {
    if (x > 0.999f) x = 0.999f;
    if (x < -1.0f) x = -1.0f;
    return (int16_t)(x * 32767.0f);
}

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
            inL[i] = dimension_int16_to_float32(rx[2U * i + 0U]);
            inR[i] = dimension_int16_to_float32(rx[2U * i + 1U]);
        }

        DIMENSION_BENCH_ENTER();
        Dimension_ProcessBlock(&g_dimension, inL, inR, outL, outR, n);
        DIMENSION_BENCH_EXIT();

        for (uint32_t i = 0U; i < n; ++i) {
            tx[2U * i + 0U] = dimension_float32_to_int16(outL[i]);
            tx[2U * i + 1U] = dimension_float32_to_int16(outR[i]);
        }

        rx += 2U * n;
        tx += 2U * n;
        frames -= n;
    }
}
