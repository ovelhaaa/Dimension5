#pragma once

#include <stdint.h>
#include "dimension_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum DimensionMode {
    DIMENSION_MODE_I = 0,
    DIMENSION_MODE_II,
    DIMENSION_MODE_III,
    DIMENSION_MODE_IV,
    DIMENSION_MODE_CUSTOM
} DimensionMode;

typedef enum DimensionQuality {
    DIMENSION_QUALITY_PERCEPTUAL = 0,
    DIMENSION_QUALITY_ANALOG_LITE,
    DIMENSION_QUALITY_BBD_LITE
} DimensionQuality;

/* Public realtime DSP API contract:
 * - Supported sample rates: 44.1, 48, and 96 kHz are validated; other rates above
 *   1 kHz are clamped internally to safe ranges. Default is 48 kHz.
 * - Maximum block size per call is DIMENSION_MAX_BLOCK_SIZE frames. Larger n values
 *   are truncated defensively; host code should split bigger buffers.
 * - Audio format is non-interleaved planar float32: inL/inR -> outL/outR, nominal
 *   range [-1.0, +1.0]. External input is sanitized/clamped to [-2.0, +2.0].
 * - Public parameters are clamped to musical/safe ranges by Dimension_SetParams.
 * - DimensionDSP is caller-owned static storage; no malloc/free is used by the core.
 * - Dimension_ProcessBlock performs no I/O and is realtime-safe after initialization.
 * - Thread-safety: one DimensionDSP instance may be used by one audio thread at a time.
 *   Synchronize externally if UI/control and audio threads touch the same instance.
 */
typedef struct DimensionParams {
    float sampleRate;
    DimensionMode mode;
    DimensionQuality quality;
    float inputGain;
    float outputGain;
    float dryGain;
    float wetDirectGain;
    float wetCrossGain;
    float baseDelayMs;
    float depthMs;
    float rateHz;
    float hpfHz;
    float lpfHz;
    float analogAmount;
    float companderAmount;
    float noiseAmount;
    float width;
} DimensionParams;

typedef struct DimensionDSP {
    DimensionParams params;
    DIMENSION_ALIGN_32 float delayL[DIMENSION_DELAY_SIZE];
    DIMENSION_ALIGN_32 float delayR[DIMENSION_DELAY_SIZE];
    uint32_t writePos;
    float lfoPhase;
    float hpfStateL;
    float hpfStateR;
    float lpf1StateL;
    float lpf1StateR;
    float lpf2StateL;
    float lpf2StateR;
    float compEnvL;
    float compEnvR;
    float expEnvL;
    float expEnvR;
    float bbdStateL;
    float bbdStateR;
    float smoothRateHz;
    float smoothDepthMs;
    float smoothBaseDelayMs;
    float smoothWetDirectGain;
    float smoothWetCrossGain;
    float smoothHpfHz;
    float smoothLpfHz;
    float smoothAnalogAmount;
    float smoothCompanderAmount;
    float smoothCoeff;
} DimensionDSP;

void Dimension_Init(DimensionDSP* d, float sampleRate);
void Dimension_Reset(DimensionDSP* d);
void Dimension_SetMode(DimensionDSP* d, DimensionMode mode);
void Dimension_SetParams(DimensionDSP* d, const DimensionParams* p);
void Dimension_GetParams(const DimensionDSP* d, DimensionParams* p);
void Dimension_ProcessBlock(
    DimensionDSP* d,
    const float* inL,
    const float* inR,
    float* outL,
    float* outR,
    uint32_t n);

#ifdef __cplusplus
}
#endif
