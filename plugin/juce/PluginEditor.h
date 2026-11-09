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

    // Synchronous presentation refresh used by the deterministic snapshot
    // harness (the normal editor drives the same code from its timer).
    void refreshPresentationNow() { timerCallback(); }

    void resetMeterDisplay() {
        inMeterLeft = inMeterRight = outMeterLeft = outMeterRight = 0.0f;
        inPeakHoldLeft = inPeakHoldRight = outPeakHoldLeft = outPeakHoldRight = 0.0f;
        repaint(bottomBarBounds);
    }

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
    juce::Label inputDbLabel;
    juce::Label outputDbLabel;
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

    // Meter levels (smoothed), peak hold and bounds
    float inMeterLeft = 0.0f;
    float inMeterRight = 0.0f;
    float outMeterLeft = 0.0f;
    float outMeterRight = 0.0f;
    float inPeakHoldLeft = 0.0f;
    float inPeakHoldRight = 0.0f;
    float outPeakHoldLeft = 0.0f;
    float outPeakHoldRight = 0.0f;

    juce::Rectangle<int> inMeterBounds;
    juce::Rectangle<int> outMeterBounds;
    juce::Rectangle<int> bottomBarBounds;
    juce::Rectangle<int> advancedBounds;

    bool advancedVisible = false;
    int heightBeforeAdvanced = 0;
    double lastTimerMs = 0.0;

    void configureBottomSlider(juce::Slider& slider, juce::Label& label,
                               const juce::String& labelText, const juce::String& suffix);
    void configureAdvancedSlider(juce::Slider& slider, juce::Label& label,
                                 const juce::String& labelText);
    void configureHeaderButton(juce::TextButton& button);

    void updateCompareButtons();
    void drawStereoMeter(juce::Graphics& g, juce::Rectangle<int> bounds,
                         float leftVal, float rightVal,
                         float leftPeak, float rightPeak);
    void layoutBottomSection(juce::Rectangle<int> section, juce::Slider& slider,
                             juce::Label& label, juce::Label& dbLabel,
                             juce::Rectangle<int>& meterBounds);

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dimension5AudioProcessorEditor)
};
