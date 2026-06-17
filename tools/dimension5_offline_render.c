#include "dimension_dsp.h"

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

#define SR 48000U
#define BLOCK DIMENSION_MAX_BLOCK_SIZE
#define SECONDS 4U
#define PI_F 3.14159265358979323846f

typedef struct Stats { float peak; double sum; double energy; } Stats;

static uint32_t rng = 0x5eed1234u;
static float clamp1(float x) { return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x); }
static float noise(void) { rng = rng * 1664525u + 1013904223u; return ((float)((rng >> 8) & 0xFFFFFFu) / 8388608.0f) - 1.0f; }
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
    const uint32_t frames = SR * SECONDS;
    float* left = (float*)calloc(frames, sizeof(float));
    float* right = (float*)calloc(frames, sizeof(float));
    if (!left || !right) { free(left); free(right); fprintf(stderr, "allocation failed\n"); exit(2); }
    DimensionDSP d; Dimension_Init(&d, (float)SR); Dimension_SetMode(&d, (DimensionMode)mode);
    float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
    Stats st = {0.0f, 0.0, 0.0};
    for (uint32_t pos = 0; pos < frames; pos += BLOCK) {
        const uint32_t n = (frames - pos > BLOCK) ? BLOCK : frames - pos;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t t = pos + i;
            float x = 0.16f * sinf(2.0f * PI_F * 220.0f * (float)t / (float)SR);
            if (t == SR / 2U) x += 0.55f;
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
    if (write_wav(path, left, right, frames) != 0) { fprintf(stderr, "failed to write %s\n", path); exit(3); }
    printf("%s peak=%.4f dc=%.6f rms=%.4f\n", path, (double)st.peak, st.sum / (double)frames, sqrt(st.energy / (double)frames));
    free(left); free(right);
}

int main(int argc, char** argv) {
    const char* out_dir = (argc > 1) ? argv[1] : "out";
    if (create_output_dir(out_dir) != 0) {
        return 1;
    }
    for (int mode = 0; mode < 4; ++mode) render_mode(mode, out_dir);
    return 0;
}
