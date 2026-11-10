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

typedef struct RegressionResult {
    const char* id;
    int passed;
    double maxDiffL;
    double maxDiffR;
    double maxDiffOverall;
    double rmsDiffOverall;
    uint32_t divergentSamples;
    uint32_t bitExactSamples;
    uint32_t totalSamples;
    int isBitExact;
} RegressionResult;

static int ensure_single_dir(const char* dir) {
#if defined(_WIN32)
    const int rc = _mkdir(dir);
#else
    const int rc = mkdir(dir, 0777);
#endif
    if (rc == 0 || errno == EEXIST) return 0;
    return -1;
}

static int ensure_dir_recursive(const char* path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    for (size_t i = 1; i < len; ++i) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char slash = tmp[i];
            tmp[i] = '\0';
            ensure_single_dir(tmp);
            tmp[i] = slash;
        }
    }
    return ensure_single_dir(tmp);
}

static float clamp1(float x) { return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x); }

static void le16(FILE* f, uint16_t v) { fputc((int)(v & 255u), f); fputc((int)(v >> 8), f); }
static void le32(FILE* f, uint32_t v) { le16(f, (uint16_t)(v & 65535u)); le16(f, (uint16_t)(v >> 16)); }

static int write_wav(const char* path, const float* l, const float* r, uint32_t frames, uint32_t sampleRate) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    const uint32_t data = frames * 2U * 2U;
    fwrite("RIFF", 1, 4, f); le32(f, 36U + data); fwrite("WAVEfmt ", 1, 8, f);
    le32(f, 16U); le16(f, 1U); le16(f, 2U); le32(f, sampleRate); le32(f, sampleRate * 4U); le16(f, 4U); le16(f, 16U);
    fwrite("data", 1, 4, f); le32(f, data);
    for (uint32_t i = 0; i < frames; ++i) {
        const float lc = clamp1(l[i]); const float rc = clamp1(r[i]);
        const int16_t li = (int16_t)(lc < 0.0f ? lc * 32768.0f : lc * 32767.0f);
        const int16_t ri = (int16_t)(rc < 0.0f ? rc * 32768.0f : rc * 32767.0f);
        le16(f, (uint16_t)li); le16(f, (uint16_t)ri);
    }
    return fclose(f);
}

static int read_bin_file(const char* path, float* l, float* r, uint32_t frames) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    if (fread(l, sizeof(float), frames, f) != frames) { fclose(f); return -1; }
    if (fread(r, sizeof(float), frames, f) != frames) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

typedef enum SignalType {
    SIG_RICH_COMPLEX = 0,
    SIG_MONO_SINE,
    SIG_IMPULSES,
    SIG_DETERMINISTIC_NOISE,
    SIG_SINE_1KHZ,
    SIG_SILENCE
} SignalType;

static void generate_signal(SignalType type, float* inL, float* inR, uint32_t frames, float sr) {
    uint32_t rng = 0x5eed1234u;
    for (uint32_t t = 0; t < frames; ++t) {
        switch (type) {
            case SIG_RICH_COMPLEX: {
                const float sub = 0.20f * sinf(2.0f * PI_F * 55.0f * (float)t / sr);
                const float midL = 0.12f * sinf(2.0f * PI_F * 220.0f * (float)t / sr);
                const float midR = 0.12f * sinf(2.0f * PI_F * 330.0f * (float)t / sr);
                const float chimeL = 0.05f * sinf(2.0f * PI_F * 2500.0f * (float)t / sr);
                const float chimeR = 0.05f * cosf(2.0f * PI_F * 2500.0f * (float)t / sr);
                float transient = 0.0f;
                if (t >= (uint32_t)sr && t < (uint32_t)sr + 128) {
                    transient = 0.40f * (1.0f - (float)(t - (uint32_t)sr) / 128.0f);
                }
                rng = rng * 1664525u + 1013904223u;
                const float n = 0.015f * (((float)((rng >> 8) & 0xFFFFFFu) / 8388608.0f) - 1.0f);
                inL[t] = sub + midL + chimeL + transient + n;
                inR[t] = sub + midR + chimeR + transient + n;
                break;
            }
            case SIG_MONO_SINE: {
                const float s = 0.25f * sinf(2.0f * PI_F * 150.0f * (float)t / sr);
                inL[t] = s;
                inR[t] = s;
                break;
            }
            case SIG_IMPULSES: {
                inL[t] = (t % 1024 == 0) ? 0.8f : 0.0f;
                inR[t] = (t % 1024 == 512) ? 0.8f : 0.0f;
                break;
            }
            case SIG_DETERMINISTIC_NOISE: {
                rng = rng * 1664525u + 1013904223u;
                inL[t] = 0.15f * (((float)((rng >> 8) & 0xFFFFFFu) / 8388608.0f) - 1.0f);
                rng = rng * 1664525u + 1013904223u;
                inR[t] = 0.15f * (((float)((rng >> 8) & 0xFFFFFFu) / 8388608.0f) - 1.0f);
                break;
            }
            case SIG_SINE_1KHZ: {
                const float sL = 0.20f * sinf(2.0f * PI_F * 1000.0f * (float)t / sr);
                const float sR = 0.20f * cosf(2.0f * PI_F * 1000.0f * (float)t / sr);
                inL[t] = sL;
                inR[t] = sR;
                break;
            }
            case SIG_SILENCE:
            default:
                inL[t] = 0.0f;
                inR[t] = 0.0f;
                break;
        }
    }
}

typedef struct TestCase {
    const char* id;
    DimensionMode mode;
    SignalType signal;
    float sampleRate;
    uint32_t frames;
    uint32_t blockSize;
    int isCustom;
    float width;
    float depthMs;
    float rateHz;
    float baseDelayMs;
    float lpfHz;
    float analogAmount;
    float companderAmount;
} TestCase;

static const TestCase kTestCases[] = {
    { "01_mode_I_stereo",           DIMENSION_MODE_I,      SIG_RICH_COMPLEX,        48000.0f, 48000 * 2, 32, 0, 1.0f, 0.9f, 0.25f,  7.0f, 8000.0f, 0.35f, 0.35f },
    { "02_mode_II_stereo",          DIMENSION_MODE_II,     SIG_RICH_COMPLEX,        48000.0f, 48000 * 2, 32, 0, 1.0f, 1.4f, 0.25f,  8.5f, 7500.0f, 0.45f, 0.45f },
    { "03_mode_III_stereo",         DIMENSION_MODE_III,    SIG_RICH_COMPLEX,        48000.0f, 48000 * 2, 32, 0, 1.0f, 1.8f, 0.50f, 10.0f, 7000.0f, 0.50f, 0.50f },
    { "04_mode_IV_stereo",          DIMENSION_MODE_IV,     SIG_RICH_COMPLEX,        48000.0f, 48000 * 2, 32, 0, 1.0f, 2.4f, 0.50f, 11.5f, 6500.0f, 0.60f, 0.60f },
    { "05_custom_params",           DIMENSION_MODE_CUSTOM, SIG_RICH_COMPLEX,        48000.0f, 48000 * 2, 32, 1, 1.2f, 2.2f, 0.75f, 12.0f, 6500.0f, 0.55f, 0.60f },
    { "06_width_zero",              DIMENSION_MODE_I,      SIG_RICH_COMPLEX,        48000.0f, 48000 * 2, 32, 0, 0.0f, 0.9f, 0.25f,  7.0f, 8000.0f, 0.35f, 0.35f },
    { "07_width_half",              DIMENSION_MODE_II,     SIG_RICH_COMPLEX,        48000.0f, 48000 * 2, 32, 0, 0.5f, 1.4f, 0.25f,  8.5f, 7500.0f, 0.45f, 0.45f },
    { "08_width_1_5",               DIMENSION_MODE_III,    SIG_RICH_COMPLEX,        48000.0f, 48000 * 2, 32, 0, 1.5f, 1.8f, 0.50f, 10.0f, 7000.0f, 0.50f, 0.50f },
    { "09_width_2_0",               DIMENSION_MODE_IV,     SIG_RICH_COMPLEX,        48000.0f, 48000 * 2, 32, 0, 2.0f, 2.4f, 0.50f, 11.5f, 6500.0f, 0.60f, 0.60f },
    { "10_varied_depth_rate",       DIMENSION_MODE_CUSTOM, SIG_RICH_COMPLEX,        48000.0f, 48000 * 2, 32, 1, 1.0f, 3.5f, 1.20f,  9.0f, 7000.0f, 0.40f, 0.40f },
    { "11_mono_input",              DIMENSION_MODE_II,     SIG_MONO_SINE,           48000.0f, 48000 * 2, 32, 0, 1.0f, 1.4f, 0.25f,  8.5f, 7500.0f, 0.45f, 0.45f },
    { "12_impulses",                DIMENSION_MODE_I,      SIG_IMPULSES,            48000.0f, 48000 * 2, 32, 0, 1.0f, 0.9f, 0.25f,  7.0f, 8000.0f, 0.35f, 0.35f },
    { "13_deterministic_noise",     DIMENSION_MODE_II,     SIG_DETERMINISTIC_NOISE, 48000.0f, 48000 * 2, 32, 0, 1.0f, 1.4f, 0.25f,  8.5f, 7500.0f, 0.45f, 0.45f },
    { "14_sine_1khz",               DIMENSION_MODE_III,    SIG_SINE_1KHZ,           48000.0f, 48000 * 2, 32, 0, 1.0f, 1.8f, 0.50f, 10.0f, 7000.0f, 0.50f, 0.50f },
    { "15_silence",                 DIMENSION_MODE_IV,     SIG_SILENCE,             48000.0f, 48000 * 2, 32, 0, 1.0f, 2.4f, 0.50f, 11.5f, 6500.0f, 0.60f, 0.60f },
    { "16_block_16",                DIMENSION_MODE_II,     SIG_RICH_COMPLEX,        48000.0f, 48000 * 2, 16, 0, 1.0f, 1.4f, 0.25f,  8.5f, 7500.0f, 0.45f, 0.45f },
    { "17_block_64",                DIMENSION_MODE_II,     SIG_RICH_COMPLEX,        48000.0f, 48000 * 2, 64, 0, 1.0f, 1.4f, 0.25f,  8.5f, 7500.0f, 0.45f, 0.45f },
    { "18_sr_44100",                DIMENSION_MODE_I,      SIG_RICH_COMPLEX,        44100.0f, 44100 * 2, 32, 0, 1.0f, 0.9f, 0.25f,  7.0f, 8000.0f, 0.35f, 0.35f },
    { "19_sr_96000",                DIMENSION_MODE_III,    SIG_RICH_COMPLEX,        96000.0f, 96000 * 2, 32, 0, 1.0f, 1.8f, 0.50f, 10.0f, 7000.0f, 0.50f, 0.50f }
};

static int run_regression_against_baseline(const char* baselineDir, RegressionResult* results, size_t numCases) {
    int allPassed = 1;
    printf("\n=== AUDIO REGRESSION VS AUTHENTIC BASELINE (08903b8) ===\n");

    for (size_t c = 0; c < numCases; ++c) {
        const TestCase* tc = &kTestCases[c];
        char binPath[512];
        snprintf(binPath, sizeof(binPath), "%s/%s.bin", baselineDir, tc->id);

        float* refL = (float*)malloc(tc->frames * sizeof(float));
        float* refR = (float*)malloc(tc->frames * sizeof(float));
        if (read_bin_file(binPath, refL, refR, tc->frames) != 0) {
            fprintf(stderr, "ERROR: Baseline binary missing: %s. Run dimension5_legacy_render first!\n", binPath);
            free(refL); free(refR);
            return 0;
        }

        float* inL = (float*)malloc(tc->frames * sizeof(float));
        float* inR = (float*)malloc(tc->frames * sizeof(float));
        float* curL = (float*)malloc(tc->frames * sizeof(float));
        float* curR = (float*)malloc(tc->frames * sizeof(float));

        generate_signal(tc->signal, inL, inR, tc->frames, tc->sampleRate);

        DimensionDSP dsp;
        Dimension_Init(&dsp, tc->sampleRate);
        Dimension_SetMode(&dsp, tc->mode);
        DimensionParams p;
        Dimension_GetParams(&dsp, &p);
        p.width = tc->width;
        p.stereoWidth = 1.0f; // Neutral spatial engine
        p.bassFocusHz = 0.0f; // Neutral bass focus
        if (tc->isCustom) {
            p.mode = DIMENSION_MODE_CUSTOM;
            p.depthMs = tc->depthMs;
            p.rateHz = tc->rateHz;
            p.baseDelayMs = tc->baseDelayMs;
            p.lpfHz = tc->lpfHz;
            p.analogAmount = tc->analogAmount;
            p.companderAmount = tc->companderAmount;
        }
        Dimension_SetParams(&dsp, &p);

        uint32_t pos = 0;
        while (pos < tc->frames) {
            const uint32_t n = (tc->frames - pos) < tc->blockSize ? (tc->frames - pos) : tc->blockSize;
            Dimension_ProcessBlock(&dsp, inL + pos, inR + pos, curL + pos, curR + pos, n);
            pos += n;
        }

        double maxDiffL = 0.0, maxDiffR = 0.0, sumSqDiff = 0.0;
        uint32_t divergent = 0;
        uint32_t bitExact = 0;
        for (uint32_t i = 0; i < tc->frames; ++i) {
            uint32_t cLb, rLb, cRb, rRb;
            memcpy(&cLb, &curL[i], sizeof(float));
            memcpy(&rLb, &refL[i], sizeof(float));
            memcpy(&cRb, &curR[i], sizeof(float));
            memcpy(&rRb, &refR[i], sizeof(float));
            if (cLb == rLb && cRb == rRb) {
                ++bitExact;
            }
            const double dL = fabs((double)curL[i] - (double)refL[i]);
            const double dR = fabs((double)curR[i] - (double)refR[i]);
            if (dL > maxDiffL) maxDiffL = dL;
            if (dR > maxDiffR) maxDiffR = dR;
            if (dL > 1e-7 || dR > 1e-7) ++divergent;
            sumSqDiff += 0.5 * (dL * dL + dR * dR);
        }
        const double maxOverall = maxDiffL > maxDiffR ? maxDiffL : maxDiffR;
        const double rmsOverall = sqrt(sumSqDiff / (double)tc->frames);

        const int passed = (maxOverall < 1e-5);
        if (!passed) allPassed = 0;

        results[c].id = tc->id;
        results[c].passed = passed;
        results[c].maxDiffL = maxDiffL;
        results[c].maxDiffR = maxDiffR;
        results[c].maxDiffOverall = maxOverall;
        results[c].rmsDiffOverall = rmsOverall;
        results[c].divergentSamples = divergent;
        results[c].bitExactSamples = bitExact;
        results[c].totalSamples = tc->frames;
        results[c].isBitExact = (bitExact == tc->frames);

        const char* statusStr = passed ? (results[c].isBitExact ? "PASS (Bit-exact)" : "PASS (Tolerance)") : "FAIL";
        printf("  %-26s : %-18s | MaxDiff: %.1e (L: %.1e, R: %.1e) | RMS: %.1e | BitExact: %u/%u\n",
               tc->id, statusStr, maxOverall, maxDiffL, maxDiffR, rmsOverall,
               bitExact, tc->frames);

        free(refL); free(refR);
        free(inL); free(inR);
        free(curL); free(curR);
    }
    return allPassed;
}

static int validate_bass_focus(void) {
    printf("\n=== BASS FOCUS AUDIT & FILTER VERIFICATION ===\n");
    int passed = 1;

    {
        DimensionDSP d;
        Dimension_Init(&d, 48000.0f);
        Dimension_SetMode(&d, DIMENSION_MODE_I);

        float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
        for (uint32_t i = 0; i < BLOCK; ++i) {
            inL[i] = 0.20f;
            inR[i] = -0.20f;
        }

        Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);

        DimensionParams p = d.params;
        p.bassFocusHz = 150.0f;
        Dimension_SetParams(&d, &p);

        float prevOut = outL[BLOCK - 1];
        float maxStepDelta = 0.0f;
        for (int b = 0; b < 10; ++b) {
            Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
            for (uint32_t i = 0; i < BLOCK; ++i) {
                const float diff = fabsf(outL[i] - prevOut);
                if (diff > maxStepDelta) maxStepDelta = diff;
                prevOut = outL[i];
            }
        }
        printf("  Off -> On (150 Hz) max transient step: %.4f (smooth block envelope)\n", (double)maxStepDelta);
        if (maxStepDelta > 0.05f) {
            printf("  FAIL: Bass focus activation transition too sharp\n");
            passed = 0;
        }

        p.bassFocusHz = 0.0f;
        Dimension_SetParams(&d, &p);

        float maxOffDelta = 0.0f;
        for (int b = 0; b < 10; ++b) {
            Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
            for (uint32_t i = 0; i < BLOCK; ++i) {
                const float diff = fabsf(outL[i] - prevOut);
                if (diff > maxOffDelta) maxOffDelta = diff;
                prevOut = outL[i];
            }
        }
        printf("  On -> Off max transient step: %.4f\n", (double)maxOffDelta);
        if (maxOffDelta > 0.05f) {
            printf("  FAIL: Bass focus deactivation transition too sharp\n");
            passed = 0;
        }

        // Allow smoothing time constant (~30 ms) to complete full decay (~300 blocks = 400 ms)
        for (int b = 0; b < 300; ++b) {
            Dimension_ProcessBlock(&d, inL, inR, outL, outR, BLOCK);
        }

        if (d.smoothBassFocusAmount != 0.0f || d.bassFocusState != 0.0f) {
            printf("  FAIL: bassFocusState not cleanly reset to zero after deactivation (smoothBassFocusAmount=%.6e, bassFocusState=%.6e)\n",
                   (double)d.smoothBassFocusAmount, (double)d.bassFocusState);
            passed = 0;
        } else {
            printf("  Internal state cleanly reset to 0.0f after deactivation: PASS\n");
        }
    }

    {
        const uint32_t blockSizes[] = { 16, 32, 64, 128, 512 };
        const uint32_t totalFrames = 4800;

        float* refOutL = (float*)malloc(totalFrames * sizeof(float));
        float* refOutR = (float*)malloc(totalFrames * sizeof(float));
        float* testInL = (float*)malloc(totalFrames * sizeof(float));
        float* testInR = (float*)malloc(totalFrames * sizeof(float));

        generate_signal(SIG_RICH_COMPLEX, testInL, testInR, totalFrames, 48000.0f);

        for (size_t bsIdx = 0; bsIdx < 5; ++bsIdx) {
            const uint32_t bs = blockSizes[bsIdx];
            DimensionDSP d;
            Dimension_Init(&d, 48000.0f);
            Dimension_SetMode(&d, DIMENSION_MODE_II);

            float* outL = (float*)malloc(totalFrames * sizeof(float));
            float* outR = (float*)malloc(totalFrames * sizeof(float));

            uint32_t pos = 0;
            while (pos < totalFrames) {
                const uint32_t cbFrames = (totalFrames - pos) < bs ? (totalFrames - pos) : bs;
                const float frac = (float)pos / (float)totalFrames;
                DimensionParams p = d.params;
                p.bassFocusHz = 40.0f + 210.0f * frac;
                Dimension_SetParams(&d, &p);

                uint32_t subPos = 0;
                while (subPos < cbFrames) {
                    const uint32_t n = (cbFrames - subPos) < DIMENSION_MAX_BLOCK_SIZE
                                     ? (cbFrames - subPos) : DIMENSION_MAX_BLOCK_SIZE;
                    Dimension_ProcessBlock(&d, testInL + pos + subPos, testInR + pos + subPos,
                                           outL + pos + subPos, outR + pos + subPos, n);
                    subPos += n;
                }
                pos += cbFrames;
            }

            if (bs == 32) {
                memcpy(refOutL, outL, totalFrames * sizeof(float));
                memcpy(refOutR, outR, totalFrames * sizeof(float));
            } else if (bsIdx > 0) {
                double maxDiff = 0.0;
                for (uint32_t i = 0; i < totalFrames; ++i) {
                    const double dL = fabs((double)outL[i] - (double)refOutL[i]);
                    const double dR = fabs((double)outR[i] - (double)refOutR[i]);
                    if (dL > maxDiff) maxDiff = dL;
                    if (dR > maxDiff) maxDiff = dR;
                }
                printf("  Automation block size %3u vs 32 frames : MaxDiff = %.1e (%s)\n",
                       bs, maxDiff, (maxDiff < 0.05) ? "PASS" : "FAIL");
                if (maxDiff >= 0.05) passed = 0;
            }

            free(outL);
            free(outR);
        }

        free(refOutL); free(refOutR);
        free(testInL); free(testInR);
    }

    return passed;
}

static void characterize_center_integrity(void) {
    printf("\n=== CENTER INTEGRITY & SAFETY LIMITER CHARACTERIZATION ===\n");
    const uint32_t frames = 4800;

    struct {
        const char* condition;
        float inputLevel;
        float stereoWidth;
        float bassFocusHz;
        int asymmetrical;
    } scenarios[] = {
        { "1. Low level (peak ~0.15), linear region",            0.15f, 1.0f,   0.0f, 0 },
        { "2. Low level (peak ~0.15), width = 2.0",              0.15f, 2.0f,   0.0f, 0 },
        { "3. Moderate level (peak ~0.40), width = 2.0, BF=120", 0.40f, 2.0f, 120.0f, 0 },
        { "4. Asymmetrical signal, low level (width=2.0)",       0.20f, 2.0f,   0.0f, 1 },
        { "5. High level near limiter (~0.90), width = 1.0",     0.90f, 1.0f,   0.0f, 0 },
        { "6. Severe overdrive (peak ~1.50, limiter clamping)",  1.50f, 2.0f, 120.0f, 0 }
    };

    for (int s = 0; s < 6; ++s) {
        float inL[4800], inR[4800], outL[4800], outR[4800];
        const float lvl = scenarios[s].inputLevel;
        for (uint32_t t = 0; t < frames; ++t) {
            const float sig = lvl * sinf(2.0f * PI_F * 220.0f * (float)t / 48000.0f);
            inL[t] = sig;
            inR[t] = scenarios[s].asymmetrical ? 0.3f * sig : sig;
        }

        DimensionDSP d;
        Dimension_Init(&d, 48000.0f);
        Dimension_SetMode(&d, DIMENSION_MODE_CUSTOM);
        DimensionParams p;
        Dimension_GetParams(&d, &p);
        p.dryGain = 1.0f;
        p.wetDirectGain = 0.0f;
        p.wetCrossGain = 0.0f;
        p.stereoWidth = scenarios[s].stereoWidth;
        p.bassFocusHz = scenarios[s].bassFocusHz;
        Dimension_SetParams(&d, &p);
        Dimension_Reset(&d);

        for (uint32_t pos = 0; pos < frames; pos += 32) {
            Dimension_ProcessBlock(&d, inL + pos, inR + pos, outL + pos, outR + pos, 32);
        }

        double maxMidDiff = 0.0;
        float peakOutput = 0.0f;
        for (uint32_t i = 1920; i < frames; ++i) {
            const float midIn = 0.5f * (inL[i] + inR[i]);
            const float midOut = 0.5f * (outL[i] + outR[i]);
            const double diff = fabs((double)midOut - (double)midIn);
            if (diff > maxMidDiff) maxMidDiff = diff;
            if (fabsf(outL[i]) > peakOutput) peakOutput = fabsf(outL[i]);
            if (fabsf(outR[i]) > peakOutput) peakOutput = fabsf(outR[i]);
        }

        const char* status = (peakOutput <= 1.20f && maxMidDiff < 1e-4) ? "EXACT INVARIANCE"
                           : (peakOutput >= 1.199f) ? "LIMITER ENGAGED (MID PREDICTABLY BOUNDED)"
                           : "LINEAR REGIME";

        printf("  %-48s : Peak=%.3f | MidDiff=%.1e | %s\n",
               scenarios[s].condition, (double)peakOutput, maxMidDiff, status);
    }
}

static void run_benchmarks(const float* inL, const float* inR, uint32_t frames, FILE* jsonReport) {
    printf("\n=== PERFORMANCE BENCHMARKS (48 kHz, Block Size = 32 frames) ===\n");
    const int iterations = 100;

    float* outL = (float*)malloc(frames * sizeof(float));
    float* outR = (float*)malloc(frames * sizeof(float));

    struct {
        const char* name;
        float stereoWidth;
        float bassFocusHz;
    } configs[] = {
        { "DSP novo: Spatial Engine neutro (width=1.0, bf=0)",   1.0f,   0.0f },
        { "DSP novo: Stereo Width ativo   (width=1.5, bf=0)",   1.5f,   0.0f },
        { "DSP novo: Bass Focus ativo     (width=1.0, bf=120)", 1.0f, 120.0f },
        { "DSP novo: Ambos ativos         (width=1.5, bf=120)", 1.5f, 120.0f }
    };

    if (jsonReport) {
        fprintf(jsonReport, "  \"benchmarks\": [\n");
    }

    double baselineNs = 0.0;
    for (int c = 0; c < 4; ++c) {
        DimensionDSP d;
        Dimension_Init(&d, 48000.0f);
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
        const double totalAudioSec = (double)frames * (double)iterations / 48000.0;
        const double cpuPercent = (totalSec / totalAudioSec) * 100.0;
        const double nsPerSample = (totalSec / ((double)frames * (double)iterations)) * 1.0e9;
        if (c == 0) baselineNs = nsPerSample;
        const double relativeOverhead = (baselineNs > 0.0) ? ((nsPerSample / baselineNs) - 1.0) * 100.0 : 0.0;

        printf("  %-50s : %6.2f ns/sample | CPU: %5.2f%% | Overhead: %+5.1f%%\n",
               configs[c].name, nsPerSample, cpuPercent, relativeOverhead);

        if (jsonReport) {
            fprintf(jsonReport, "    { \"config\": \"%s\", \"nsPerSample\": %.2f, \"cpuPercent\": %.2f, \"overheadPercent\": %.2f }%s\n",
                    configs[c].name, nsPerSample, cpuPercent, relativeOverhead, (c + 1 < 4) ? "," : "");
        }
    }

    if (jsonReport) {
        fprintf(jsonReport, "  ],\n");
    }

    free(outL);
    free(outR);
}

int main(int argc, char** argv) {
    int isSmokeMode = 0;
    const char* baselineDir = "tests/test_vectors/legacy_baseline";
    const char* outAudioDir = "docs/audio/spatial-engine-2.0";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--smoke") == 0) {
            isSmokeMode = 1;
        } else if (strcmp(argv[i], "--full") == 0) {
            isSmokeMode = 0;
        } else if (strncmp(argv[i], "--baseline-dir=", 15) == 0) {
            baselineDir = argv[i] + 15;
        } else if (strncmp(argv[i], "--out-dir=", 10) == 0) {
            outAudioDir = argv[i] + 10;
        }
    }

    printf("=================================================================\n");
    printf(" DIMENSION5 — SPATIAL ENGINE 2.0 VALIDATION HARNESS\n");
    printf(" Execution mode: %s\n", isSmokeMode ? "SMOKE (Fast CI Deterministic)" : "FULL (Extended Analysis & Benchmarks)");
    printf(" Baseline directory: %s\n", baselineDir);
    printf("=================================================================\n");

    const size_t numCases = sizeof(kTestCases) / sizeof(kTestCases[0]);
    RegressionResult* regResults = (RegressionResult*)calloc(numCases, sizeof(RegressionResult));

    int regressionPassed = run_regression_against_baseline(baselineDir, regResults, numCases);
    int bassFocusPassed = validate_bass_focus();
    characterize_center_integrity();

    int overallSuccess = regressionPassed && bassFocusPassed;

    if (!isSmokeMode) {
        ensure_dir_recursive(outAudioDir);

        char jsonReportPath[512];
        snprintf(jsonReportPath, sizeof(jsonReportPath), "%s/spatial_validation_report.json", outAudioDir);
        FILE* jr = fopen(jsonReportPath, "w");
        if (jr) {
            fprintf(jr, "{\n  \"timestamp\": %ld,\n  \"status\": \"%s\",\n",
                    (long)time(NULL), overallSuccess ? "PASSED" : "FAILED");
            fprintf(jr, "  \"regression_cases\": [\n");
            for (size_t c = 0; c < numCases; ++c) {
                fprintf(jr, "    { \"id\": \"%s\", \"passed\": %s, \"maxDiff\": %.2e, \"rmsDiff\": %.2e, \"divergent\": %u }%s\n",
                        regResults[c].id, regResults[c].passed ? "true" : "false",
                        regResults[c].maxDiffOverall, regResults[c].rmsDiffOverall,
                        regResults[c].divergentSamples, (c + 1 < numCases) ? "," : "");
            }
            fprintf(jr, "  ],\n");
        }

        float* inL = (float*)malloc(TOTAL_FRAMES * sizeof(float));
        float* inR = (float*)malloc(TOTAL_FRAMES * sizeof(float));
        float* curL = (float*)malloc(TOTAL_FRAMES * sizeof(float));
        float* curR = (float*)malloc(TOTAL_FRAMES * sizeof(float));
        generate_signal(SIG_RICH_COMPLEX, inL, inR, TOTAL_FRAMES, (float)SR);

        char legacyBinPath[512];
        snprintf(legacyBinPath, sizeof(legacyBinPath), "%s/01_mode_I_stereo.bin", baselineDir);
        if (read_bin_file(legacyBinPath, curL, curR, 48000 * 2) == 0) {
            char path[512];
            snprintf(path, sizeof(path), "%s/01_legacy_reference.wav", outAudioDir);
            write_wav(path, curL, curR, 48000 * 2, SR);
        }

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

            char path[512];
            snprintf(path, sizeof(path), "%s/%s", outAudioDir, renderSpecs[i].filename);
            write_wav(path, curL, curR, TOTAL_FRAMES, SR);
        }

        run_benchmarks(inL, inR, TOTAL_FRAMES, jr);

        if (jr) {
            fprintf(jr, "  \"overallSuccess\": %s\n}\n", overallSuccess ? "true" : "false");
            fclose(jr);
            printf("\nFull machine report saved to: %s\n", jsonReportPath);
        }

        free(inL); free(inR);
        free(curL); free(curR);
    } else {
        char smokeReportPath[512];
        snprintf(smokeReportPath, sizeof(smokeReportPath), "%s/spatial_validation_smoke.json", outAudioDir);
        ensure_dir_recursive(outAudioDir);
        FILE* sr = fopen(smokeReportPath, "w");
        if (sr) {
            fprintf(sr, "{\n  \"timestamp\": %ld,\n  \"status\": \"%s\",\n",
                    (long)time(NULL), overallSuccess ? "PASSED" : "FAILED");
            fprintf(sr, "  \"regression_cases\": [\n");
            for (size_t c = 0; c < numCases; ++c) {
                fprintf(sr, "    { \"id\": \"%s\", \"passed\": %s, \"bitExact\": %s, \"bitExactSamples\": %u, \"maxDiff\": %.2e, \"rmsDiff\": %.2e, \"divergent\": %u }%s\n",
                        regResults[c].id, regResults[c].passed ? "true" : "false",
                        regResults[c].isBitExact ? "true" : "false",
                        regResults[c].bitExactSamples,
                        regResults[c].maxDiffOverall, regResults[c].rmsDiffOverall,
                        regResults[c].divergentSamples, (c + 1 < numCases) ? "," : "");
            }
            fprintf(sr, "  ],\n  \"overallSuccess\": %s\n}\n", overallSuccess ? "true" : "false");
            fclose(sr);
        }
    }

    int allCasesBitExact = 1;
    for (size_t c = 0; c < numCases; ++c) {
        if (!regResults[c].isBitExact) {
            allCasesBitExact = 0;
            break;
        }
    }

    free(regResults);

    printf("\n=================================================================\n");
    if (overallSuccess) {
        printf(" STATUS: >>> VALIDATION SUCCESS (PASSED) <<<\n");
        printf(" Zero audible regression vs authentic legacy baseline (08903b8).\n");
        if (allCasesBitExact) {
            printf(" Equivalence: Mathematically bit-exact (100%% identical sample bits).\n");
        } else {
            printf(" Equivalence: Numerically equivalent within tolerance (maxDiff < 1e-5).\n");
        }
        printf(" Verification criteria:\n");
        printf("   - Maximum difference threshold: 1.0e-5\n");
        printf("   - Audio regression cases tested: %u\n", (unsigned)numCases);
        printf("   - Bit-exact cases: %s\n", allCasesBitExact ? "ALL (100%)" : "Preserved within strict tolerance");
        printf("=================================================================\n");
        return 0;
    } else {
        fprintf(stderr, " STATUS: >>> VALIDATION FAILED <<<\n");
        fprintf(stderr, " Discrepancies detected exceeding safety threshold.\n");
        printf("=================================================================\n");
        return 1;
    }
}
