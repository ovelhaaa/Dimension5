#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "dimension_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DimensionBBDClock — Physical Clock & Temporal Character Modeling
 *
 * Models the temporal characteristics of an MN3007 Bucket Brigade Device (BBD)
 * as used in classic analog chorus circuits (e.g. Roland SDD-320 Dimension D):
 *
 * 1. Physical Clock Mapping:
 *    - An MN3007 contains 1024 stages clocked by a 2-phase clock (CP1, CP2).
 *    - Charge advances two stages per full clock cycle (one stage per half-cycle).
 *    - Effective delay: T_d = 512 / f_clock.
 *    - Instantaneous clock: f_clock(t) = 512000.0f / delayMs(t) [Hz].
 *    - Nominal operating range: ~40 kHz to ~85 kHz across Dimension modes I..IV.
 *
 * 2. Clock Modulation Curvature (VCO Relaxation Inertia):
 *    - Models the analog clock driver (MN3101 + transistor current source)
 *      relaxation contour, softly rounding the LFO turnaround points to eliminate
 *      artificial piecewise-linear corner discontinuities.
 *
 * 3. Charge Transfer Inefficiency (CTE) & Baseband Dispersion:
 *    - Over 1024 transfers, charge transfer inefficiency (epsilon ~ 1e-4) induces
 *      a dynamic high-frequency dispersion and group delay that breathes with
 *      instantaneous clock rate (slower clock -> warmer, softer transients).
 *
 * 4. Micro-Time Transport Elasticity:
 *    - Dynamic bucket transit delay variation proportional to clock modulation velocity.
 *
 * Neutrality Contract:
 * - When bbdCharacter == 0.0f, all calculations are mathematically neutral,
 *   preserving exact bit-level identity with Spatial Engine 2.0.
 */

#define DIMENSION_BBD_NOMINAL_CLOCK_HZ 60000.0f
#define DIMENSION_BBD_MIN_CLOCK_HZ     16000.0f
#define DIMENSION_BBD_MAX_CLOCK_HZ     120000.0f

typedef struct DimensionBBDClockChannel {
    float instantaneousClockHz;
    float dispState;
} DimensionBBDClockChannel;

typedef struct DimensionBBDClock {
    DimensionBBDClockChannel chL;
    DimensionBBDClockChannel chR;
} DimensionBBDClock;

void DimensionBBDClock_Init(DimensionBBDClock* clock);
void DimensionBBDClock_Reset(DimensionBBDClock* clock);

/**
 * Computes instantaneous BBD clock frequency from delay in milliseconds.
 * MN3007: 1024 stages / 2 phases = 512 clock periods.
 */
static inline float DimensionBBDClock_DelayToFrequency(float delayMs) {
    if (delayMs < 0.5f) {
        delayMs = 0.5f;
    }
    return 512000.0f / delayMs;
}

/**
 * Evaluates smooth VCO relaxation contour for LFO modulation.
 * When bbdCharacter == 0, returns tri identically.
 * When bbdCharacter > 0, softly curves the turnaround peaks.
 */
static inline float DimensionBBDClock_ModulateLfo(float tri, float bbdChar) {
    if (bbdChar <= 0.0f) {
        return tri;
    }
    /* Polynomial turnaround curve: f(x) = 1.25x - 0.25x^3
     * At x = 0 -> 0.0
     * At x = 1 -> 1.0, slope = 1.25 - 0.75 = 0.50 (smooth rounded crest)
     * At x = -1 -> -1.0, slope = 0.50 */
    const float tri3 = tri * tri * tri;
    const float curved = 1.25f * tri - 0.25f * tri3;
    return tri + bbdChar * (curved - tri);
}

/**
 * Computes micro-time transport lag in samples based on relative clock shift.
 */
static inline float DimensionBBDClock_TransportLag(float clockHz, float nominalClockHz, float bbdChar) {
    if (bbdChar <= 0.0f || nominalClockHz < 1000.0f) {
        return 0.0f;
    }
    const float relClockShift = (clockHz - nominalClockHz) / nominalClockHz;
    /* Scale: up to ~0.12 samples at full character and maximum modulation depth */
    return bbdChar * 0.12f * relClockShift;
}

/**
 * Processes wet sample through clock-dependent baseband charge dispersion filter.
 * When bbdChar == 0, returns input sample untouched.
 */
static inline float DimensionBBDClock_ProcessDispersion(
    DimensionBBDClockChannel* ch,
    float sample,
    float clockHz,
    float sampleRate,
    float bbdChar) {
    if (bbdChar <= 0.0f) {
        return sample;
    }

    /* Dynamic cutoff tracks BBD clock frequency:
     * Clock 75 kHz -> fc ~ 14.5 kHz
     * Clock 45 kHz -> fc ~ 9.0 kHz */
    const float clockRatio = clockHz * (1.0f / DIMENSION_BBD_NOMINAL_CLOCK_HZ);
    float fc = 11500.0f + 4500.0f * (clockRatio - 1.0f);
    const float maxFc = 0.45f * sampleRate;
    if (fc < 2500.0f) fc = 2500.0f;
    if (fc > maxFc) fc = maxFc;

    /* Single-pole alpha approximation: 2*pi*fc/sr (accurate for fc << sr, no expf) */
    const float alpha = (6.28318530718f * fc) / sampleRate;
    const float safeAlpha = (alpha > 0.95f) ? 0.95f : alpha;

    ch->dispState += safeAlpha * (sample - ch->dispState);
    if (!ch->dispState) {
        ch->dispState = 0.0f;
    }

    /* Blend dispersion softening according to bbdCharacter */
    return sample + bbdChar * 0.35f * (ch->dispState - sample);
}

#ifdef __cplusplus
}
#endif
