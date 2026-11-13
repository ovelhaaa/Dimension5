#if !defined(_WIN32)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "dimension_dsp.h"
#include "dimension_dsp_phase_a4.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
static double get_time_sec(void) {
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
}
#else
#include <sys/stat.h>
#include <sys/types.h>
static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

#define BENCH_PI 3.14159265358979323846
#define BENCH_REPETITIONS 11
#define BENCH_WARMUP_BLOCKS 50
#define BENCH_SECONDS 2
#define BENCH_MAX_BLOCK 64

typedef struct SampleStats {
    double median;
    double mean;
    double min;
    double max;
    double spread_percent;
} SampleStats;

static int compare_doubles(const void* a, const void* b) {
    const double da = *(const double*)a;
    const double db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

static SampleStats compute_stats(double* values, int count) {
    SampleStats stats;
    qsort(values, (size_t)count, sizeof(double), compare_doubles);
    stats.min = values[0];
    stats.max = values[count - 1];
    stats.median = (count % 2 == 1)
        ? values[count / 2]
        : 0.5 * (values[count / 2 - 1] + values[count / 2]);
    double sum = 0.0;
    for (int i = 0; i < count; ++i) sum += values[i];
    stats.mean = sum / (double)count;
    stats.spread_percent = (stats.median > 0.0)
        ? ((stats.max - stats.min) / stats.median) * 100.0
        : 0.0;
    return stats;
}

static void fill_input(
    float* inL,
    float* inR,
    uint32_t blockSize,
    uint32_t blockIndex,
    float sampleRate) {
    for (uint32_t i = 0; i < blockSize; ++i) {
        const uint64_t absolute = (uint64_t)blockIndex * (uint64_t)blockSize + (uint64_t)i;
        const double t = (double)absolute / (double)sampleRate;
        inL[i] = (float)(0.25 * sin(2.0 * BENCH_PI * 440.0 * t));
        inR[i] = (float)(0.25 * cos(2.0 * BENCH_PI * 660.0 * t));
    }
}

static double benchmark_b1(
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

    float inL[BENCH_MAX_BLOCK];
    float inR[BENCH_MAX_BLOCK];
    float outL[BENCH_MAX_BLOCK];
    float outR[BENCH_MAX_BLOCK];

    for (uint32_t w = 0; w < BENCH_WARMUP_BLOCKS; ++w) {
        fill_input(inL, inR, blockSize, w, sampleRate);
        Dimension_ProcessBlock(&dsp, inL, inR, outL, outR, blockSize);
    }

    const uint32_t numBlocks = totalSamples / blockSize;
    const double t0 = get_time_sec();
    for (uint32_t b = 0; b < numBlocks; ++b) {
        fill_input(inL, inR, blockSize, BENCH_WARMUP_BLOCKS + b, sampleRate);
        Dimension_ProcessBlock(&dsp, inL, inR, outL, outR, blockSize);
    }
    const double t1 = get_time_sec();
    return ((t1 - t0) * 1e9) / (double)(numBlocks * blockSize);
}

static double benchmark_a4(
    float bbdChar,
    float sampleRate,
    uint32_t blockSize,
    uint32_t totalSamples) {
    DimensionDSP_phase_a4 dsp;
    Dimension_Init_phase_a4(&dsp, sampleRate);
    Dimension_SetMode_phase_a4(&dsp, DIMENSION_MODE_II_phase_a4);
    DimensionParams_phase_a4 p = dsp.params;
    p.bbdCharacter = bbdChar;
    Dimension_SetParams_phase_a4(&dsp, &p);

    float inL[BENCH_MAX_BLOCK];
    float inR[BENCH_MAX_BLOCK];
    float outL[BENCH_MAX_BLOCK];
    float outR[BENCH_MAX_BLOCK];

    for (uint32_t w = 0; w < BENCH_WARMUP_BLOCKS; ++w) {
        fill_input(inL, inR, blockSize, w, sampleRate);
        Dimension_ProcessBlock_phase_a4(&dsp, inL, inR, outL, outR, blockSize);
    }

    const uint32_t numBlocks = totalSamples / blockSize;
    const double t0 = get_time_sec();
    for (uint32_t b = 0; b < numBlocks; ++b) {
        fill_input(inL, inR, blockSize, BENCH_WARMUP_BLOCKS + b, sampleRate);
        Dimension_ProcessBlock_phase_a4(&dsp, inL, inR, outL, outR, blockSize);
    }
    const double t1 = get_time_sec();
    return ((t1 - t0) * 1e9) / (double)(numBlocks * blockSize);
}

static int create_output_dir(const char* out_dir) {
#if defined(_WIN32)
    const int rc = _mkdir(out_dir);
#else
    const int rc = mkdir(out_dir, 0777);
#endif
    if (rc == 0 || errno == EEXIST) return 0;
    fprintf(stderr, "failed to create output directory %s: %s\n", out_dir, strerror(errno));
    return -1;
}

static uint64_t java_style_hash(const char* text) {
    uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char* p = (const unsigned char*)text; *p != 0; ++p) {
        hash ^= (uint64_t)(*p);
        hash *= 1099511628211ULL;
    }
    return hash;
}

int main(int argc, char** argv) {
    const char* out_dir = "out";
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            out_dir = argv[i];
            break;
        }
    }
    if (create_output_dir(out_dir) != 0) return 1;

    printf("=================================================================\n");
    printf(" DIMENSION5 — PHASE A.4 vs PHASE B.1 INCREMENTAL BENCHMARK\n");
    printf(" Measured host desktop performance. Cortex-M33 figures are NOT\n");
    printf(" presented as measured hardware results.\n");
    printf("=================================================================\n");
    printf(" Phase A.4 instance: %u bytes | Phase B.1 instance: %u bytes\n",
           (unsigned int)sizeof(DimensionDSP_phase_a4),
           (unsigned int)sizeof(DimensionDSP));
    printf(" Delay buffers: 2 x %u float samples = %u bytes per instance\n",
           DIMENSION_DELAY_SIZE,
           (unsigned int)(2 * DIMENSION_DELAY_SIZE * sizeof(float)));
    printf(" Repetitions per configuration: %d | warm-up blocks: %d | audio per repetition: %d s\n\n",
           BENCH_REPETITIONS, BENCH_WARMUP_BLOCKS, BENCH_SECONDS);

    const float rates[3] = { 44100.0f, 48000.0f, 96000.0f };
    const uint32_t blocks[3] = { 16, 32, 64 };
    const float charLevels[3] = { 0.0f, 0.50f, 1.00f };

    char jsonPath[512];
    snprintf(jsonPath, sizeof(jsonPath), "%s/phase_b1_incremental_benchmark.json", out_dir);
    FILE* json = fopen(jsonPath, "wb");
    if (!json) {
        fprintf(stderr, "failed to open %s\n", jsonPath);
        return 3;
    }

    fprintf(json, "{\n");
    fprintf(json, "  \"metadata\": {\n");
    fprintf(json, "    \"title\": \"Dimension5 Phase A.4 vs B.1 Incremental Benchmark\",\n");
    const char* phaseB1Commit = getenv("GITHUB_SHA");
    if (phaseB1Commit == NULL || phaseB1Commit[0] == '\0') phaseB1Commit = "local-working-tree";
    fprintf(json, "    \"phase_a4_baseline_commit\": \"37591d09ee32647910678400b95075d6d7118a87\",\n");
    fprintf(json, "    \"phase_b1_branch\": \"feature/bbd-spectral-character-phase-b1\",\n");
    fprintf(json, "    \"phase_b1_commit\": \"%s\",\n", phaseB1Commit);
    fprintf(json, "    \"measurement_environment\": \"host desktop (measured wall-clock)\",\n");
    fprintf(json, "    \"hardware_estimates\": \"not measured; Cortex-M33 projections are excluded from this JSON\",\n");
    fprintf(json, "    \"methodology\": \"identical compiler/flags/input/parameters; warm-up before timing; median of repeated runs\",\n");
    fprintf(json, "    \"compiler\": \"%s\",\n",
#if defined(_MSC_VER)
            "MSVC"
#elif defined(__clang__)
            "Clang"
#elif defined(__GNUC__)
            "GCC"
#else
            "unknown"
#endif
    );
    fprintf(json, "    \"config_hash\": \"%016llx\"\n",
            (unsigned long long)java_style_hash(
                "sr=44100,48000,96000;blocks=16,32,64;char=0,0.5,1.0;mode=II;warmup=50;reps=11"));
    fprintf(json, "  },\n");
    fprintf(json, "  \"configuration\": {\n");
    fprintf(json, "    \"sample_rates_hz\": [44100, 48000, 96000],\n");
    fprintf(json, "    \"block_sizes\": [16, 32, 64],\n");
    fprintf(json, "    \"character_levels\": [0.0, 0.5, 1.0],\n");
    fprintf(json, "    \"mode\": \"DIMENSION_MODE_II\",\n");
    fprintf(json, "    \"warmup_blocks\": 50,\n");
    fprintf(json, "    \"measurement_seconds_per_repetition\": 2,\n");
    fprintf(json, "    \"repetitions\": %d,\n", BENCH_REPETITIONS);
    fprintf(json, "    \"aggregation\": \"median ns/sample over repetitions\"\n");
    fprintf(json, "  },\n");
    fprintf(json, "  \"memory\": {\n");
    fprintf(json, "    \"phase_a4\": {\"instance_bytes\": %u, \"delay_buffers_bytes_included\": %u, \"non_delay_bytes\": %u},\n",
            (unsigned int)sizeof(DimensionDSP_phase_a4),
            (unsigned int)(2 * DIMENSION_DELAY_SIZE * sizeof(float)),
            (unsigned int)(sizeof(DimensionDSP_phase_a4) - 2 * DIMENSION_DELAY_SIZE * sizeof(float)));
    fprintf(json, "    \"phase_b1\": {\"instance_bytes\": %u, \"delay_buffers_bytes_included\": %u, \"non_delay_bytes\": %u},\n",
            (unsigned int)sizeof(DimensionDSP),
            (unsigned int)(2 * DIMENSION_DELAY_SIZE * sizeof(float)),
            (unsigned int)(sizeof(DimensionDSP) - 2 * DIMENSION_DELAY_SIZE * sizeof(float)));
    fprintf(json, "    \"incremental_state_bytes\": %d\n",
            (int)((long)sizeof(DimensionDSP) - (long)sizeof(DimensionDSP_phase_a4)));
    fprintf(json, "  },\n");
    fprintf(json, "  \"measurements\": [\n");

    int firstEmitted = 1;
    for (int r = 0; r < 3; ++r) {
        const float sr = rates[r];
        const uint32_t totalSamples = (uint32_t)((double)sr * (double)BENCH_SECONDS);
        for (int b = 0; b < 3; ++b) {
            const uint32_t blockSize = blocks[b];
            for (int c = 0; c < 3; ++c) {
                const float character = charLevels[c];
                double nsA4[BENCH_REPETITIONS];
                double nsB1[BENCH_REPETITIONS];
                for (int rep = 0; rep < BENCH_REPETITIONS; ++rep) {
                    /* Alternate order to cancel slow host-frequency drift bias. */
                    if ((rep & 1) == 0) {
                        nsA4[rep] = benchmark_a4(character, sr, blockSize, totalSamples);
                        nsB1[rep] = benchmark_b1(character, sr, blockSize, totalSamples);
                    } else {
                        nsB1[rep] = benchmark_b1(character, sr, blockSize, totalSamples);
                        nsA4[rep] = benchmark_a4(character, sr, blockSize, totalSamples);
                    }
                }
                const SampleStats statsA4 = compute_stats(nsA4, BENCH_REPETITIONS);
                const SampleStats statsB1 = compute_stats(nsB1, BENCH_REPETITIONS);

                const double overheadPercent =
                    ((statsB1.median - statsA4.median) / statsA4.median) * 100.0;
                const double incrementalNs = statsB1.median - statsA4.median;
                const double cpuSecondsPerAudioSecond48k = (statsB1.median * 48000.0) / 1e9;
                const double hostRealtimeLoadPercent =
                    (statsB1.median * (double)sr / 1e9) * 100.0;

                if (!firstEmitted) fprintf(json, ",\n");
                firstEmitted = 0;
                fprintf(json, "    {\n");
                fprintf(json, "      \"sample_rate_hz\": %.0f,\n", (double)sr);
                fprintf(json, "      \"block_size\": %u,\n", blockSize);
                fprintf(json, "      \"character\": %.2f,\n", (double)character);
                fprintf(json, "      \"phase_a4_ns_per_sample\": %.4f,\n", statsA4.median);
                fprintf(json, "      \"phase_b1_ns_per_sample\": %.4f,\n", statsB1.median);
                fprintf(json, "      \"incremental_ns_per_sample\": %.4f,\n", incrementalNs);
                fprintf(json, "      \"incremental_overhead_percent\": %.2f,\n", overheadPercent);
                fprintf(json, "      \"phase_b1_cpu_seconds_per_audio_second_at_48k\": %.9f,\n",
                        cpuSecondsPerAudioSecond48k);
                fprintf(json, "      \"phase_b1_host_realtime_load_percent\": %.4f,\n",
                        hostRealtimeLoadPercent);
                fprintf(json, "      \"phase_a4_stats\": {\"median_ns\": %.4f, \"mean_ns\": %.4f, \"min_ns\": %.4f, \"max_ns\": %.4f, \"spread_percent\": %.2f},\n",
                        statsA4.median, statsA4.mean, statsA4.min, statsA4.max, statsA4.spread_percent);
                fprintf(json, "      \"phase_b1_stats\": {\"median_ns\": %.4f, \"mean_ns\": %.4f, \"min_ns\": %.4f, \"max_ns\": %.4f, \"spread_percent\": %.2f}\n",
                        statsB1.median, statsB1.mean, statsB1.min, statsB1.max, statsB1.spread_percent);
                fprintf(json, "    }");

                printf("SR %5.0f | block %2u | char %3.0f%% | A.4 %7.2f ns | B.1 %7.2f ns | incremental %+6.2f%%\n",
                       (double)sr, blockSize, (double)(character * 100.0f),
                       statsA4.median, statsB1.median, overheadPercent);
            }
        }
    }

    fprintf(json, "\n  ]\n");
    fprintf(json, "}\n");
    fclose(json);

    printf("\nJSON report: %s\n", jsonPath);
    return 0;
}
