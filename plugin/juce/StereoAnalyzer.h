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

/**
 * A pair of genuinely measured input/output samples expressed in Mid/Side,
 * captured at the same sample index. Input and output share the exact same
 * timestamp, which makes DELTA comparisons physically meaningful.
 */
struct StereoComparisonPoint {
    float inputMid { 0.0f };
    float inputSide { 0.0f };
    float outputMid { 0.0f };
    float outputSide { 0.0f };
};

enum class CorrelationStatus : uint8_t {
    Valid = 0,
    Silence,
    LeftOnly,
    RightOnly,
    Undefined
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
    float stereoDeltaDb { 0.0f };
    bool hasSignal { false };
    bool isCorrelationValid { false };
    bool isSideMidValid { false };
    bool isInputSideMidValid { false };
    bool isStereoDeltaValid { false };
    CorrelationStatus correlationStatus { CorrelationStatus::Silence };
};

struct StereoTelemetrySnapshot {
    StereoAnalysisFrame frame;

    // Legacy output-only goniometer cloud (kept for backward compatibility).
    static constexpr size_t kMaxGonioPoints = 128;
    uint32_t gonioCount { 0 };
    std::array<GoniometerPoint, kMaxGonioPoints> gonioPoints {};

    // Real, time-aligned input/output comparison points.
    // Fixed capacity: no allocation is performed by the audio thread.
    static constexpr size_t kMaxComparisonPoints = 512;
    uint32_t comparisonCount { 0 };
    std::array<StereoComparisonPoint, kMaxComparisonPoints> comparisonPoints {};

    // Absolute number of comparison points ever produced. Consumers use it to
    // ingest only the points that are new since their previous update, without
    // duplicating samples or inventing continuity across dropped snapshots.
    uint64_t comparisonEndSequence { 0 };

    void clearComparison() {
        comparisonCount = 0;
        comparisonEndSequence = 0;
    }
};

/**
 * Realtime stereo signal analyzer.
 * Calculates running Mid/Side RMS, cross-correlation, input/output comparison,
 * mono compatibility, and collects decimated, time-aligned comparison points.
 * Operates strictly wait-free: zero dynamic allocations, zero locks, zero NaN/Inf.
 * Block-size independent continuous-time peak decay and RMS averaging.
 */
class StereoAnalyzer {
public:
    StereoAnalyzer() = default;

    void prepare(double sampleRate) {
        const double sr = (sampleRate > 1000.0) ? sampleRate : 48000.0;
        currentSampleRate = static_cast<float>(sr);
        // ~150 ms continuous-time equivalent leaky integrator
        alpha = (float)(1.0 - std::exp(-1.0 / (0.150 * sr)));

        // ~8 kHz comparison point rate, independent of the host block size.
        // This keeps enough temporal resolution for a dense, useful vector
        // display while bounding the memory and copy cost per audio callback.
        const int srInt = (int)(sr + 0.5);
        comparisonDecimation = std::max(1, srInt / 8000);
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

        historyWritePos = 0;
        historyTotal = 0;
        decimationPhase = 0;
    }

    void processBlock(const float* inL, const float* inR,
                      const float* outL, const float* outR,
                      int numSamples,
                      StereoTelemetrySnapshot& snapshot) {
        if (numSamples <= 0 || inL == nullptr || outL == nullptr) {
            snapshot.frame = computeFrame();
            snapshot.gonioCount = 0;
            snapshot.comparisonCount = 0;
            snapshot.comparisonEndSequence = historyTotal;
            return;
        }

        const float* rIn = (inR != nullptr) ? inR : inL;
        const float* rOut = (outR != nullptr) ? outR : outL;

        float blockInPeakL = 0.0f;
        float blockInPeakR = 0.0f;
        float blockOutPeakL = 0.0f;
        float blockOutPeakR = 0.0f;

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

            if (decimationPhase == 0) {
                auto& pt = comparisonHistory[historyWritePos];
                pt.inputMid = iMid;
                pt.inputSide = iSide;
                pt.outputMid = oMid;
                pt.outputSide = oSide;
                historyWritePos = (historyWritePos + 1) % kHistoryCapacity;
                ++historyTotal;
            }
            if (++decimationPhase >= comparisonDecimation) {
                decimationPhase = 0;
            }
        }

        // Flush subnormal values to prevent denormal penalties
        sanitizeAccumulators();

        // Continuous-time exponential peak decay (tau ~ 100 ms), completely independent of block size
        const float peakReleaseTimeSec = 0.100f;
        const float blockDurationSec = static_cast<float>(numSamples) / currentSampleRate;
        const float peakDecay = std::exp(-blockDurationSec / peakReleaseTimeSec);

        inPeakL = std::max(blockInPeakL, inPeakL * peakDecay);
        inPeakR = std::max(blockInPeakR, inPeakR * peakDecay);
        outPeakL = std::max(blockOutPeakL, outPeakL * peakDecay);
        outPeakR = std::max(blockOutPeakR, outPeakR * peakDecay);

        fillSnapshotPoints(snapshot);
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
        const bool outLeftActive = (accumOutLL > kSilenceThreshold || outPeakL > 1e-4f);
        const bool outRightActive = (accumOutRR > kSilenceThreshold || outPeakR > 1e-4f);
        const bool inLeftActive = (accumInLL > kSilenceThreshold || inPeakL > 1e-4f);
        const bool inRightActive = (accumInRR > kSilenceThreshold || inPeakR > 1e-4f);

        const bool outHasSignal = (outLeftActive || outRightActive);
        const bool inHasSignal = (inLeftActive || inRightActive);

        f.hasSignal = outHasSignal || inHasSignal;

        if (!f.hasSignal) {
            f.correlation = 1.0f;
            f.correlationStatus = CorrelationStatus::Silence;
            f.isCorrelationValid = false;
            f.midRms = 0.0f;
            f.sideRms = 0.0f;
            f.inputMidRms = 0.0f;
            f.inputSideRms = 0.0f;
            f.monoRms = 0.0f;
            f.stereoReferenceRms = 0.0f;
            f.sideMidRatioDb = -60.0f;
            f.inputSideMidRatioDb = -60.0f;
            f.monoAttenuationDb = 0.0f;
            f.stereoDeltaDb = 0.0f;
            f.isSideMidValid = false;
            f.isInputSideMidValid = false;
            f.isStereoDeltaValid = false;
            return f;
        }

        // Mid / Side RMS calculations
        f.midRms = std::sqrt(std::max(0.0f, accumOutMM));
        f.sideRms = std::sqrt(std::max(0.0f, accumOutSS));
        f.inputMidRms = std::sqrt(std::max(0.0f, accumInMM));
        f.inputSideRms = std::sqrt(std::max(0.0f, accumInSS));

        // Normalized Correlation: sumLR / sqrt(sumL2 * sumR2)
        if (outLeftActive && outRightActive) {
            const float denom = std::sqrt(std::max(0.0f, accumOutLL * accumOutRR));
            if (denom > 1e-7f) {
                f.correlation = std::clamp(accumOutLR / denom, -1.0f, 1.0f);
                f.correlationStatus = CorrelationStatus::Valid;
                f.isCorrelationValid = true;
            } else {
                f.correlation = 0.0f;
                f.correlationStatus = CorrelationStatus::Undefined;
                f.isCorrelationValid = false;
            }
        } else if (outLeftActive && !outRightActive) {
            f.correlation = 0.0f;
            f.correlationStatus = CorrelationStatus::LeftOnly;
            f.isCorrelationValid = false;
        } else if (!outLeftActive && outRightActive) {
            f.correlation = 0.0f;
            f.correlationStatus = CorrelationStatus::RightOnly;
            f.isCorrelationValid = false;
        } else {
            f.correlation = 0.0f;
            f.correlationStatus = CorrelationStatus::Undefined;
            f.isCorrelationValid = false;
        }

        // Output Side/Mid Ratio in dB
        constexpr float kRmsFloor = 1e-5f;
        if (f.midRms > kRmsFloor && f.sideRms > kRmsFloor) {
            const float ratio = f.sideRms / f.midRms;
            f.sideMidRatioDb = std::clamp(20.0f * std::log10(ratio), -60.0f, 30.0f);
            f.isSideMidValid = true;
        } else if (f.midRms > kRmsFloor && f.sideRms <= kRmsFloor) {
            f.sideMidRatioDb = -60.0f; // Pure mono
            f.isSideMidValid = false;
        } else if (f.sideRms > kRmsFloor && f.midRms <= kRmsFloor) {
            f.sideMidRatioDb = 30.0f; // Pure side / antiphase
            f.isSideMidValid = false;
        } else {
            f.sideMidRatioDb = -60.0f;
            f.isSideMidValid = false;
        }

        // Input Side/Mid Ratio in dB
        if (f.inputMidRms > kRmsFloor && f.inputSideRms > kRmsFloor) {
            const float ratio = f.inputSideRms / f.inputMidRms;
            f.inputSideMidRatioDb = std::clamp(20.0f * std::log10(ratio), -60.0f, 30.0f);
            f.isInputSideMidValid = true;
        } else if (f.inputMidRms > kRmsFloor && f.inputSideRms <= kRmsFloor) {
            f.inputSideMidRatioDb = -60.0f;
            f.isInputSideMidValid = false;
        } else if (f.inputSideRms > kRmsFloor && f.inputMidRms <= kRmsFloor) {
            f.inputSideMidRatioDb = 30.0f;
            f.isInputSideMidValid = false;
        } else {
            f.inputSideMidRatioDb = -60.0f;
            f.isInputSideMidValid = false;
        }

        // Stereo Delta: valid only when both input and output have measurable Side & Mid
        if (f.isSideMidValid && f.isInputSideMidValid) {
            f.stereoDeltaDb = std::clamp(f.sideMidRatioDb - f.inputSideMidRatioDb, -30.0f, 30.0f);
            f.isStereoDeltaValid = true;
        } else {
            f.stereoDeltaDb = 0.0f;
            f.isStereoDeltaValid = false;
        }

        // Mono Compatibility: compare mono RMS to stereo reference RMS
        f.monoRms = f.midRms;
        const float stereoRefEnergy = 0.5f * (accumOutLL + accumOutRR);
        f.stereoReferenceRms = std::sqrt(std::max(0.0f, stereoRefEnergy));

        if (f.stereoReferenceRms > kRmsFloor) {
            const float monoRatio = f.monoRms / f.stereoReferenceRms;
            f.monoAttenuationDb = 20.0f * std::log10(std::max(1e-4f, monoRatio));
        } else {
            f.monoAttenuationDb = 0.0f;
        }

        return f;
    }

private:
    static constexpr size_t kHistoryCapacity = 4096;

    void fillSnapshotPoints(StereoTelemetrySnapshot& snapshot) {
        const uint32_t available = (uint32_t)std::min<uint64_t>(historyTotal, kHistoryCapacity);
        const uint32_t count = std::min<uint32_t>(available, (uint32_t)StereoTelemetrySnapshot::kMaxComparisonPoints);

        // Oldest of the last `count` points.
        const size_t start = (historyWritePos + kHistoryCapacity - count) % kHistoryCapacity;
        for (uint32_t i = 0; i < count; ++i) {
            snapshot.comparisonPoints[i] = comparisonHistory[(start + i) % kHistoryCapacity];
        }
        snapshot.comparisonCount = count;
        snapshot.comparisonEndSequence = historyTotal;

        // Legacy output-only goniometer array derived from the same measured data.
        const uint32_t gonioCount = std::min<uint32_t>(count, (uint32_t)StereoTelemetrySnapshot::kMaxGonioPoints);
        const uint32_t gonioStart = count - gonioCount;
        for (uint32_t i = 0; i < gonioCount; ++i) {
            const auto& cmp = snapshot.comparisonPoints[gonioStart + i];
            snapshot.gonioPoints[i].mid = cmp.outputMid;
            snapshot.gonioPoints[i].side = cmp.outputSide;
        }
        snapshot.gonioCount = gonioCount;
    }

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

    float currentSampleRate { 48000.0f };
    float alpha { 0.005f };
    int comparisonDecimation { 8 };
    int decimationPhase { 0 };

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

    std::array<StereoComparisonPoint, kHistoryCapacity> comparisonHistory {};
    size_t historyWritePos { 0 };
    uint64_t historyTotal { 0 };
};
