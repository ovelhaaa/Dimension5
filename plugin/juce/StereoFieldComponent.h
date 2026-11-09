#pragma once

#include <array>
#include <cstdint>
#include <JuceHeader.h>

#include "StereoAnalyzer.h"
#include "StereoMetricsComponent.h"
#include "Dimension5LookAndFeel.h"

/**
 * StereoFieldComponent 2.1
 *
 * Professional Stereo Field Monitor for Dimension5 BBD Stereo Enhancer.
 *
 * All three views are driven exclusively by genuinely measured audio:
 * - FIELD:  phosphor cloud of real output samples + configuration aperture.
 * - VECTOR: Lissajous trajectory with strict temporal continuity (no invented
 *           segments across dropped samples).
 * - DELTA:  side-by-side INPUT / OUTPUT goniometers built from time-aligned
 *           input/output sample pairs, shown with a common visual scale.
 */
class StereoFieldComponent final : public juce::Component {
public:
    enum class ViewMode {
        Field = 0,
        Vector,
        Delta
    };

    StereoFieldComponent();
    ~StereoFieldComponent() override = default;

    void updateTelemetry(const StereoTelemetrySnapshot& snapshot);
    void updateParameterGeometry(float spread, float depthMs, float rateHz, float mix);
    void setBypassState(bool isBypassed);

    void setViewMode(ViewMode mode);
    ViewMode getViewMode() const noexcept { return currentMode; }

    void resetDisplayState();

    void paint(juce::Graphics& g) override;
    void resized() override;

    // --- Inspection hooks (tests / diagnostics) ---------------------------------
    float getDisplayScale() const noexcept { return displayScale; }
    float getDeltaDisplayScale() const noexcept { return deltaDisplayScale; }
    uint32_t getActiveOutputPointCount() const noexcept;
    uint32_t getActiveInputPointCount() const noexcept;
    uint32_t getVectorSegmentCount() const noexcept;
    uint64_t getLastIngestedSequence() const noexcept { return lastIngestedSequence; }
    bool isAutoScaleActive() const noexcept {
        return displayScale > 1.08f || displayScale < 0.98f;
    }

private:
    struct ComparisonDisplayPoint {
        float inputSide { 0.0f };
        float inputMid { 0.0f };
        float outputSide { 0.0f };
        float outputMid { 0.0f };
        float age { 0.0f };
        uint64_t sequence { 0 };
    };

    ViewMode currentMode { ViewMode::Field };

    juce::TextButton fieldModeButton;
    juce::TextButton vectorModeButton;
    juce::TextButton deltaModeButton;

    StereoMetricsComponent metricsComponent;

    static constexpr size_t kMaxPoints = 1536;
    std::array<ComparisonDisplayPoint, kMaxPoints> points {};
    size_t pointWritePos { 0 };
    size_t pointCount { 0 };
    uint64_t lastIngestedSequence { 0 };

    StereoTelemetrySnapshot lastTelemetry {};
    bool hasSignal { false };
    bool bypassActive { false };

    double lastUpdateTimeMs { 0.0 };

    // Parameter configuration geometry state
    float paramSpread { 1.0f };
    float paramDepthMs { 0.9f };
    float paramRateHz { 0.25f };
    float paramMix { 1.0f };
    float motionPhase { 0.0f };

    // Visual auto-gain envelopes (independent from audio gain)
    float displayScale { 1.0f };
    float deltaDisplayScale { 1.0f };

    juce::Rectangle<int> headerBounds;
    juce::Rectangle<int> scopeBounds;
    juce::Rectangle<int> metricsBounds;

    void updateModeButtons();

    void ingestComparisonPoints(const StereoTelemetrySnapshot& snapshot);
    void updateVisualScale(const StereoTelemetrySnapshot& snapshot, double deltaSeconds);
    void decayPoints(float decayFactor);

    size_t chronologicalIndex(size_t k) const noexcept {
        const size_t oldest = (pointWritePos + kMaxPoints - pointCount) % kMaxPoints;
        return (oldest + k) % kMaxPoints;
    }

    // Mode-specific rendering
    void drawFieldMode(juce::Graphics& g, juce::Rectangle<float> area);
    void drawVectorMode(juce::Graphics& g, juce::Rectangle<float> area);
    void drawDeltaMode(juce::Graphics& g, juce::Rectangle<float> area);

    void drawReticle(juce::Graphics& g, juce::Point<float> center, float radius, bool showDiagonals);
    void drawAuxiliaryAperture(juce::Graphics& g, juce::Point<float> center, float radius);
    void drawAutoScaleBadge(juce::Graphics& g, juce::Rectangle<float> area, float scale);
    void drawBypassBadge(juce::Graphics& g, juce::Rectangle<float> area);

    juce::Path buildVectorPath(juce::Rectangle<float> area) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoFieldComponent)
};
