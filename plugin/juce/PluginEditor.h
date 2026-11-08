#pragma once

#include <array>
#include <memory>
#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "StereoFieldComponent.h"

/**
 * Dimension5LookAndFeel
 * Contemporary precision rack aesthetic with custom palette:
 * - Background: #10171C
 * - Surface:    #1B272D
 * - Dividers:   #34444B
 * - Text:       #E8EFEC
 * - Secondary:  #899DA3
 * - Spatial:    #65DFC8
 * - Analog:     #E2A65A
 * - Warning:    #E27865
 */
class Dimension5LookAndFeel : public juce::LookAndFeel_V4 {
public:
    Dimension5LookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
};

class Dimension5AudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer {
public:
    explicit Dimension5AudioProcessorEditor(Dimension5AudioProcessor& ownerProcessor);
    ~Dimension5AudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    Dimension5AudioProcessor& audioProcessor;
    Dimension5LookAndFeel lnf;

    // Header Components
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label taglineLabel;
    juce::ComboBox presetBox;
    juce::ComboBox modeBox;
    juce::TextButton compareAButton;
    juce::TextButton compareBButton;
    juce::TextButton compareCopyButton;
    juce::TextButton bypassButton;

    // Stereo Field Monitor
    StereoFieldComponent stereoFieldComponent;

    // Main Spatial / Character Controls
    juce::Slider spreadSlider;   // width
    juce::Slider mixSlider;      // mix
    juce::Slider depthSlider;    // depthMs
    juce::Slider motionSlider;   // rateHz
    juce::Slider colorSlider;    // lpfHz

    juce::Label spreadLabel;
    juce::Label mixLabel;
    juce::Label depthLabel;
    juce::Label motionLabel;
    juce::Label colorLabel;

    // Bottom Bar Controls & Meters
    juce::Slider inputSlider;    // inputGain
    juce::Slider outputSlider;   // outputGain
    juce::Label inputLabel;
    juce::Label outputLabel;
    juce::TextButton advancedButton;

    // Advanced Panel Components (DSP controls not on main panel)
    juce::Label advancedTitleLabel;
    juce::Label advancedHintLabel;
    std::array<juce::Slider, 7> advancedSliders;
    std::array<juce::Label, 7> advancedLabels;

    // Attachments
    std::unique_ptr<ComboAttachment> modeAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    std::unique_ptr<SliderAttachment> spreadAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> depthAttachment;
    std::unique_ptr<SliderAttachment> motionAttachment;
    std::unique_ptr<SliderAttachment> colorAttachment;

    std::unique_ptr<SliderAttachment> inputAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;

    std::array<std::unique_ptr<SliderAttachment>, 7> advancedAttachments;

    // Meter levels (smoothed)
    float inMeterLeft = 0.0f;
    float inMeterRight = 0.0f;
    float outMeterLeft = 0.0f;
    float outMeterRight = 0.0f;

    bool advancedVisible = false;

    void configureRotaryKnob(juce::Slider& slider, juce::Label& label,
                             const juce::String& labelText, const juce::String& suffix,
                             bool isSpatialAccent);
    void configureBottomSlider(juce::Slider& slider, juce::Label& label,
                               const juce::String& labelText, const juce::String& suffix);
    void configureAdvancedSlider(juce::Slider& slider, juce::Label& label,
                                 const juce::String& labelText);
    void configureSmallButton(juce::TextButton& button);

    void setAdvancedVisible(bool shouldBeVisible);
    void updateCompareButtons();
    void drawStereoMeter(juce::Graphics& g, juce::Rectangle<int> bounds,
                         float leftVal, float rightVal, const juce::String& label);

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dimension5AudioProcessorEditor)
};
