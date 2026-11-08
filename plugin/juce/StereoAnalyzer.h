#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

struct GoniometerPoint {
    float side { 0.0f };
    float mid { 0.0f };
};

struct StereoAnalysisFrame {
    float correlation { 1.0f };
    float midRms { 0.0f };
    float sideRms { 0.0f };
    float inputMidRms { 0.0f };
    float inputSideRms { 0.0f };
    float monoRms { 0.0f };
    float stereoReferenceRms { 0.0f };
    float outputPeakL { 0.0f };
    float outputPeakR { 0.0f };
    float inputPeakL { 0.0f };
    float inputPeakR { 0.0f };
    float sideMidRatioDb { -60.0f };
    float inputSideMidRatioDb { -60.0f };
    float monoAttenuationDb { 0.0f };
    bool hasSignal { false };
};

struct StereoTelemetrySnapshot {
    StereoAnalysisFrame frame;
    static constexpr size_t kMaxGonioPoints = 128;
    uint32_t gonioCount { 0 };
    std::array<GoniometerPoint, kMaxGonioPoints> gonioPoints {};
};

/**
 * Realtime stereo signal analyzer.
 * Calculates running Mid/Side RMS, cross-correlation, input/output comparison,
 * mono compatibility, and collects decimated goniometer points.
 * Operates strictly wait-free: zero dynamic allocations, zero locks, zero NaN/Inf.
 */
class StereoAnalyzer {
public:
    StereoAnalyzer() = default;

    void prepare(double sampleRate) {
        const double sr = (sampleRate > 1000.0) ? sampleRate : 48000.0;
        // ~150 ms continuous-time equivalent leaky integrator
        alpha = (float)(1.0 - std::exp(-1.0 / (0.150 * sr)));
        reset();
    }

    void reset() {
        accumInLL = 0.0f;
        accumInRR = 0.0f;
        accumInMM = 0.0f;
        accumInSS = 0.0f;

        accumOutLL = 0.0f;
        accumOutRR = 0.0f;
        accumOutLR = 0.0f;
        accumOutMM = 0.0f;
        accumOutSS = 0.0f;

        inPeakL = 0.0f;
        inPeakR = 0.0f;
        outPeakL = 0.0f;
        outPeakR = 0.0f;
    }

    void processBlock(const float* inL, const float* inR,
                      const float* outL, const float* outR,
                      int numSamples,
                      StereoTelemetrySnapshot& snapshot) {
        if (numSamples <= 0 || inL == nullptr || outL == nullptr) {
            snapshot.frame = computeFrame();
            snapshot.gonioCount = 0;
            return;
        }

        const float* rIn = (inR != nullptr) ? inR : inL;
        const float* rOut = (outR != nullptr) ? outR : outL;

        float blockInPeakL = 0.0f;
        float blockInPeakR = 0.0f;
        float blockOutPeakL = 0.0f;
        float blockOutPeakR = 0.0f;

        // Decimation step to collect up to kMaxGonioPoints across the block
        const int step = std::max(1, numSamples / (int)StereoTelemetrySnapshot::kMaxGonioPoints);
        uint32_t gonioIdx = 0;

        for (int i = 0; i < numSamples; ++i) {
            const float iL = inL[i];
            const float iR = rIn[i];
            const float oL = outL[i];
            const float oR = rOut[i];

            blockInPeakL = std::max(blockInPeakL, std::abs(iL));
            blockInPeakR = std::max(blockInPeakR, std::abs(iR));
            blockOutPeakL = std::max(blockOutPeakL, std::abs(oL));
            blockOutPeakR = std::max(blockOutPeakR, std::abs(oR));

            const float iMid = (iL + iR) * 0.5f;
            const float iSide = (iL - iR) * 0.5f;
            const float oMid = (oL + oR) * 0.5f;
            const float oSide = (oL - oR) * 0.5f;

            accumInLL += alpha * (iL * iL - accumInLL);
            accumInRR += alpha * (iR * iR - accumInRR);
            accumInMM += alpha * (iMid * iMid - accumInMM);
            accumInSS += alpha * (iSide * iSide - accumInSS);

            accumOutLL += alpha * (oL * oL - accumOutLL);
            accumOutRR += alpha * (oR * oR - accumOutRR);
            accumOutLR += alpha * (oL * oR - accumOutLR);
            accumOutMM += alpha * (oMid * oMid - accumOutMM);
            accumOutSS += alpha * (oSide * oSide - accumOutSS);

            if ((i % step) == 0 && gonioIdx < StereoTelemetrySnapshot::kMaxGonioPoints) {
                snapshot.gonioPoints[gonioIdx].side = oSide;
                snapshot.gonioPoints[gonioIdx].mid = oMid;
                ++gonioIdx;
            }
        }

        // Flush subnormal values to prevent denormal penalties
        sanitizeAccumulators();

        // Smooth peak values (fast attack, 100ms release)
        const float peakDecay = 0.85f;
        inPeakL = std::max(blockInPeakL, inPeakL * peakDecay);
        inPeakR = std::max(blockInPeakR, inPeakR * peakDecay);
        outPeakL = std::max(blockOutPeakL, outPeakL * peakDecay);
        outPeakR = std::max(blockOutPeakR, outPeakR * peakDecay);

        snapshot.gonioCount = gonioIdx;
        snapshot.frame = computeFrame();
    }

    StereoAnalysisFrame computeFrame() const {
        StereoAnalysisFrame f;
        f.inputPeakL = inPeakL;
        f.inputPeakR = inPeakR;
        f.outputPeakL = outPeakL;
        f.outputPeakR = outPeakR;

        // Signal detection threshold (~ -80 dBFS)
        constexpr float kSilenceThreshold = 1e-8f;
        const bool outHasSignal = (accumOutLL > kSilenceThreshold || accumOutRR > kSilenceThreshold || outPeakL > 1e-4f || outPeakR > 1e-4f);
        const bool inHasSignal = (accumInLL > kSilenceThreshold || accumInRR > kSilenceThreshold || inPeakL > 1e-4f || inPeakR > 1e-4f);

        f.hasSignal = outHasSignal || inHasSignal;

        if (!f.hasSignal) {
            f.correlation = 1.0f;
            f.midRms = 0.0f;
            f.sideRms = 0.0f;
            f.inputMidRms = 0.0f;
            f.inputSideRms = 0.0f;
            f.monoRms = 0.0f;
            f.stereoReferenceRms = 0.0f;
            f.sideMidRatioDb = -60.0f;
            f.inputSideMidRatioDb = -60.0f;
            f.monoAttenuationDb = 0.0f;
            return f;
        }

        // Mid / Side RMS calculations
        f.midRms = std::sqrt(std::max(0.0f, accumOutMM));
        f.sideRms = std::sqrt(std::max(0.0f, accumOutSS));
        f.inputMidRms = std::sqrt(std::max(0.0f, accumInMM));
        f.inputSideRms = std::sqrt(std::max(0.0f, accumInSS));

        // Normalized Correlation: sumLR / sqrt(sumL2 * sumR2)
        const float denom = std::sqrt(std::max(0.0f, accumOutLL * accumOutRR));
        if (denom > 1e-7f) {
            f.correlation = std::clamp(accumOutLR / denom, -1.0f, 1.0f);
        } else {
            // Signal present in only one channel -> correlation is undefined/zero
            f.correlation = 0.0f;
        }

        // Output Side/Mid Ratio in dB
        if (f.midRms > 1e-5f && f.sideRms > 1e-5f) {
            const float ratio = f.sideRms / f.midRms;
            f.sideMidRatioDb = std::clamp(20.0f * std::log10(ratio), -60.0f, 30.0f);
        } else if (f.midRms > 1e-5f && f.sideRms <= 1e-5f) {
            f.sideMidRatioDb = -60.0f; // Pure mono
        } else if (f.sideRms > 1e-5f && f.midRms <= 1e-5f) {
            f.sideMidRatioDb = 30.0f; // Pure side / antiphase
        } else {
            f.sideMidRatioDb = -60.0f;
        }

        // Input Side/Mid Ratio in dB
        if (f.inputMidRms > 1e-5f && f.inputSideRms > 1e-5f) {
            const float ratio = f.inputSideRms / f.inputMidRms;
            f.inputSideMidRatioDb = std::clamp(20.0f * std::log10(ratio), -60.0f, 30.0f);
        } else if (f.inputMidRms > 1e-5f && f.inputSideRms <= 1e-5f) {
            f.inputSideMidRatioDb = -60.0f;
        } else if (f.inputSideRms > 1e-5f && f.inputMidRms <= 1e-5f) {
            f.inputSideMidRatioDb = 30.0f;
        } else {
            f.inputSideMidRatioDb = -60.0f;
        }

        // Mono Compatibility: compare mono RMS to stereo reference RMS
        f.monoRms = f.midRms;
        const float stereoRefEnergy = 0.5f * (accumOutLL + accumOutRR);
        f.stereoReferenceRms = std::sqrt(std::max(0.0f, stereoRefEnergy));

        if (f.stereoReferenceRms > 1e-5f) {
            const float monoRatio = f.monoRms / f.stereoReferenceRms;
            f.monoAttenuationDb = 20.0f * std::log10(std::max(1e-4f, monoRatio));
        } else {
            f.monoAttenuationDb = 0.0f;
        }

        return f;
    }

private:
    void sanitizeAccumulators() {
        auto clean = [](float& val) {
            if (std::abs(val) < 1e-15f || !std::isfinite(val)) {
                val = 0.0f;
            }
        };
        clean(accumInLL);
        clean(accumInRR);
        clean(accumInMM);
        clean(accumInSS);
        clean(accumOutLL);
        clean(accumOutRR);
        clean(accumOutLR);
        clean(accumOutMM);
        clean(accumOutSS);
    }

    float alpha { 0.005f };

    float accumInLL { 0.0f };
    float accumInRR { 0.0f };
    float accumInMM { 0.0f };
    float accumInSS { 0.0f };

    float accumOutLL { 0.0f };
    float accumOutRR { 0.0f };
    float accumOutLR { 0.0f };
    float accumOutMM { 0.0f };
    float accumOutSS { 0.0f };

    float inPeakL { 0.0f };
    float inPeakR { 0.0f };
    float outPeakL { 0.0f };
    float outPeakR { 0.0f };
};
