#pragma once

#include <array>
#include <memory>

#include <JuceHeader.h>

#include "PluginProcessor.h"

class Dimension5AudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer {
public:
    explicit Dimension5AudioProcessorEditor(Dimension5AudioProcessor& processor);
    ~Dimension5AudioProcessorEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    Dimension5AudioProcessor& audioProcessor;
    juce::ComboBox presetBox;
    juce::ComboBox modeBox;
    juce::TextButton compareAButton;
    juce::TextButton compareBButton;
    juce::TextButton compareCopyButton;
    juce::TextButton bypassButton;
    juce::TextButton advancedButton;
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label advancedHintLabel;
    juce::Slider inputSlider;
    juce::Slider outputSlider;
    juce::Slider widthSlider;
    juce::Slider mixSlider;
    std::array<juce::Slider, 6> advancedSliders;
    std::array<juce::Label, 6> advancedLabels;
    std::unique_ptr<ComboAttachment> modeAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::array<std::unique_ptr<SliderAttachment>, 4> sliderAttachments;
    std::array<std::unique_ptr<SliderAttachment>, 6> advancedAttachments;
    float meterLeft = 0.0f;
    float meterRight = 0.0f;
    bool advancedVisible = false;

    void configureSlider(juce::Slider& slider, const juce::String& suffix);
    void configureSmallButton(juce::TextButton& button);
    void configureAdvancedSlider(juce::Slider& slider);
    void setAdvancedVisible(bool shouldBeVisible);
    void updateCompareButtons();
    void switchAdvancedToCustom();
    void drawMeter(juce::Graphics& g, juce::Rectangle<int> bounds, float value, const juce::String& label);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dimension5AudioProcessorEditor)
};
