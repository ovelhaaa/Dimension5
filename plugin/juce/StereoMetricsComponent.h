#pragma once

#include <JuceHeader.h>
#include "StereoAnalyzer.h"
#include "Dimension5LookAndFeel.h"

/**
 * StereoMetricsComponent 2.1
 *
 * High-legibility measurement strip:
 * - CORRELATION
 * - SIDE / MID
 * - MONO SUM
 * - STEREO DELTA
 *
 * Each card separates title, numeric value, graphical indicator and validity
 * state. Invalid, silent and undefined measurements are never presented with
 * finite numeric values, and no judgement is made when there is no signal.
 */
class StereoMetricsComponent final : public juce::Component {
public:
    StereoMetricsComponent();
    ~StereoMetricsComponent() override = default;

    void updateMetrics(const StereoAnalysisFrame& frame, double deltaSeconds);
    void setBypassState(bool isBypassed);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    StereoAnalysisFrame currentFrame {};
    bool bypassActive { false };

    // Smoothed display values (time-based, FPS independent)
    float smoothedCorr { 1.0f };
    float smoothedOutSmDb { -60.0f };
    float smoothedMonoAttenDb { 0.0f };
    float smoothedStereoDeltaDb { 0.0f };

    void drawMetricCard(juce::Graphics& g, juce::Rectangle<float> bounds,
                        const juce::String& title, const juce::String& valueText,
                        const juce::String& badgeText, juce::Colour accentColour,
                        float meterValue, bool isBipolar, bool isValid);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoMetricsComponent)
};
