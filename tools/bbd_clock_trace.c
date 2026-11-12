#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "dimension_dsp.h"

#define TEST_PI 3.14159265358979323846f

/* Structure to hold current trace sample */
typedef struct TraceSample {
    uint32_t frame;
    float sampleRate;
    uint32_t blockSize;
    int mode;
    float smoothBaseDelayMs;
    float smoothDepthMs;
    float lfoPhase;
    float effDelayLMs;
    float effDelayRMs;
    float clockLHz;
    float clockRHz;
    float bbdCharacter;
} TraceSample;

static FILE* g_traceFile = NULL;
static uint32_t g_currentFrame = 0U;
static uint32_t g_currentBlockSize = 64U;
static uint32_t g_currentRunId = 0U;
static bool g_traceActive = false;

static void clock_trace_hook_impl(
    const DimensionDSP* d,
    uint32_t sampleIdx,
    float clockL,
    float clockR,
    float lfo,
    float curBbdF0
) {
    (void)curBbdF0;
    (void)sampleIdx;
    if (!g_traceActive || g_traceFile == NULL) {
        return;
    }

    const float effDelayL = d->smoothBaseDelayMs + d->smoothDepthMs * lfo;
    const float effDelayR = d->smoothBaseDelayMs - d->smoothDepthMs * lfo;

    fprintf(g_traceFile, "%u,%.1f,%u,%d,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.2f,%.2f,%.4f\n",
        g_currentFrame,
        (double)d->params.sampleRate,
        g_currentBlockSize,
        (int)d->params.mode,
        g_currentRunId,
        (double)d->smoothBaseDelayMs,
        (double)d->smoothDepthMs,
        (double)d->lfoPhase,
        (double)effDelayL,
        (double)effDelayR,
        (double)clockL,
        (double)clockR,
        (double)d->smoothBbdCharacter
    );
    g_currentFrame++;
}

#define DIMENSION_CLOCK_TRACE_HOOK(d, i, clkL, clkR, lfo, curF0) \
    clock_trace_hook_impl((d), (i), (clkL), (clkR), (lfo), (curF0))

/* Include DSP source directly with the hook active */
#include "../src/dimension_dsp.c"

/* Automation helper for invariance tests */
static inline void get_custom_automation_params(DimensionParams* p, uint32_t frameIdx) {
    p->mode = DIMENSION_MODE_CUSTOM;
    if (frameIdx >= 4096) {
        p->bbdCharacter = 0.25f;
        p->baseDelayMs = 7.0f;
        p->depthMs = 0.9f;
        p->rateHz = 0.25f;
    } else if (frameIdx >= 3072) {
        p->rateHz = 0.50f;
    } else if (frameIdx >= 2048) {
        p->depthMs = 2.4f;
    } else if (frameIdx >= 1024) {
        p->bbdCharacter = 1.00f;
        p->baseDelayMs = 11.5f;
    } else if (frameIdx >= 512) {
        p->bbdCharacter = 0.50f;
        p->baseDelayMs = 8.5f;
        p->depthMs = 1.4f;
    } else {
        p->bbdCharacter = 0.0f;
        p->baseDelayMs = 7.0f;
        p->depthMs = 0.9f;
        p->rateHz = 0.25f;
    }
}

static inline uint32_t get_next_event_frame(uint32_t pos) {
    const uint32_t events[7] = { 0, 512, 1024, 2048, 3072, 4096, 5120 };
    for (int i = 0; i < 7; ++i) {
        if (events[i] > pos) return events[i];
    }
    return 5120;
}

int main(int argc, char** argv) {
    const char* outDir = (argc > 1) ? argv[1] : "out";
    char traceCsvPath[512];
    char invarJsonPath[512];
    snprintf(traceCsvPath, sizeof(traceCsvPath), "%s/clock_trace.csv", outDir);
    snprintf(invarJsonPath, sizeof(invarJsonPath), "%s/block_invariance_c99.json", outDir);

    printf("======================================================================\n");
    printf("DIMENSION5 — C99 BBD CLOCK TRACE & BLOCK INVARIANCE HARNESS\n");
    printf("======================================================================\n");
    printf("Exporting Clock Trace to: %s\n", traceCsvPath);
    printf("Exporting Invariance to:  %s\n", invarJsonPath);

    /* -------------------------------------------------------------------------
     * 1. Export Structured Per-Sample Clock Trace
     * ------------------------------------------------------------------------- */
    g_traceFile = fopen(traceCsvPath, "w");
    if (!g_traceFile) {
        fprintf(stderr, "Error: cannot open trace CSV for writing: %s\n", traceCsvPath);
        return 1;
    }

    fprintf(g_traceFile, "frame,sample_rate,block_size,mode,run_id,base_delay_smoothed_ms,depth_smoothed_ms,lfo_phase,effective_delay_l_ms,effective_delay_r_ms,clock_l_hz,clock_r_hz,bbd_character\n");

    const float sampleRates[3] = { 44100.0f, 48000.0f, 96000.0f };
    const uint32_t blockSizes[5] = { 1, 7, 16, 32, 64 };
    const DimensionMode modes[5] = { DIMENSION_MODE_I, DIMENSION_MODE_II, DIMENSION_MODE_III, DIMENSION_MODE_IV, DIMENSION_MODE_CUSTOM };
    const float charAmounts[3] = { 0.0f, 0.5f, 1.0f };

    g_currentFrame = 0U;
    g_currentRunId = 0U;
    g_traceActive = true;

    /* Trace Section A: Steady-state across Modes I..IV and Custom at 48 kHz, bs=64 */
    printf("\n[1/3] Generating Steady-State Clock Traces across Modes & Rates...\n");
    for (int m = 0; m < 5; ++m) {
        for (int c = 0; c < 3; ++c) {
            g_currentRunId++;
            DimensionDSP d;
            Dimension_Init(&d, 48000.0f);
            Dimension_SetMode(&d, modes[m]);
            DimensionParams p;
            Dimension_GetParams(&d, &p);
            p.bbdCharacter = charAmounts[c];
            if (modes[m] == DIMENSION_MODE_CUSTOM) {
                p.baseDelayMs = 10.0f;
                p.depthMs = 1.8f;
                p.rateHz = 0.50f;
            }
            Dimension_SetParams(&d, &p);
            Dimension_Reset(&d);

            g_currentBlockSize = 64U;
            float inL[64], inR[64], outL[64], outR[64];
            for (uint32_t i = 0; i < 64; ++i) inL[i] = inR[i] = 0.2f;

            /* Warm up 40 blocks */
            g_traceActive = false;
            for (uint32_t b = 0; b < 40; ++b) {
                Dimension_ProcessBlock(&d, inL, inR, outL, outR, 64);
            }
            /* Record 40 blocks */
            g_traceActive = true;
            for (uint32_t b = 0; b < 40; ++b) {
                Dimension_ProcessBlock(&d, inL, inR, outL, outR, 64);
            }
        }
    }

    /* Trace Section B: Rapid Automation in Custom Mode across 44.1, 48, 96 kHz and Block Sizes */
    printf("[2/3] Generating Rapid Automation Clock Traces (Custom Delay, Depth, Rate)...\n");
    for (int s = 0; s < 3; ++s) {
        const float sr = sampleRates[s];
        for (int b = 0; b < 5; ++b) {
            const uint32_t bs = blockSizes[b];
            g_currentBlockSize = bs;
            g_currentRunId++;

            DimensionDSP d;
            Dimension_Init(&d, sr);
            Dimension_SetMode(&d, DIMENSION_MODE_CUSTOM);
            DimensionParams p;
            Dimension_GetParams(&d, &p);
            p.mode = DIMENSION_MODE_CUSTOM;
            p.baseDelayMs = 7.0f;
            p.depthMs = 0.9f;
            p.rateHz = 0.25f;
            p.bbdCharacter = 1.0f;
            Dimension_SetParams(&d, &p);
            Dimension_Reset(&d);

            float inL[64], inR[64], outL[64], outR[64];
            for (uint32_t i = 0; i < bs; ++i) inL[i] = inR[i] = 0.15f;

            /* Steady-state warm up */
            g_traceActive = false;
            for (uint32_t k = 0; k < 30; ++k) {
                Dimension_ProcessBlock(&d, inL, inR, outL, outR, bs);
            }

            /* Record: Event 1 - Base Delay Step (7.0 -> 11.5 ms) */
            p.baseDelayMs = 11.5f;
            Dimension_SetParams(&d, &p);
            g_traceActive = true;
            for (uint32_t k = 0; k < 60; ++k) {
                Dimension_ProcessBlock(&d, inL, inR, outL, outR, bs);
            }

            /* Record: Event 2 - Depth Step (0.9 -> 2.4 ms) */
            p.depthMs = 2.4f;
            Dimension_SetParams(&d, &p);
            for (uint32_t k = 0; k < 60; ++k) {
                Dimension_ProcessBlock(&d, inL, inR, outL, outR, bs);
            }

            /* Record: Event 3 - Rate Step (0.25 -> 0.50 Hz) */
            p.rateHz = 0.50f;
            Dimension_SetParams(&d, &p);
            for (uint32_t k = 0; k < 60; ++k) {
                Dimension_ProcessBlock(&d, inL, inR, outL, outR, bs);
            }
        }
    }

    g_traceActive = false;
    fclose(g_traceFile);
    g_traceFile = NULL;
    printf("  --> Wrote %u samples to %s\n", g_currentFrame, traceCsvPath);

    /* -------------------------------------------------------------------------
     * 2. Authentic DSP Core & Host Block Invariance Measurements
     * ------------------------------------------------------------------------- */
    printf("\n[3/3] Executing Real Block Invariance Measurements in C99...\n");
    const uint32_t totalFrames = 5120U;
    const uint32_t coreBlockSizes[4] = { 7, 16, 32, 64 };
    const uint32_t hostSizes[5] = { 64, 128, 256, 512, 1024 };

    double worstCoreMaxDiff = 0.0;
    double worstCoreRmsDiff = 0.0;
    double worstCoreNrmse = 0.0;
    uint32_t totalCoreDivergent = 0U;

    double worstHostMaxDiff = 0.0;
    double worstHostRmsDiff = 0.0;
    double worstHostNrmse = 0.0;
    uint32_t totalHostDivergent = 0U;

    for (int r = 0; r < 3; ++r) {
        const float sr = sampleRates[r];
        float* inStreamL = (float*)malloc(totalFrames * sizeof(float));
        float* inStreamR = (float*)malloc(totalFrames * sizeof(float));
        float* refOutL = (float*)malloc(totalFrames * sizeof(float));
        float* refOutR = (float*)malloc(totalFrames * sizeof(float));

        if (!inStreamL || !inStreamR || !refOutL || !refOutR) {
            fprintf(stderr, "Allocation failure\n");
            return 1;
        }

        for (uint32_t i = 0; i < totalFrames; ++i) {
            const float tSec = (float)i / sr;
            float s = 0.20f * sinf(2.0f * TEST_PI * 330.0f * tSec) + 0.10f * sinf(2.0f * TEST_PI * 880.0f * tSec);
            if (i % 512 == 0) s += 0.4f;
            inStreamL[i] = s;
            inStreamR[i] = s;
        }

        /* 1. Generate 1-sample (sample-by-sample) reference */
        {
            DimensionDSP dspRef;
            Dimension_Init(&dspRef, sr);
            Dimension_SetMode(&dspRef, DIMENSION_MODE_CUSTOM);
            uint32_t pos = 0;
            while (pos < totalFrames) {
                DimensionParams p = dspRef.params;
                get_custom_automation_params(&p, pos);
                Dimension_SetParams(&dspRef, &p);
                Dimension_ProcessBlock(&dspRef, &inStreamL[pos], &inStreamR[pos], &refOutL[pos], &refOutR[pos], 1);
                pos++;
            }
        }

        double refEnergy = 0.0;
        for (uint32_t i = 0; i < totalFrames; ++i) {
            refEnergy += (double)(refOutL[i] * refOutL[i] + refOutR[i] * refOutR[i]);
        }
        const double refRms = sqrt(refEnergy / (double)(2 * totalFrames));

        /* 2. Test DSP Core block sizes (7, 16, 32, 64) against 1-sample reference */
        for (int b = 0; b < 4; ++b) {
            const uint32_t bs = coreBlockSizes[b];
            float* testOutL = (float*)malloc(totalFrames * sizeof(float));
            float* testOutR = (float*)malloc(totalFrames * sizeof(float));

            DimensionDSP dspTest;
            Dimension_Init(&dspTest, sr);
            Dimension_SetMode(&dspTest, DIMENSION_MODE_CUSTOM);

            uint32_t pos = 0;
            while (pos < totalFrames) {
                const uint32_t nextEvent = get_next_event_frame(pos);
                const uint32_t chunkMax = nextEvent - pos;
                const uint32_t n = (chunkMax < bs) ? chunkMax : bs;

                DimensionParams p = dspTest.params;
                get_custom_automation_params(&p, pos);
                Dimension_SetParams(&dspTest, &p);

                Dimension_ProcessBlock(&dspTest, &inStreamL[pos], &inStreamR[pos],
                                       &testOutL[pos], &testOutR[pos], n);
                pos += n;
            }

            double sumSqDiff = 0.0;
            double maxDiff = 0.0;
            uint32_t divergent = 0U;

            for (uint32_t i = 0; i < totalFrames; ++i) {
                const double diffL = fabs((double)(testOutL[i] - refOutL[i]));
                const double diffR = fabs((double)(testOutR[i] - refOutR[i]));
                if (diffL > maxDiff) maxDiff = diffL;
                if (diffR > maxDiff) maxDiff = diffR;
                if (diffL > 1e-4 || diffR > 1e-4) divergent++;
                sumSqDiff += diffL * diffL + diffR * diffR;
            }

            const double rmsDiff = sqrt(sumSqDiff / (double)(2 * totalFrames));
            const double nrmse = rmsDiff / refRms;

            if (maxDiff > worstCoreMaxDiff) worstCoreMaxDiff = maxDiff;
            if (rmsDiff > worstCoreRmsDiff) worstCoreRmsDiff = rmsDiff;
            if (nrmse > worstCoreNrmse) worstCoreNrmse = nrmse;
            totalCoreDivergent += divergent;

            free(testOutL);
            free(testOutR);
        }

        /* 3. Test Host segmentation (128, 256, 512, 1024) against 64-frame host reference */
        float* hostRefOutL = (float*)malloc(totalFrames * sizeof(float));
        float* hostRefOutR = (float*)malloc(totalFrames * sizeof(float));
        {
            DimensionDSP dspRef;
            Dimension_Init(&dspRef, sr);
            Dimension_SetMode(&dspRef, DIMENSION_MODE_CUSTOM);
            uint32_t pos = 0;
            while (pos < totalFrames) {
                const uint32_t nextEvent = get_next_event_frame(pos);
                const uint32_t hostChunk = (pos + 64 > totalFrames) ? (totalFrames - pos) : 64;
                const uint32_t validHost = (pos + hostChunk > nextEvent) ? (nextEvent - pos) : hostChunk;

                DimensionParams p = dspRef.params;
                get_custom_automation_params(&p, pos);
                Dimension_SetParams(&dspRef, &p);

                uint32_t offset = 0;
                while (offset < validHost) {
                    uint32_t n = validHost - offset;
                    if (n > 64) n = 64;
                    Dimension_ProcessBlock(&dspRef, &inStreamL[pos + offset], &inStreamR[pos + offset],
                                           &hostRefOutL[pos + offset], &hostRefOutR[pos + offset], n);
                    offset += n;
                }
                pos += validHost;
            }
        }

        for (int h = 1; h < 5; ++h) {
            const uint32_t hostBs = hostSizes[h];
            float* testOutL = (float*)malloc(totalFrames * sizeof(float));
            float* testOutR = (float*)malloc(totalFrames * sizeof(float));

            DimensionDSP dspTest;
            Dimension_Init(&dspTest, sr);
            Dimension_SetMode(&dspTest, DIMENSION_MODE_CUSTOM);

            uint32_t pos = 0;
            while (pos < totalFrames) {
                const uint32_t nextEvent = get_next_event_frame(pos);
                const uint32_t hostChunk = (pos + hostBs > totalFrames) ? (totalFrames - pos) : hostBs;
                const uint32_t validHost = (pos + hostChunk > nextEvent) ? (nextEvent - pos) : hostChunk;

                DimensionParams p = dspTest.params;
                get_custom_automation_params(&p, pos);
                Dimension_SetParams(&dspTest, &p);

                uint32_t offset = 0;
                while (offset < validHost) {
                    uint32_t n = validHost - offset;
                    if (n > 64) n = 64;
                    Dimension_ProcessBlock(&dspTest, &inStreamL[pos + offset], &inStreamR[pos + offset],
                                           &testOutL[pos + offset], &testOutR[pos + offset], n);
                    offset += n;
                }
                pos += validHost;
            }

            double sumSqDiff = 0.0;
            double maxDiff = 0.0;
            uint32_t divergent = 0U;

            for (uint32_t i = 0; i < totalFrames; ++i) {
                const double diffL = fabs((double)(testOutL[i] - hostRefOutL[i]));
                const double diffR = fabs((double)(testOutR[i] - hostRefOutR[i]));
                if (diffL > maxDiff) maxDiff = diffL;
                if (diffR > maxDiff) maxDiff = diffR;
                if (diffL > 0.0 || diffR > 0.0) divergent++;
                sumSqDiff += diffL * diffL + diffR * diffR;
            }

            const double rmsDiff = sqrt(sumSqDiff / (double)(2 * totalFrames));
            const double nrmse = rmsDiff / refRms;

            if (maxDiff > worstHostMaxDiff) worstHostMaxDiff = maxDiff;
            if (rmsDiff > worstHostRmsDiff) worstHostRmsDiff = rmsDiff;
            if (nrmse > worstHostNrmse) worstHostNrmse = nrmse;
            totalHostDivergent += divergent;

            free(testOutL);
            free(testOutR);
        }

        free(inStreamL);
        free(inStreamR);
        free(refOutL);
        free(refOutR);
        free(hostRefOutL);
        free(hostRefOutR);
    }

    printf("  DSP Core Max Diff:  %.3e (threshold < 0.005)\n", worstCoreMaxDiff);
    printf("  DSP Core RMS Diff:  %.3e\n", worstCoreRmsDiff);
    printf("  DSP Core NRMSE:     %.3e\n", worstCoreNrmse);
    printf("  Host Max Diff:      %.3e (bit-exact)\n", worstHostMaxDiff);
    printf("  Host RMS Diff:      %.3e\n", worstHostRmsDiff);

    /* Export real measured metrics to JSON */
    FILE* jf = fopen(invarJsonPath, "w");
    if (jf) {
        fprintf(jf, "{\n");
        fprintf(jf, "  \"metadata\": {\n");
        fprintf(jf, "    \"harness\": \"tools/bbd_clock_trace.c\",\n");
        fprintf(jf, "    \"source\": \"bbd_block_invariance_c99\",\n");
        fprintf(jf, "    \"description\": \"Real measured DSP core and host segmentation invariance under Custom automation\"\n");
        fprintf(jf, "  },\n");
        fprintf(jf, "  \"dsp_core_block_sizes\": [1, 7, 16, 32, 64],\n");
        fprintf(jf, "  \"host_buffer_sizes\": [64, 128, 256, 512, 1024],\n");
        fprintf(jf, "  \"sample_rates\": [44100, 48000, 96000],\n");
        fprintf(jf, "  \"measured_metrics\": {\n");
        fprintf(jf, "    \"dsp_core_max_abs_diff\": %.9e,\n", worstCoreMaxDiff);
        fprintf(jf, "    \"dsp_core_rms_diff\": %.9e,\n", worstCoreRmsDiff);
        fprintf(jf, "    \"dsp_core_nrmse\": %.9e,\n", worstCoreNrmse);
        fprintf(jf, "    \"dsp_core_divergent_samples\": %u,\n", totalCoreDivergent);
        fprintf(jf, "    \"host_segmentation_max_abs_diff\": %.9e,\n", worstHostMaxDiff);
        fprintf(jf, "    \"host_segmentation_rms_diff\": %.9e,\n", worstHostRmsDiff);
        fprintf(jf, "    \"host_segmentation_nrmse\": %.9e,\n", worstHostNrmse);
        fprintf(jf, "    \"host_segmentation_divergent_samples\": %u,\n", totalHostDivergent);
        fprintf(jf, "    \"is_bit_exact\": %s,\n", (worstHostMaxDiff == 0.0) ? "true" : "false");
        fprintf(jf, "    \"status\": \"%s\"\n", (worstCoreMaxDiff < 0.005 && worstHostMaxDiff == 0.0) ? "PASS" : "FAIL");
        fprintf(jf, "  }\n");
        fprintf(jf, "}\n");
        fclose(jf);
        printf("  --> Wrote invariance metrics to %s\n", invarJsonPath);
    }

    printf("\n======================================================================\n");
    printf("BBD CLOCK TRACE & INVARIANCE COMPLETED SUCCESSFULLY\n");
    printf("======================================================================\n");
    return 0;
}
