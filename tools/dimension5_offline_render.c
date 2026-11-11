#include "dimension_dsp.h"
#include "dimension_dsp_e153da5.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define SR_DEFAULT 48000U
#define BLOCK DIMENSION_MAX_BLOCK_SIZE
#define SECONDS_SMOKE 4U
#define SECONDS_SPECTRAL 16U
#define PI_F 3.14159265358979323846f

typedef struct Stats { float peak; double sum; double energy; } Stats;

static uint32_t rng = 0x5eed1234u;
static float clamp1(float x) { return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x); }
static float noise(void) { rng = rng * 1664525u + 1013904223u; return ((float)((rng >> 8) & 0xFFFFFFu) / 8388608.0f) - 1.0f; }
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

static int write_wav_float32(const char* path, const float* l, const float* r, uint32_t frames, uint32_t sampleRate) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    const uint32_t data = frames * 2U * (uint32_t)sizeof(float);
    fwrite("RIFF", 1, 4, f); le32(f, 36U + data); fwrite("WAVEfmt ", 1, 8, f);
    le32(f, 16U); le16(f, 3U); le16(f, 2U); le32(f, sampleRate); le32(f, sampleRate * 8U); le16(f, 8U); le16(f, 32U);
    fwrite("data", 1, 4, f); le32(f, data);
    for (uint32_t i = 0; i < frames; ++i) {
        fwrite(&l[i], sizeof(float), 1, f);
        fwrite(&r[i], sizeof(float), 1, f);
    }
    return fclose(f);
}

static int create_output_dir(const char* out_dir) {
#if defined(_WIN32)
    const int rc = _mkdir(out_dir);
#else
    const int rc = mkdir(out_dir, 0777);
#endif
    if (rc == 0 || errno == EEXIST) {
        return 0;
    }
    fprintf(stderr, "failed to create output directory %s: %s\n", out_dir, strerror(errno));
    return -1;
}

static void render_mode(int mode, const char* out_dir) {
    const uint32_t sr = SR_DEFAULT;
    const uint32_t frames = sr * SECONDS_SMOKE;
    float* left = (float*)calloc(frames, sizeof(float));
    float* right = (float*)calloc(frames, sizeof(float));
    if (!left || !right) { free(left); free(right); fprintf(stderr, "allocation failed\n"); exit(2); }
    DimensionDSP d; Dimension_Init(&d, (float)sr); Dimension_SetMode(&d, (DimensionMode)mode);
    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    Stats st = {0.0f, 0.0, 0.0};
    for (uint32_t pos = 0; pos < frames; pos += BLOCK) {
        const uint32_t n = (frames - pos > BLOCK) ? BLOCK : frames - pos;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t t = pos + i;
            float x = 0.16f * sinf(2.0f * PI_F * 220.0f * (float)t / (float)sr);
            if (t == sr / 2U) x += 0.55f;
            x += 0.025f * noise();
            inL[i] = x; inR[i] = x;
        }
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, n);
        for (uint32_t i = 0; i < n; ++i) {
            left[pos + i] = outL[i]; right[pos + i] = outR[i];
            const float m = 0.5f * (outL[i] + outR[i]); const float a = fabsf(m);
            if (a > st.peak) { st.peak = a; }
            st.sum += (double)m;
            st.energy += (double)m * (double)m;
        }
    }
    char path[256]; snprintf(path, sizeof(path), "%s/dimension5_mode%d.wav", out_dir, mode + 1);
    if (write_wav(path, left, right, frames, sr) != 0) { fprintf(stderr, "failed to write %s\n", path); exit(3); }
    printf("%s peak=%.4f dc=%.6f rms=%.4f\n", path, (double)st.peak, st.sum / (double)frames, sqrt(st.energy / (double)frames));
    free(left); free(right);
}

typedef enum SignalKind {
    SIG_KIND_SYNTH_PLUCK = 0,
    SIG_KIND_MULTITONE,
    SIG_KIND_IMPULSES
} SignalKind;

static void render_bbd_character_variation(
    float bbdChar,
    int wetOnly,
    SignalKind kind,
    const char* filename,
    const char* out_dir) {
    const uint32_t sr = SR_DEFAULT;
    const uint32_t frames = sr * SECONDS_SMOKE;
    float* left = (float*)calloc(frames, sizeof(float));
    float* right = (float*)calloc(frames, sizeof(float));
    if (!left || !right) { free(left); free(right); fprintf(stderr, "allocation failed\n"); exit(2); }

    DimensionDSP d;
    Dimension_Init(&d, (float)sr);
    Dimension_SetMode(&d, DIMENSION_MODE_II);
    DimensionParams p = d.params;
    p.bbdCharacter = bbdChar;
    if (wetOnly) {
        p.mode = DIMENSION_MODE_CUSTOM;
        p.dryGain = 0.0f;
    }
    Dimension_SetParams(&d, &p);

    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    Stats st = {0.0f, 0.0, 0.0};
    uint32_t localRng = 0x12345678u;

    for (uint32_t pos = 0; pos < frames; pos += BLOCK) {
        const uint32_t n = (frames - pos > BLOCK) ? BLOCK : frames - pos;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t t = pos + i;
            float xL = 0.0f, xR = 0.0f;

            switch (kind) {
                case SIG_KIND_SYNTH_PLUCK: {
                    const float f0 = 220.0f;
                    const float tSec = (float)t / (float)sr;
                    const float env = expf(-fmodf(tSec, 1.0f) * 3.5f);
                    const float body = 0.22f * sinf(2.0f * PI_F * f0 * tSec) +
                                       0.12f * sinf(2.0f * PI_F * 2.0f * f0 * tSec) +
                                       0.06f * sinf(2.0f * PI_F * 3.0f * f0 * tSec) +
                                       0.03f * sinf(2.0f * PI_F * 5.0f * f0 * tSec);
                    localRng = localRng * 1664525u + 1013904223u;
                    const float nz = 0.015f * (((float)((localRng >> 8) & 0xFFFFFFu) / 8388608.0f) - 1.0f);
                    xL = (body + nz) * env;
                    xR = xL;
                    break;
                }
                case SIG_KIND_MULTITONE: {
                    const float tSec = (float)t / (float)sr;
                    const float m = 0.12f * sinf(2.0f * PI_F * 300.0f * tSec) +
                                    0.10f * sinf(2.0f * PI_F * 1200.0f * tSec) +
                                    0.06f * sinf(2.0f * PI_F * 4500.0f * tSec);
                    xL = m;
                    xR = m;
                    break;
                }
                case SIG_KIND_IMPULSES: {
                    xL = (t % 2400 == 0) ? 0.75f : 0.0f;
                    xR = (t % 2400 == 1200) ? 0.75f : 0.0f;
                    break;
                }
            }

            inL[i] = xL;
            inR[i] = xR;
        }
        Dimension_ProcessBlock(&d, inL, inR, outL, outR, n);
        for (uint32_t i = 0; i < n; ++i) {
            left[pos + i] = outL[i];
            right[pos + i] = outR[i];
            const float m = 0.5f * (outL[i] + outR[i]);
            const float a = fabsf(m);
            if (a > st.peak) st.peak = a;
            st.sum += (double)m;
            st.energy += (double)m * (double)m;
        }
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/%s", out_dir, filename);
    if (write_wav(path, left, right, frames, sr) != 0) {
        fprintf(stderr, "failed to write %s\n", path);
        exit(3);
    }
    char pathF32[256];
    char baseName[128];
    memset(baseName, 0, sizeof(baseName));
    strncpy(baseName, filename, sizeof(baseName) - 1);
    char* dot = strrchr(baseName, '.');
    if (dot) *dot = '\0';
    snprintf(pathF32, sizeof(pathF32), "%s/%s_f32.wav", out_dir, baseName);
    write_wav_float32(pathF32, left, right, frames, sr);

    printf("%s peak=%.4f dc=%.6f rms=%.4f\n", path, (double)st.peak, st.sum / (double)frames, sqrt(st.energy / (double)frames));
    free(left);
    free(right);
}

static void render_e153da5_baseline(const char* out_dir) {
    const uint32_t sr = SR_DEFAULT;
    const uint32_t frames = sr * SECONDS_SMOKE;
    float* left = (float*)calloc(frames, sizeof(float));
    float* right = (float*)calloc(frames, sizeof(float));
    if (!left || !right) { free(left); free(right); fprintf(stderr, "allocation failed\n"); exit(2); }

    DimensionDSP_e153da5 d;
    Dimension_Init_e153da5(&d, (float)sr);
    Dimension_SetMode_e153da5(&d, DIMENSION_MODE_II_e153da5);

    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    Stats st = {0.0f, 0.0, 0.0};
    uint32_t localRng = 0x12345678u;

    for (uint32_t pos = 0; pos < frames; pos += BLOCK) {
        const uint32_t n = (frames - pos > BLOCK) ? BLOCK : frames - pos;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t t = pos + i;
            const float f0 = 220.0f;
            const float tSec = (float)t / (float)sr;
            const float env = expf(-fmodf(tSec, 1.0f) * 3.5f);
            const float body = 0.22f * sinf(2.0f * PI_F * f0 * tSec) +
                               0.12f * sinf(2.0f * PI_F * 2.0f * f0 * tSec) +
                               0.06f * sinf(2.0f * PI_F * 3.0f * f0 * tSec) +
                               0.03f * sinf(2.0f * PI_F * 5.0f * f0 * tSec);
            localRng = localRng * 1664525u + 1013904223u;
            const float nz = 0.015f * (((float)((localRng >> 8) & 0xFFFFFFu) / 8388608.0f) - 1.0f);
            inL[i] = (body + nz) * env;
            inR[i] = inL[i];
        }
        Dimension_ProcessBlock_e153da5(&d, inL, inR, outL, outR, n);
        for (uint32_t i = 0; i < n; ++i) {
            left[pos + i] = outL[i];
            right[pos + i] = outR[i];
            const float m = 0.5f * (outL[i] + outR[i]);
            const float a = fabsf(m);
            if (a > st.peak) st.peak = a;
            st.sum += (double)m;
            st.energy += (double)m * (double)m;
        }
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/spatial_engine_2_0_baseline.wav", out_dir);
    write_wav(path, left, right, frames, sr);
    char pathF32[256];
    snprintf(pathF32, sizeof(pathF32), "%s/spatial_engine_2_0_baseline_f32.wav", out_dir);
    write_wav_float32(pathF32, left, right, frames, sr);

    printf("%s peak=%.4f dc=%.6f rms=%.4f\n", path, (double)st.peak, st.sum / (double)frames, sqrt(st.energy / (double)frames));
    free(left);
    free(right);
}

/**
 * Renders long (16s) high-resolution spectral fixtures for sideband and aliasing analysis.
 */
static void render_spectral_fixture(
    float freqHz,
    float bbdChar,
    uint32_t sampleRate,
    uint32_t seconds,
    int useBaseline,
    const char* filename,
    const char* out_dir) {
    const uint32_t frames = sampleRate * seconds;
    float* left = (float*)calloc(frames, sizeof(float));
    float* right = (float*)calloc(frames, sizeof(float));
    if (!left || !right) {
        free(left); free(right);
        fprintf(stderr, "allocation failed for spectral fixture\n");
        exit(2);
    }

    DimensionDSP d;
    DimensionDSP_e153da5 dBase;
    if (useBaseline) {
        Dimension_Init_e153da5(&dBase, (float)sampleRate);
        Dimension_SetMode_e153da5(&dBase, DIMENSION_MODE_II_e153da5);
    } else {
        Dimension_Init(&d, (float)sampleRate);
        Dimension_SetMode(&d, DIMENSION_MODE_II);
        DimensionParams p = d.params;
        p.bbdCharacter = bbdChar;
        Dimension_SetParams(&d, &p);
    }

    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    Stats st = {0.0f, 0.0, 0.0};

    for (uint32_t pos = 0; pos < frames; pos += BLOCK) {
        const uint32_t n = (frames - pos > BLOCK) ? BLOCK : frames - pos;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t t = pos + i;
            const float tSec = (float)t / (float)sampleRate;
            float s = 0.0f;
            if (freqHz > 0.0f) {
                /* Pure sinusoid */
                s = 0.25f * sinf(2.0f * PI_F * freqHz * tSec);
            } else {
                /* Multitone fixture */
                s = 0.12f * sinf(2.0f * PI_F * 1000.0f * tSec) +
                    0.08f * sinf(2.0f * PI_F * 4500.0f * tSec) +
                    0.05f * sinf(2.0f * PI_F * 12000.0f * tSec);
            }
            inL[i] = s;
            inR[i] = s;
        }

        if (useBaseline) {
            Dimension_ProcessBlock_e153da5(&dBase, inL, inR, outL, outR, n);
        } else {
            Dimension_ProcessBlock(&d, inL, inR, outL, outR, n);
        }

        for (uint32_t i = 0; i < n; ++i) {
            left[pos + i] = outL[i];
            right[pos + i] = outR[i];
            const float m = 0.5f * (outL[i] + outR[i]);
            const float a = fabsf(m);
            if (a > st.peak) st.peak = a;
            st.sum += (double)m;
            st.energy += (double)m * (double)m;
        }
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/%s", out_dir, filename);
    write_wav(path, left, right, frames, sampleRate);

    char pathF32[256];
    char baseName[128];
    memset(baseName, 0, sizeof(baseName));
    strncpy(baseName, filename, sizeof(baseName) - 1);
    char* dot = strrchr(baseName, '.');
    if (dot) *dot = '\0';
    snprintf(pathF32, sizeof(pathF32), "%s/%s_f32.wav", out_dir, baseName);
    write_wav_float32(pathF32, left, right, frames, sampleRate);

    printf("%s (sr=%u) peak=%.4f dc=%.6f rms=%.4f\n", path, sampleRate, (double)st.peak, st.sum / (double)frames, sqrt(st.energy / (double)frames));
    free(left);
    free(right);
}

static void render_all_spectral_fixtures(const char* out_dir) {
    printf("\n=== RENDERING LONG (16-SECOND) HIGH-RESOLUTION SPECTRAL FIXTURES ===\n");
    const uint32_t sec = SECONDS_SPECTRAL;

    /* 48 kHz Carrier Sweep: 1k, 5k, 10k, 15k, 18k, Multitone */
    const float freqs[5] = { 1000.0f, 5000.0f, 10000.0f, 15000.0f, 18000.0f };
    const char* freqNames[5] = { "1k", "5k", "10k", "15k", "18k" };

    for (int f = 0; f < 5; ++f) {
        char fn0[128], fn50[128], fn100[128], fnBase[128];
        snprintf(fn0, sizeof(fn0), "spec_%s_48k_char000.wav", freqNames[f]);
        snprintf(fn50, sizeof(fn50), "spec_%s_48k_char050.wav", freqNames[f]);
        snprintf(fn100, sizeof(fn100), "spec_%s_48k_char100.wav", freqNames[f]);
        snprintf(fnBase, sizeof(fnBase), "spec_%s_48k_e153da5.wav", freqNames[f]);

        render_spectral_fixture(freqs[f], 0.00f, 48000, sec, 0, fn0, out_dir);
        render_spectral_fixture(freqs[f], 0.50f, 48000, sec, 0, fn50, out_dir);
        render_spectral_fixture(freqs[f], 1.00f, 48000, sec, 0, fn100, out_dir);
        render_spectral_fixture(freqs[f], 0.00f, 48000, sec, 1, fnBase, out_dir);
    }

    /* Multitone at 48 kHz */
    render_spectral_fixture(-1.0f, 0.00f, 48000, sec, 0, "spec_multitone_48k_char000.wav", out_dir);
    render_spectral_fixture(-1.0f, 0.50f, 48000, sec, 0, "spec_multitone_48k_char050.wav", out_dir);
    render_spectral_fixture(-1.0f, 1.00f, 48000, sec, 0, "spec_multitone_48k_char100.wav", out_dir);

    /* Multi-rate Sweep: 10k and 15k at 44.1 kHz and 96 kHz */
    render_spectral_fixture(10000.0f, 1.00f, 44100, sec, 0, "spec_10k_44k_char100.wav", out_dir);
    render_spectral_fixture(10000.0f, 0.00f, 44100, sec, 0, "spec_10k_44k_char000.wav", out_dir);
    render_spectral_fixture(10000.0f, 1.00f, 96000, sec, 0, "spec_10k_96k_char100.wav", out_dir);
    render_spectral_fixture(10000.0f, 0.00f, 96000, sec, 0, "spec_10k_96k_char000.wav", out_dir);

    render_spectral_fixture(15000.0f, 1.00f, 44100, sec, 0, "spec_15k_44k_char100.wav", out_dir);
    render_spectral_fixture(15000.0f, 0.00f, 44100, sec, 0, "spec_15k_44k_char000.wav", out_dir);
    render_spectral_fixture(15000.0f, 1.00f, 96000, sec, 0, "spec_15k_96k_char100.wav", out_dir);
    render_spectral_fixture(15000.0f, 0.00f, 96000, sec, 0, "spec_15k_96k_char000.wav", out_dir);
}

int main(int argc, char** argv) {
    const char* out_dir = (argc > 1 && argv[1][0] != '-') ? argv[1] : "out";
    int render_spectral = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--spectral") == 0 || strcmp(argv[i], "--all") == 0) {
            render_spectral = 1;
        }
    }

    if (create_output_dir(out_dir) != 0) {
        return 1;
    }

    printf("=== RENDERING LEGACY MODES (I..IV) ===\n");
    for (int mode = 0; mode < 4; ++mode) render_mode(mode, out_dir);

    printf("\n=== RENDERING FROZEN BASELINE (e153da5 Spatial Engine 2.0) ===\n");
    render_e153da5_baseline(out_dir);

    printf("\n=== RENDERING BBD CHARACTER ENGINE 2.1 SUITE ===\n");
    render_bbd_character_variation(0.00f, 0, SIG_KIND_SYNTH_PLUCK, "bbd_character_000.wav", out_dir);
    render_bbd_character_variation(0.25f, 0, SIG_KIND_SYNTH_PLUCK, "bbd_character_025.wav", out_dir);
    render_bbd_character_variation(0.50f, 0, SIG_KIND_SYNTH_PLUCK, "bbd_character_050.wav", out_dir);
    render_bbd_character_variation(0.75f, 0, SIG_KIND_SYNTH_PLUCK, "bbd_character_075.wav", out_dir);
    render_bbd_character_variation(1.00f, 0, SIG_KIND_SYNTH_PLUCK, "bbd_character_100.wav", out_dir);

    render_bbd_character_variation(0.00f, 1, SIG_KIND_SYNTH_PLUCK, "bbd_character_000_wet.wav", out_dir);
    render_bbd_character_variation(0.50f, 1, SIG_KIND_SYNTH_PLUCK, "bbd_character_050_wet.wav", out_dir);
    render_bbd_character_variation(1.00f, 1, SIG_KIND_SYNTH_PLUCK, "bbd_character_100_wet.wav", out_dir);

    render_bbd_character_variation(0.00f, 0, SIG_KIND_MULTITONE,   "bbd_multitone_000.wav", out_dir);
    render_bbd_character_variation(1.00f, 0, SIG_KIND_MULTITONE,   "bbd_multitone_100.wav", out_dir);

    render_bbd_character_variation(0.00f, 0, SIG_KIND_IMPULSES,    "bbd_impulses_000.wav", out_dir);
    render_bbd_character_variation(1.00f, 0, SIG_KIND_IMPULSES,    "bbd_impulses_100.wav", out_dir);

    if (render_spectral) {
        render_all_spectral_fixtures(out_dir);
    }

    return 0;
}
