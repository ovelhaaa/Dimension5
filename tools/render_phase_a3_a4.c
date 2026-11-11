#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "dimension_dsp.h"

/* Declarations for Phase A.3 implementation (compiled in tools/dimension_dsp_phase_a3.c) */
void Dimension_Init_A3(DimensionDSP* d, float sampleRate);
void Dimension_Reset_A3(DimensionDSP* d);
void Dimension_SetMode_A3(DimensionDSP* d, DimensionMode mode);
void Dimension_SetParams_A3(DimensionDSP* d, const DimensionParams* p);
void Dimension_GetParams_A3(const DimensionDSP* d, DimensionParams* p);
void Dimension_ProcessBlock_A3(DimensionDSP* d, const float* inL, const float* inR, float* outL, float* outR, uint32_t n);

/* Audio file writer (32-bit float stereo WAV) */
static void write_wav_f32(const char* path, const float* l, const float* r, uint32_t n, uint32_t sr) {
    FILE* f = fopen(path, "wb");
    if (!f) return;

    uint32_t dataSize = n * 2U * sizeof(float);
    uint32_t fileSize = 36U + dataSize;
    uint16_t numChannels = 2U;
    uint16_t bitsPerSample = 32U;
    uint16_t audioFormat = 3U; /* IEEE Float */
    uint32_t byteRate = sr * 8U;
    uint16_t blockAlign = 8U;

    fwrite("RIFF", 1, 4, f);
    fwrite(&fileSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t fmtSize = 16U;
    fwrite(&fmtSize, 4, 1, f);
    fwrite(&audioFormat, 2, 1, f);
    fwrite(&numChannels, 2, 1, f);
    fwrite(&sr, 4, 1, f);
    fwrite(&byteRate, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);
    fwrite(&bitsPerSample, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);

    for (uint32_t i = 0; i < n; ++i) {
        fwrite(&l[i], sizeof(float), 1, f);
        fwrite(&r[i], sizeof(float), 1, f);
    }
    fclose(f);
}

/* Run rapid Custom automation on test audio */
static void render_scenario(
    bool isPhaseA3,
    float charAmount,
    const char* outPath,
    uint32_t sr,
    uint32_t totalSamples,
    uint32_t bs
) {
    DimensionDSP d;
    if (isPhaseA3) {
        Dimension_Init_A3(&d, (float)sr);
        Dimension_SetMode_A3(&d, DIMENSION_MODE_CUSTOM);
    } else {
        Dimension_Init(&d, (float)sr);
        Dimension_SetMode(&d, DIMENSION_MODE_CUSTOM);
    }

    DimensionParams p;
    if (isPhaseA3) {
        Dimension_GetParams_A3(&d, &p);
    } else {
        Dimension_GetParams(&d, &p);
    }

    p.mode = DIMENSION_MODE_CUSTOM;
    p.baseDelayMs = 7.0f;
    p.depthMs = 0.9f;
    p.rateHz = 0.25f;
    p.bbdCharacter = charAmount;

    if (isPhaseA3) {
        Dimension_SetParams_A3(&d, &p);
        Dimension_Reset_A3(&d);
    } else {
        Dimension_SetParams(&d, &p);
        Dimension_Reset(&d);
    }

    float* inL = (float*)malloc(totalSamples * sizeof(float));
    float* inR = (float*)malloc(totalSamples * sizeof(float));
    float* outL = (float*)malloc(totalSamples * sizeof(float));
    float* outR = (float*)malloc(totalSamples * sizeof(float));

    if (!inL || !inR || !outL || !outR) {
        if (inL) free(inL);
        if (inR) free(inR);
        if (outL) free(outL);
        if (outR) free(outR);
        return;
    }

    /* Test signal: Synth string pad mimic (440, 880, 1760, 3520 Hz) */
    const float twoPi = 6.28318530718f;
    for (uint32_t i = 0; i < totalSamples; ++i) {
        float t = (float)i / (float)sr;
        float s = 0.35f * sinf(twoPi * 440.0f * t) +
                  0.20f * sinf(twoPi * 880.0f * t) +
                  0.15f * sinf(twoPi * 1760.0f * t) +
                  0.10f * sinf(twoPi * 3520.0f * t);
        inL[i] = s;
        inR[i] = s;
    }

    uint32_t pos = 0;
    while (pos < totalSamples) {
        /* Automation steps at 1s, 2s, 3s */
        if (pos == sr * 1U) {
            /* Event 1: Base Delay 7.0 -> 12.0 ms */
            p.baseDelayMs = 12.0f;
            if (isPhaseA3) Dimension_SetParams_A3(&d, &p);
            else Dimension_SetParams(&d, &p);
        } else if (pos == sr * 2U) {
            /* Event 2: Depth 0.9 -> 2.4 ms */
            p.depthMs = 2.4f;
            if (isPhaseA3) Dimension_SetParams_A3(&d, &p);
            else Dimension_SetParams(&d, &p);
        } else if (pos == sr * 3U) {
            /* Event 3: Base Delay 12.0 -> 7.0 ms, Depth 2.4 -> 1.4 ms */
            p.baseDelayMs = 7.0f;
            p.depthMs = 1.4f;
            if (isPhaseA3) Dimension_SetParams_A3(&d, &p);
            else Dimension_SetParams(&d, &p);
        }

        uint32_t n = totalSamples - pos;
        if (n > bs) n = bs;

        if (isPhaseA3) {
            Dimension_ProcessBlock_A3(&d, &inL[pos], &inR[pos], &outL[pos], &outR[pos], n);
        } else {
            Dimension_ProcessBlock(&d, &inL[pos], &inR[pos], &outL[pos], &outR[pos], n);
        }
        pos += n;
    }

    write_wav_f32(outPath, outL, outR, totalSamples, sr);
    free(inL);
    free(inR);
    free(outL);
    free(outR);
}

int main(int argc, char** argv) {
    const char* outDir = (argc > 1) ? argv[1] : "out";
    char pA3_c50[512], pA4_c50[512], pA3_c100[512], pA4_c100[512];
    snprintf(pA3_c50, sizeof(pA3_c50), "%s/ab_phase_a3_char050.wav", outDir);
    snprintf(pA4_c50, sizeof(pA4_c50), "%s/ab_phase_a4_char050.wav", outDir);
    snprintf(pA3_c100, sizeof(pA3_c100), "%s/ab_phase_a3_char100.wav", outDir);
    snprintf(pA4_c100, sizeof(pA4_c100), "%s/ab_phase_a4_char100.wav", outDir);

    const uint32_t sr = 48000;
    const uint32_t durationSec = 5;
    const uint32_t totalSamples = sr * durationSec;
    const uint32_t bs = 64;

    printf("Rendering Phase A.4 audio (tau=30ms prediction)...\n");
    render_scenario(false, 0.50f, pA4_c50, sr, totalSamples, bs);
    render_scenario(false, 1.00f, pA4_c100, sr, totalSamples, bs);

    printf("Rendering Phase A.3 audio (tau=20ms prediction)...\n");
    render_scenario(true, 0.50f, pA3_c50, sr, totalSamples, bs);
    render_scenario(true, 1.00f, pA3_c100, sr, totalSamples, bs);

    printf("Rendered A/B WAV fixtures successfully:\n");
    printf("  %s\n  %s\n  %s\n  %s\n", pA3_c50, pA4_c50, pA3_c100, pA4_c100);
    return 0;
}
