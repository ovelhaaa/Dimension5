#if !defined(_WIN32)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "dimension_dsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#if defined(_WIN32)
#include <windows.h>
static double get_time_sec(void) {
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
}
#else
static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

typedef struct BenchResult {
    const char* label;
    float bbdChar;
    float sampleRate;
    uint32_t blockSize;
    double nsPerSample;
    double cpuHostPercent;
    double cpuM33TheoreticalEstPercent; /* Static unverified projection: ~40-46 cycles @ 250 MHz */
} BenchResult;

static BenchResult benchmark_configuration(
    const char* label,
    float bbdChar,
    float sampleRate,
    uint32_t blockSize,
    uint32_t totalSamples) {

    DimensionDSP dsp;
    Dimension_Init(&dsp, sampleRate);
    DimensionParams p = dsp.params;
    p.mode = DIMENSION_MODE_II;
    p.bbdCharacter = bbdChar;
    Dimension_SetParams(&dsp, &p);

    float inL[64], inR[64], outL[64], outR[64];
    for (uint32_t i = 0; i < blockSize; ++i) {
        inL[i] = 0.25f * sinf(2.0f * 3.14159f * 440.0f * (float)i / sampleRate);
        inR[i] = 0.25f * cosf(2.0f * 3.14159f * 660.0f * (float)i / sampleRate);
    }

    /* Warm-up: 50 blocks */
    for (uint32_t w = 0; w < 50; ++w) {
        Dimension_ProcessBlock(&dsp, inL, inR, outL, outR, blockSize);
    }

    const uint32_t numBlocks = totalSamples / blockSize;
    const double t0 = get_time_sec();
    for (uint32_t b = 0; b < numBlocks; ++b) {
        Dimension_ProcessBlock(&dsp, inL, inR, outL, outR, blockSize);
    }
    const double t1 = get_time_sec();
    const double elapsedSec = t1 - t0;
    const double totalProcessed = (double)(numBlocks * blockSize);
    const double nsPerSample = (elapsedSec * 1e9) / totalProcessed;
    const double audioSamplePeriodNs = (1e9) / (double)sampleRate;
    const double cpuHostPercent = (nsPerSample / audioSamplePeriodNs) * 100.0;

    /* Theoretical unverified projection on Cortex-M33 @ 250 MHz (~4.0 ns / cycle):
     * Based on assembly instruction count model: ~40 cycles baseline, ~46 cycles with BBD character.
     * Note: This is an analytical estimation, NOT a physical hardware measurement. */
    const double theoreticalCyclesPerSampleM33 = (bbdChar > 0.0f) ? 46.0 : 40.0;
    const double m33BudgetCyclesPerSample = 250000000.0 / (double)sampleRate;
    const double cpuM33TheoreticalEstPercent = (theoreticalCyclesPerSampleM33 / m33BudgetCyclesPerSample) * 100.0;

    BenchResult res;
    res.label = label;
    res.bbdChar = bbdChar;
    res.sampleRate = sampleRate;
    res.blockSize = blockSize;
    res.nsPerSample = nsPerSample;
    res.cpuHostPercent = cpuHostPercent;
    res.cpuM33TheoreticalEstPercent = cpuM33TheoreticalEstPercent;
    return res;
}

int main(void) {
    printf("=================================================================\n");
    printf(" DIMENSION5 — BBD CHARACTER ENGINE 2.1 PERFORMANCE BENCHMARK\n");
    printf(" Platform: Host Desktop (Measured) | STM32H562 @ 250 MHz (Theoretical Model)\n");
    printf(" Static Memory footprint: sizeof(DimensionDSP) = %u bytes (~%.1f kB)\n",
           (unsigned int)sizeof(DimensionDSP), (double)sizeof(DimensionDSP) / 1024.0);
    printf(" Delay buffer size: 2 x %u float samples = %u bytes\n",
           DIMENSION_DELAY_SIZE, (unsigned int)(2 * DIMENSION_DELAY_SIZE * sizeof(float)));
    printf(" Additional BBD Clock state: %u bytes\n", (unsigned int)sizeof(DimensionBBDClock));
    printf("=================================================================\n\n");

    const float rates[3] = { 44100.0f, 48000.0f, 96000.0f };
    const uint32_t blocks[3] = { 16, 32, 64 };
    const float charLevels[4] = { 0.0f, 0.25f, 0.50f, 1.00f };
    const char* charLabels[4] = {
        "BBD Character =   0% (Neutral)",
        "BBD Character =  25% (Discreet)",
        "BBD Character =  50% (Musical)",
        "BBD Character = 100% (Full)"
    };

    printf("--- 1. MEASURED HOST DESKTOP PERFORMANCE & BBD OVERHEAD (48 kHz, Block = 32 frames) ---\n");
    BenchResult baselineRes = benchmark_configuration(charLabels[0], 0.0f, 48000.0f, 32, 48000 * 20);
    printf("%-35s : %6.2f ns/sample | Host CPU: %4.2f%% | Overhead: +0.0%%\n",
           baselineRes.label, baselineRes.nsPerSample, baselineRes.cpuHostPercent);

    for (int i = 1; i < 4; ++i) {
        BenchResult r = benchmark_configuration(charLabels[i], charLevels[i], 48000.0f, 32, 48000 * 20);
        const double overhead = ((r.nsPerSample - baselineRes.nsPerSample) / baselineRes.nsPerSample) * 100.0;
        printf("%-35s : %6.2f ns/sample | Host CPU: %4.2f%% | Overhead: %+4.1f%%\n",
               r.label, r.nsPerSample, r.cpuHostPercent, overhead);
    }

    printf("\n--- 2. HOST SAMPLE RATE SCALING (Block = 32 frames, BBD Character = 50%%) ---\n");
    for (int r = 0; r < 3; ++r) {
        BenchResult res = benchmark_configuration("SR Scaling", 0.50f, rates[r], 32, (uint32_t)rates[r] * 20);
        printf("Sample Rate %5.0f Hz : %6.2f ns/sample | Host CPU: %4.2f%%\n",
               (double)rates[r], res.nsPerSample, res.cpuHostPercent);
    }

    printf("\n--- 3. HOST BLOCK SIZE SCALING (48 kHz, BBD Character = 50%%) ---\n");
    for (int b = 0; b < 3; ++b) {
        BenchResult res = benchmark_configuration("Block Scaling", 0.50f, 48000.0f, blocks[b], 48000 * 20);
        printf("Block Size %2u frames : %6.2f ns/sample | Host CPU: %4.2f%%\n",
               blocks[b], res.nsPerSample, res.cpuHostPercent);
    }

    printf("\n--- 4. THEORETICAL MCU (CORTEX-M33 @ 250 MHz) ANALYTICAL PROJECTIONS ---\n");
    printf("NOTE: The figures below are analytical cycle-budget projections, NOT physical\n");
    printf("hardware measurements. Physical MCU characterization must be verified on STM32H562\n");
    printf("hardware using DWT->CYCCNT or GPIO profiling.\n");
    printf("  - Baseline (0%% character) : ~40 cycles/sample -> ~0.8%% theoretical load @ 48 kHz\n");
    printf("  - With BBD Character     : ~46 cycles/sample -> ~0.9%% theoretical load @ 48 kHz\n");
    printf("  - Budget margin           : 250 MHz FPU allows 5,208 cycles/sample at 48 kHz (budget >> 100x)\n");

    printf("\n=================================================================\n");
    printf(" HARDWARE MEASUREMENT INSTRUCTIONS FOR STM32H562:\n");
    printf(" 1. Enable DWT cycle counter:\n");
    printf("      CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;\n");
    printf("      DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;\n");
    printf(" 2. Measure cycles around Dimension_ProcessBlock():\n");
    printf("      uint32_t t0 = DWT->CYCCNT;\n");
    printf("      Dimension_ProcessBlock(&dsp, inL, inR, outL, outR, n);\n");
    printf("      uint32_t cycles = DWT->CYCCNT - t0;\n");
    printf(" 3. Alternatively, toggle a designated GPIO pin around the block call and\n");
    printf("    measure duty cycle on an oscilloscope or logic analyzer.\n");
    printf("=================================================================\n");

    return 0;
}
