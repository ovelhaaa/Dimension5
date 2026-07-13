#pragma once

#include <array>
#include <memory>

#include <JuceHeader.h>

#include "PluginProcessor.h"

class Dimension5AudioProcessorEditor final : public juce::AudioProcessorEditor {
public:
    explicit Dimension5AudioProcessorEditor(Dimension5AudioProcessor& processor);
    ~Dimension5AudioProcessorEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    Dimension5AudioProcessor& audioProcessor;
    juce::ComboBox modeBox;
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Slider inputSlider;
    juce::Slider outputSlider;
    juce::Slider widthSlider;
    juce::Slider mixSlider;
    std::unique_ptr<ComboAttachment> modeAttachment;
    std::array<std::unique_ptr<SliderAttachment>, 4> sliderAttachments;

    void configureSlider(juce::Slider& slider, const juce::String& suffix);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dimension5AudioProcessorEditor)
};
