#pragma once

#include <JuceHeader.h>
#include "StereoAnalyzer.h"
#include "Dimension5LookAndFeel.h"

/**
 * StereoMetricsComponent
 * 
 * High-legibility professional metrics readout bar:
 * - CORRELATION with graduated graphic indicator (-1 to +1, silence & single-channel handling)
 * - SIDE / MID ratio in dB
 * - MONO SUM integrity in dB
 * - STEREO DELTA expansion in dB
 * 
 * Replaces the cramped 3-column text block with 4 distinct technical instrument cards.
 */
class StereoMetricsComponent final : public juce::Component {
public:
    StereoMetricsComponent();
    ~StereoMetricsComponent() override = default;

    void updateMetrics(const StereoAnalysisFrame& frame);
    void setBypassState(bool isBypassed);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    StereoAnalysisFrame currentFrame {};
    bool bypassActive { false };

    // Smoothed display values for visual stability
    float smoothedCorr { 1.0f };
    float smoothedOutSmDb { -60.0f };
    float smoothedInSmDb { -60.0f };
    float smoothedMonoAttenDb { 0.0f };
    float smoothedStereoDeltaDb { 0.0f };

    void drawMetricCard(juce::Graphics& g, juce::Rectangle<float> bounds,
                        const juce::String& title, const juce::String& valueText,
                        const juce::String& badgeText, juce::Colour accentColour,
                        float meterValue, bool isBipolar, bool isValid);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoMetricsComponent)
};
