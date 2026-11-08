#pragma once

#include <memory>
#include <JuceHeader.h>
#include "Dimension5LookAndFeel.h"

/**
 * SpatialControlsComponent
 * 
 * Right-hand dedicated control panel for Dimension5 (~35% width).
 * Hierarchical organization:
 * - Header: Section title & BBD engine status
 * - Primary Hero Row: SPREAD & MIX (prominent large dials with calibrated arcs)
 * - Secondary Precision Row: DEPTH (ms), MOTION (Hz), COLOR (kHz)
 */
class SpatialControlsComponent final : public juce::Component {
public:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    SpatialControlsComponent();
    ~SpatialControlsComponent() override = default;

    void attachParameters(juce::AudioProcessorValueTreeState& apvts);

    juce::Slider& getSpreadSlider()  { return spreadSlider; }
    juce::Slider& getMixSlider()     { return mixSlider; }
    juce::Slider& getDepthSlider()   { return depthSlider; }
    juce::Slider& getMotionSlider()  { return motionSlider; }
    juce::Slider& getColorSlider()   { return colorSlider; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::Slider spreadSlider;
    juce::Slider mixSlider;
    juce::Slider depthSlider;
    juce::Slider motionSlider;
    juce::Slider colorSlider;

    juce::Label spreadLabel;
    juce::Label mixLabel;
    juce::Label depthLabel;
    juce::Label motionLabel;
    juce::Label colorLabel;

    std::unique_ptr<SliderAttachment> spreadAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> depthAttachment;
    std::unique_ptr<SliderAttachment> motionAttachment;
    std::unique_ptr<SliderAttachment> colorAttachment;

    void configureKnob(juce::Slider& slider, juce::Label& label,
                       const juce::String& text, bool isSpatialHero);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpatialControlsComponent)
};
