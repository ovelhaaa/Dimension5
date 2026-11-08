#pragma once

#include <array>
#include <memory>
#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "Dimension5LookAndFeel.h"
#include "StereoFieldComponent.h"
#include "SpatialControlsComponent.h"

class Dimension5AudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer {
public:
    explicit Dimension5AudioProcessorEditor(Dimension5AudioProcessor& ownerProcessor);
    ~Dimension5AudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    StereoFieldComponent& getStereoFieldComponent() noexcept { return stereoFieldComponent; }
    SpatialControlsComponent& getSpatialControlsComponent() noexcept { return spatialControlsComponent; }
    bool isAdvancedVisible() const noexcept { return advancedVisible; }
    void setAdvancedVisible(bool shouldBeVisible);

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    Dimension5AudioProcessor& audioProcessor;
    Dimension5LookAndFeel lnf;

    // Header Components
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::ComboBox presetBox;
    juce::ComboBox modeBox;
    juce::TextButton compareAButton;
    juce::TextButton compareBButton;
    juce::TextButton compareCopyButton;
    juce::TextButton bypassButton;

    // Central Area Components
    StereoFieldComponent stereoFieldComponent;
    SpatialControlsComponent spatialControlsComponent;

    // Bottom Bar Components
    juce::Slider inputSlider;
    juce::Slider outputSlider;
    juce::Label inputLabel;
    juce::Label outputLabel;
    juce::TextButton advancedButton;

    // Advanced Drawer Components
    juce::Label advancedTitleLabel;
    juce::Label advancedHintLabel;
    std::array<juce::Slider, 7> advancedSliders;
    std::array<juce::Label, 7> advancedLabels;

    // Attachments
    std::unique_ptr<ComboAttachment> modeAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::unique_ptr<SliderAttachment> inputAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;
    std::array<std::unique_ptr<SliderAttachment>, 7> advancedAttachments;

    // Meter levels (smoothed) and bounds
    float inMeterLeft = 0.0f;
    float inMeterRight = 0.0f;
    float outMeterLeft = 0.0f;
    float outMeterRight = 0.0f;
    juce::Rectangle<int> inMeterBounds;
    juce::Rectangle<int> outMeterBounds;

    bool advancedVisible = false;

    void configureBottomSlider(juce::Slider& slider, juce::Label& label,
                               const juce::String& labelText, const juce::String& suffix);
    void configureAdvancedSlider(juce::Slider& slider, juce::Label& label,
                                 const juce::String& labelText);
    void configureHeaderButton(juce::TextButton& button);

    void updateCompareButtons();
    void drawStereoMeter(juce::Graphics& g, juce::Rectangle<int> bounds,
                         float leftVal, float rightVal, const juce::String& label);

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dimension5AudioProcessorEditor)
};
