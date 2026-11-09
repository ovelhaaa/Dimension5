#include "dimension_dsp.h"

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
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define SR 48000U
#define BLOCK DIMENSION_MAX_BLOCK_SIZE
#define SECONDS 4U
#define TOTAL_FRAMES (SR * SECONDS)
#define PI_F 3.14159265358979323846f

typedef struct AudioMetrics {
    float peakL;
    float peakR;
    double rmsL;
    double rmsR;
    double rmsMid;
    double rmsSide;
    double sideMidRatio;
    double correlation;
    double maxDiffFromRef;
    double rmsDiffFromRef;
} AudioMetrics;

static uint32_t g_rng = 0x5eed1234u;
static float clamp1(float x) { return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x); }
static float noise_sample(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return ((float)((g_rng >> 8) & 0xFFFFFFu) / 8388608.0f) - 1.0f;
}

static void le16(FILE* f, uint16_t v) { fputc((int)(v & 255u), f); fputc((int)(v >> 8), f); }
static void le32(FILE* f, uint32_t v) { le16(f, (uint16_t)(v & 65535u)); le16(f, (uint16_t)(v >> 16)); }

static int write_wav(const char* path, const float* l, const float* r, uint32_t frames) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    const uint32_t data = frames * 2U * 2U;
    fwrite("RIFF", 1, 4, f); le32(f, 36U + data); fwrite("WAVEfmt ", 1, 8, f);
    le32(f, 16U); le16(f, 1U); le16(f, 2U); le32(f, SR); le32(f, SR * 4U); le16(f, 4U); le16(f, 16U);
    fwrite("data", 1, 4, f); le32(f, data);
    for (uint32_t i = 0; i < frames; ++i) {
        const float lc = clamp1(l[i]); const float rc = clamp1(r[i]);
        const int16_t li = (int16_t)(lc < 0.0f ? lc * 32768.0f : lc * 32767.0f);
        const int16_t ri = (int16_t)(rc < 0.0f ? rc * 32768.0f : rc * 32767.0f);
        le16(f, (uint16_t)li); le16(f, (uint16_t)ri);
    }
    return fclose(f);
}

static int ensure_dir(const char* dir) {
#if defined(_WIN32)
    const int rc = _mkdir(dir);
#else
    const int rc = mkdir(dir, 0777);
#endif
    if (rc == 0 || errno == EEXIST) return 0;
    return -1;
}

static void generate_test_signal(float* inL, float* inR, uint32_t frames) {
    g_rng = 0x5eed1234u;
    for (uint32_t t = 0; t < frames; ++t) {
        /* Complex rich test signal:
         * 1. Low bass kick / sub at 55 Hz (mono center)
         * 2. Stereo chord at 220 Hz & 330 Hz
         * 3. Transient burst at 1.0s
         * 4. High-frequency chime at 2500 Hz
         * 5. Light deterministic pink/white noise */
        const float sub = 0.20f * sinf(2.0f * PI_F * 55.0f * (float)t / (float)SR);
        const float midL = 0.12f * sinf(2.0f * PI_F * 220.0f * (float)t / (float)SR);
        const float midR = 0.12f * sinf(2.0f * PI_F * 330.0f * (float)t / (float)SR);
        const float chimeL = 0.05f * sinf(2.0f * PI_F * 2500.0f * (float)t / (float)SR);
        const float chimeR = 0.05f * cosf(2.0f * PI_F * 2500.0f * (float)t / (float)SR);
        float transient = 0.0f;
        if (t >= SR && t < SR + 128) {
            transient = 0.40f * (1.0f - (float)(t - SR) / 128.0f);
        }
        const float n = 0.015f * noise_sample();

        inL[t] = sub + midL + chimeL + transient + n;
        inR[t] = sub + midR + chimeR + transient + n;
    }
}

static void compute_metrics(const float* l, const float* r, const float* refL, const float* refR,
                            uint32_t frames, AudioMetrics* m) {
    double sumL = 0.0, sumR = 0.0;
    double sumMid = 0.0, sumSide = 0.0;
    double sumLR = 0.0;
    double maxDiff = 0.0, sumDiffSq = 0.0;
    float peakL = 0.0f, peakR = 0.0f;

    for (uint32_t i = 0; i < frames; ++i) {
        const float aL = fabsf(l[i]);
        const float aR = fabsf(r[i]);
        if (aL > peakL) peakL = aL;
        if (aR > peakR) peakR = aR;

        sumL += (double)l[i] * (double)l[i];
        sumR += (double)r[i] * (double)r[i];
        sumLR += (double)l[i] * (double)r[i];

        const double mid = 0.5 * ((double)l[i] + (double)r[i]);
        const double side = 0.5 * ((double)l[i] - (double)r[i]);
        sumMid += mid * mid;
        sumSide += side * side;

        if (refL && refR) {
            const double dL = (double)l[i] - (double)refL[i];
            const double dR = (double)r[i] - (double)refR[i];
            const double adL = fabs(dL);
            const double adR = fabs(dR);
            if (adL > maxDiff) maxDiff = adL;
            if (adR > maxDiff) maxDiff = adR;
            sumDiffSq += 0.5 * (dL * dL + dR * dR);
        }
    }

    m->peakL = peakL;
    m->peakR = peakR;
    m->rmsL = sqrt(sumL / (double)frames);
    m->rmsR = sqrt(sumR / (double)frames);
    m->rmsMid = sqrt(sumMid / (double)frames);
    m->rmsSide = sqrt(sumSide / (double)frames);
    m->sideMidRatio = (m->rmsMid > 1e-9) ? (m->rmsSide / m->rmsMid) : 0.0;

    const double denom = sqrt(sumL * sumR);
    m->correlation = (denom > 1e-12) ? (sumLR / denom) : 1.0;
    m->maxDiffFromRef = maxDiff;
    m->rmsDiffFromRef = sqrt(sumDiffSq / (double)frames);
}

static void run_benchmarks(const float* inL, const float* inR, uint32_t frames) {
    printf("\n=== PERFORMANCE BENCHMARK (48 kHz, Block Size = 32 frames) ===\n");
    const int iterations = 100;

    float* outL = (float*)malloc(frames * sizeof(float));
    float* outR = (float*)malloc(frames * sizeof(float));

    struct {
        const char* name;
        float stereoWidth;
        float bassFocusHz;
    } configs[] = {
        { "1. Neutral / Bypass (stereoWidth=1.0, bassFocus=0)", 1.0f, 0.0f },
        { "2. Stereo Width Only (stereoWidth=1.5, bassFocus=0)", 1.5f, 0.0f },
        { "3. Bass Focus Only (stereoWidth=1.0, bassFocus=120)", 1.0f, 120.0f },
        { "4. Spatial Engine Full (stereoWidth=1.5, bassFocus=120)", 1.5f, 120.0f }
    };

    for (int c = 0; c < 4; ++c) {
        DimensionDSP d;
        Dimension_Init(&d, (float)SR);
        Dimension_SetMode(&d, DIMENSION_MODE_II);
        DimensionParams p;
        Dimension_GetParams(&d, &p);
        p.stereoWidth = configs[c].stereoWidth;
        p.bassFocusHz = configs[c].bassFocusHz;
        Dimension_SetParams(&d, &p);

#if defined(_WIN32)
        LARGE_INTEGER freq, t0, t1;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t0);
        for (int it = 0; it < iterations; ++it) {
            Dimension_Reset(&d);
            for (uint32_t pos = 0; pos < frames; pos += 32) {
                Dimension_ProcessBlock(&d, inL + pos, inR + pos, outL + pos, outR + pos, 32);
            }
        }
        QueryPerformanceCounter(&t1);
        const double totalSec = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
#else
        clock_t t0 = clock();
        for (int it = 0; it < iterations; ++it) {
            Dimension_Reset(&d);
            for (uint32_t pos = 0; pos < frames; pos += 32) {
                Dimension_ProcessBlock(&d, inL + pos, inR + pos, outL + pos, outR + pos, 32);
            }
        }
        clock_t t1 = clock();
        const double totalSec = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
#endif
        const double totalAudioSec = (double)frames * (double)iterations / (double)SR;
        const double cpuPercent = (totalSec / totalAudioSec) * 100.0;
        const double nsPerSample = (totalSec / ((double)frames * (double)iterations)) * 1.0e9;

        printf("%-56s : %6.2f ns/sample | CPU: %5.2f%%\n",
               configs[c].name, nsPerSample, cpuPercent);
    }

    free(outL);
    free(outR);
}

int main(void) {
    ensure_dir("docs");
    ensure_dir("docs/audio");
    ensure_dir("docs/audio/spatial-engine-2.0");
    const char* out_dir = "docs/audio/spatial-engine-2.0";

    float* inL = (float*)malloc(TOTAL_FRAMES * sizeof(float));
    float* inR = (float*)malloc(TOTAL_FRAMES * sizeof(float));
    float* refL = (float*)malloc(TOTAL_FRAMES * sizeof(float));
    float* refR = (float*)malloc(TOTAL_FRAMES * sizeof(float));
    float* curL = (float*)malloc(TOTAL_FRAMES * sizeof(float));
    float* curR = (float*)malloc(TOTAL_FRAMES * sizeof(float));

    generate_test_signal(inL, inR, TOTAL_FRAMES);

    printf("=== SPATIAL ENGINE 2.0 VALIDATION & AUDIO RENDERING ===\n");

    /* 1. Legacy Reference (Mode I default) */
    {
        DimensionDSP d;
        Dimension_Init(&d, (float)SR);
        Dimension_SetMode(&d, DIMENSION_MODE_I);
        for (uint32_t pos = 0; pos < TOTAL_FRAMES; pos += BLOCK) {
            Dimension_ProcessBlock(&d, inL + pos, inR + pos, refL + pos, refR + pos, BLOCK);
        }
        char path[256];
        snprintf(path, sizeof(path), "%s/01_legacy_reference.wav", out_dir);
        write_wav(path, refL, refR, TOTAL_FRAMES);

        AudioMetrics m;
        compute_metrics(refL, refR, NULL, NULL, TOTAL_FRAMES, &m);
        printf("%-30s | Peak: %.4f | RMS: %.4f | Side/Mid: %.4f | Corr: %.4f\n",
               "01_legacy_reference.wav", (double)(m.peakL > m.peakR ? m.peakL : m.peakR),
               0.5*(m.rmsL + m.rmsR), m.sideMidRatio, m.correlation);
    }

    /* Test configurations */
    struct {
        const char* filename;
        float stereoWidth;
        float bassFocusHz;
        int isSweep;
    } renderSpecs[] = {
        { "02_neutral_new_engine.wav", 1.0f,   0.0f, 0 },
        { "03_width_0_mono.wav",        0.0f,   0.0f, 0 },
        { "04_width_1_reference.wav",   1.0f,   0.0f, 0 },
        { "05_width_1_5.wav",           1.5f,   0.0f, 0 },
        { "06_width_2.wav",             2.0f,   0.0f, 0 },
        { "07_bass_focus_80hz.wav",     1.0f,  80.0f, 0 },
        { "08_bass_focus_120hz.wav",    1.0f, 120.0f, 0 },
        { "09_bass_focus_200hz.wav",    1.0f, 200.0f, 0 },
        { "10_automation_sweep.wav",    1.0f,   0.0f, 1 }
    };

    for (int i = 0; i < 9; ++i) {
        DimensionDSP d;
        Dimension_Init(&d, (float)SR);
        Dimension_SetMode(&d, DIMENSION_MODE_I);

        DimensionParams p;
        Dimension_GetParams(&d, &p);
        p.stereoWidth = renderSpecs[i].stereoWidth;
        p.bassFocusHz = renderSpecs[i].bassFocusHz;
        Dimension_SetParams(&d, &p);

        if (renderSpecs[i].isSweep) {
            /* Sweep stereoWidth from 0.0 -> 2.0 and bassFocus from 0 -> 250 Hz */
            for (uint32_t pos = 0; pos < TOTAL_FRAMES; pos += BLOCK) {
                const float frac = (float)pos / (float)TOTAL_FRAMES;
                p.stereoWidth = 2.0f * frac;
                p.bassFocusHz = 40.0f + 210.0f * frac;
                Dimension_SetParams(&d, &p);
                Dimension_ProcessBlock(&d, inL + pos, inR + pos, curL + pos, curR + pos, BLOCK);
            }
        } else {
            for (uint32_t pos = 0; pos < TOTAL_FRAMES; pos += BLOCK) {
                Dimension_ProcessBlock(&d, inL + pos, inR + pos, curL + pos, curR + pos, BLOCK);
            }
        }

        char path[256];
        snprintf(path, sizeof(path), "%s/%s", out_dir, renderSpecs[i].filename);
        write_wav(path, curL, curR, TOTAL_FRAMES);

        AudioMetrics m;
        compute_metrics(curL, curR, refL, refR, TOTAL_FRAMES, &m);
        printf("%-30s | Peak: %.4f | RMS: %.4f | Side/Mid: %.4f | Corr: %+.4f | DiffRef: max=%.1e rms=%.1e\n",
               renderSpecs[i].filename, (double)(m.peakL > m.peakR ? m.peakL : m.peakR),
               0.5*(m.rmsL + m.rmsR), m.sideMidRatio, m.correlation,
               m.maxDiffFromRef, m.rmsDiffFromRef);
    }

    run_benchmarks(inL, inR, TOTAL_FRAMES);

    free(inL); free(inR);
    free(refL); free(refR);
    free(curL); free(curR);
    return 0;
}
