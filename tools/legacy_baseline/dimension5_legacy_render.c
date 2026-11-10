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

#define PI_F 3.14159265358979323846f

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

static void le16(FILE* f, uint16_t v) { fputc((int)(v & 255u), f); fputc((int)(v >> 8), f); }
static void le32(FILE* f, uint32_t v) { le16(f, (uint16_t)(v & 65535u)); le16(f, (uint16_t)(v >> 16)); }

static int write_wav_file(const char* path, const float* l, const float* r, uint32_t frames, uint32_t sampleRate) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    const uint32_t data = frames * 2U * 2U;
    fwrite("RIFF", 1, 4, f); le32(f, 36U + data); fwrite("WAVEfmt ", 1, 8, f);
    le32(f, 16U); le16(f, 1U); le16(f, 2U); le32(f, sampleRate); le32(f, sampleRate * 4U); le16(f, 4U); le16(f, 16U);
    fwrite("data", 1, 4, f); le32(f, data);
    for (uint32_t i = 0; i < frames; ++i) {
        float lc = l[i] < -1.0f ? -1.0f : (l[i] > 1.0f ? 1.0f : l[i]);
        float rc = r[i] < -1.0f ? -1.0f : (r[i] > 1.0f ? 1.0f : r[i]);
        const int16_t li = (int16_t)(lc < 0.0f ? lc * 32768.0f : lc * 32767.0f);
        const int16_t ri = (int16_t)(rc < 0.0f ? rc * 32768.0f : rc * 32767.0f);
        le16(f, (uint16_t)li); le16(f, (uint16_t)ri);
    }
    return fclose(f);
}

static int write_bin_file(const char* path, const float* l, const float* r, uint32_t frames) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    if (fwrite(l, sizeof(float), frames, f) != frames) { fclose(f); return -1; }
    if (fwrite(r, sizeof(float), frames, f) != frames) { fclose(f); return -1; }
    return fclose(f);
}

/* 64-bit FNV-1a hash of float data */
static uint64_t compute_fnv1a(const float* l, const float* r, uint32_t frames) {
    uint64_t h = 14695981039346656037ULL;
    const uint8_t* pL = (const uint8_t*)l;
    const uint8_t* pR = (const uint8_t*)r;
    const size_t sz = frames * sizeof(float);
    for (size_t i = 0; i < sz; ++i) {
        h ^= (uint64_t)pL[i];
        h *= 1099511628211ULL;
    }
    for (size_t i = 0; i < sz; ++i) {
        h ^= (uint64_t)pR[i];
        h *= 1099511628211ULL;
    }
    return h;
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

int main(int argc, char** argv) {
    const char* outDir = (argc > 1) ? argv[1] : "tests/test_vectors/legacy_baseline";
    ensure_dir_recursive(outDir);

    printf("=== RENDERING AUTHENTIC LEGACY DSP BASELINE (COMMIT 08903b8) ===\n");
    printf("Output target: %s\n", outDir);

    char manifestPath[512];
    snprintf(manifestPath, sizeof(manifestPath), "%s/manifest.json", outDir);
    FILE* mf = fopen(manifestPath, "w");
    if (!mf) {
        fprintf(stderr, "Failed to create manifest file %s\n", manifestPath);
        return 1;
    }

    fprintf(mf, "{\n  \"baseline_commit\": \"08903b86de41\",\n  \"test_cases\": [\n");

    const size_t numCases = sizeof(kTestCases) / sizeof(kTestCases[0]);
    for (size_t c = 0; c < numCases; ++c) {
        const TestCase* tc = &kTestCases[c];
        printf("[%2zu/%2zu] Rendering %-28s (SR: %5.0f, Block: %2u, Frames: %6u)... ",
               c + 1, numCases, tc->id, (double)tc->sampleRate, tc->blockSize, tc->frames);

        float* inL = (float*)malloc(tc->frames * sizeof(float));
        float* inR = (float*)malloc(tc->frames * sizeof(float));
        float* outL = (float*)malloc(tc->frames * sizeof(float));
        float* outR = (float*)malloc(tc->frames * sizeof(float));

        generate_signal(tc->signal, inL, inR, tc->frames, tc->sampleRate);

        DimensionDSP dsp;
        Dimension_Init(&dsp, tc->sampleRate);
        Dimension_SetMode(&dsp, tc->mode);
        DimensionParams p;
        Dimension_GetParams(&dsp, &p);
        p.width = tc->width;
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

        /* Process in blocks */
        uint32_t pos = 0;
        while (pos < tc->frames) {
            const uint32_t n = (tc->frames - pos) < tc->blockSize ? (tc->frames - pos) : tc->blockSize;
            Dimension_ProcessBlock(&dsp, inL + pos, inR + pos, outL + pos, outR + pos, n);
            pos += n;
        }

        /* Calculate metrics */
        float peakL = 0.0f, peakR = 0.0f;
        double sumSqL = 0.0, sumSqR = 0.0;
        for (uint32_t i = 0; i < tc->frames; ++i) {
            const float aL = fabsf(outL[i]);
            const float aR = fabsf(outR[i]);
            if (aL > peakL) peakL = aL;
            if (aR > peakR) peakR = aR;
            sumSqL += (double)outL[i] * (double)outL[i];
            sumSqR += (double)outR[i] * (double)outR[i];
        }
        const double rmsL = sqrt(sumSqL / (double)tc->frames);
        const double rmsR = sqrt(sumSqR / (double)tc->frames);
        const uint64_t fnv = compute_fnv1a(outL, outR, tc->frames);

        /* Write .bin and .wav */
        char binPath[512], wavPath[512];
        snprintf(binPath, sizeof(binPath), "%s/%s.bin", outDir, tc->id);
        snprintf(wavPath, sizeof(wavPath), "%s/%s.wav", outDir, tc->id);
        write_bin_file(binPath, outL, outR, tc->frames);
        write_wav_file(wavPath, outL, outR, tc->frames, (uint32_t)tc->sampleRate);

        printf("Peak: %.4f | RMS: %.4f | FNV: %016llx\n",
               (double)(peakL > peakR ? peakL : peakR), 0.5 * (rmsL + rmsR), (unsigned long long)fnv);

        fprintf(mf, "    {\n");
        fprintf(mf, "      \"id\": \"%s\",\n", tc->id);
        fprintf(mf, "      \"mode\": %d,\n", (int)tc->mode);
        fprintf(mf, "      \"sampleRate\": %.1f,\n", (double)tc->sampleRate);
        fprintf(mf, "      \"frames\": %u,\n", tc->frames);
        fprintf(mf, "      \"blockSize\": %u,\n", tc->blockSize);
        fprintf(mf, "      \"width\": %.2f,\n", (double)tc->width);
        fprintf(mf, "      \"peakL\": %.6f,\n", (double)peakL);
        fprintf(mf, "      \"peakR\": %.6f,\n", (double)peakR);
        fprintf(mf, "      \"rmsL\": %.6f,\n", (double)rmsL);
        fprintf(mf, "      \"rmsR\": %.6f,\n", (double)rmsR);
        fprintf(mf, "      \"fnv1a\": \"%016llx\"\n", (unsigned long long)fnv);
        fprintf(mf, "    }%s\n", (c + 1 < numCases) ? "," : "");

        free(inL);
        free(inR);
        free(outL);
        free(outR);
    }

    fprintf(mf, "  ]\n}\n");
    fclose(mf);

    printf("Manifest saved to %s\n", manifestPath);
    printf("Authentic legacy DSP baseline render complete!\n");
    return 0;
}
