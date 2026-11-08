#pragma once

#include <array>
#include <vector>
#include <JuceHeader.h>

#include "StereoAnalyzer.h"

/**
 * StereoFieldComponent
 * 
 * Professional real-time stereo field monitor for Dimension5 BBD Stereo Enhancer.
 * Displays:
 * - Real audio goniometer cloud (Mid-Side phase display with phosphor fading)
 * - Auxiliary parameter geometry (responsive to SPREAD, DEPTH, MOTION, MIX)
 * - L/R phase correlation meter (-1.0 to +1.0 with phase warning)
 * - Side/Mid ratio (in dB)
 * - Input vs Output stereo expansion comparison
 * - Mono compatibility attenuation (dB) with phase cancellation alert
 * - Silence / No Signal state indicator
 */
class StereoFieldComponent final : public juce::Component {
public:
    StereoFieldComponent();
    ~StereoFieldComponent() override = default;

    void updateTelemetry(const StereoTelemetrySnapshot& snapshot);
    void updateParameterGeometry(float spread, float depthMs, float rateHz, float mix);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct CloudPoint {
        float x { 0.0f };
        float y { 0.0f };
        float age { 0.0f };
    };

    static constexpr size_t kMaxCloudPoints = 384;
    std::array<CloudPoint, kMaxCloudPoints> cloudPoints {};
    size_t cloudWritePos { 0 };

    StereoTelemetrySnapshot lastTelemetry {};
    bool hasSignal { false };

    // Smoothed display values
    float displayCorr { 1.0f };
    float displayOutSmDb { -60.0f };
    float displayInSmDb { -60.0f };
    float displayMonoAttenDb { 0.0f };

    // Parameter configuration geometry state
    float paramSpread { 1.0f };
    float paramDepthMs { 0.9f };
    float paramRateHz { 0.25f };
    float paramMix { 1.0f };
    float motionPhase { 0.0f };

    // Layout areas
    juce::Rectangle<int> scopeBounds;
    juce::Rectangle<int> metricsBounds;

    void drawGoniometer(juce::Graphics& g, juce::Rectangle<float> area);
    void drawParamGeometry(juce::Graphics& g, juce::Point<float> center, float radius);
    void drawAudioCloud(juce::Graphics& g, juce::Point<float> center, float radius);
    void drawMetrics(juce::Graphics& g, juce::Rectangle<float> area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoFieldComponent)
};
