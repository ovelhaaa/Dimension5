#pragma once

#include <array>
#include <vector>
#include <JuceHeader.h>

#include "StereoAnalyzer.h"
#include "StereoMetricsComponent.h"
#include "Dimension5LookAndFeel.h"

/**
 * StereoFieldComponent 2.0
 * 
 * Professional Stereo Field Monitor for Dimension5 BBD Stereo Enhancer.
 * Protagonist of the user interface (~65% width).
 * 
 * Three selectable view modes:
 * - FIELD:  Dynamic phosphorescent cloud + parameter aperture/dispersion geometry
 * - VECTOR: High-precision Lissajous / Goniometer with phase & vector envelope
 * - DELTA:  Comparative Input vs Output expansion and mono compatibility
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

    void resetDisplayState() {
        for (auto& pt : cloudPoints) pt.age = 0.0f;
        for (auto& pt : inputPoints) pt.age = 0.0f;
        displayScale = 1.0f;
        repaint();
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct CloudPoint {
        float x { 0.0f };
        float y { 0.0f };
        float age { 0.0f };
    };

    ViewMode currentMode { ViewMode::Field };

    // View selector pill buttons
    juce::TextButton fieldModeButton;
    juce::TextButton vectorModeButton;
    juce::TextButton deltaModeButton;

    // Metrics panel embedded at bottom
    StereoMetricsComponent metricsComponent;

    // Cloud & vector history (ring buffer)
    static constexpr size_t kMaxCloudPoints = 512;
    std::array<CloudPoint, kMaxCloudPoints> cloudPoints {};
    size_t cloudWritePos { 0 };

    // Input signal history for DELTA mode comparison
    static constexpr size_t kMaxInputPoints = 128;
    std::array<CloudPoint, kMaxInputPoints> inputPoints {};
    size_t inputWritePos { 0 };

    StereoTelemetrySnapshot lastTelemetry {};
    bool hasSignal { false };
    bool bypassActive { false };

    // Continuous time persistence tracking
    double lastUpdateTimeMs { 0.0 };

    // Parameter configuration geometry state
    float paramSpread { 1.0f };
    float paramDepthMs { 0.9f };
    float paramRateHz { 0.25f };
    float paramMix { 1.0f };
    float motionPhase { 0.0f };

    // Auto-scale envelope
    float displayScale { 1.0f };

    // Layout areas
    juce::Rectangle<int> headerBounds;
    juce::Rectangle<int> scopeBounds;
    juce::Rectangle<int> metricsBounds;

    void updateModeButtons();

    // Mode-specific rendering
    void drawFieldMode(juce::Graphics& g, juce::Rectangle<float> area);
    void drawVectorMode(juce::Graphics& g, juce::Rectangle<float> area);
    void drawDeltaMode(juce::Graphics& g, juce::Rectangle<float> area);

    // Reticle & Auxiliary Geometry
    void drawReticle(juce::Graphics& g, juce::Point<float> center, float radius, bool showDiagonals);
    void drawAuxiliaryAperture(juce::Graphics& g, juce::Point<float> center, float radius);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoFieldComponent)
};
