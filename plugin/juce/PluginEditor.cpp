#include "PluginEditor.h"

namespace {
constexpr int kMargin = 24;
constexpr int kHeaderHeight = 70;
constexpr int kControlHeight = 94;

juce::Colour panelColour() {
    return juce::Colour::fromRGB(18, 17, 15);
}

juce::Colour accentColour() {
    return juce::Colour::fromRGB(218, 142, 28);
}
}

Dimension5AudioProcessorEditor::Dimension5AudioProcessorEditor(Dimension5AudioProcessor& processor)
    : AudioProcessorEditor(&processor), audioProcessor(processor) {
    setSize(620, 320);

    titleLabel.setText("DIMENSION5", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, accentColour());
    titleLabel.setFont(juce::Font(24.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("BBD-lite stereo dimension chorus", juce::dontSendNotification);
    subtitleLabel.setJustificationType(juce::Justification::centredLeft);
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(137, 129, 99));
    subtitleLabel.setFont(juce::Font(13.0f));
    addAndMakeVisible(subtitleLabel);

    presetBox.addItemList(Dimension5AudioProcessor::factoryPresetNames(), 1);
    presetBox.setSelectedItemIndex(audioProcessor.getCurrentProgram(), juce::dontSendNotification);
    presetBox.onChange = [this] {
        const int selected = presetBox.getSelectedItemIndex();
        if (selected >= 0) {
            audioProcessor.setCurrentProgram(selected);
        }
    };
    addAndMakeVisible(presetBox);

    modeBox.addItemList({ "I", "II", "III", "IV", "Custom" }, 1);
    addAndMakeVisible(modeBox);
    modeAttachment = std::make_unique<ComboAttachment>(audioProcessor.parameters, "mode", modeBox);

    configureSlider(inputSlider, "x");
    configureSlider(outputSlider, "x");
    configureSlider(widthSlider, "");
    configureSlider(mixSlider, "");

    addAndMakeVisible(inputSlider);
    addAndMakeVisible(outputSlider);
    addAndMakeVisible(widthSlider);
    addAndMakeVisible(mixSlider);

    sliderAttachments[0] = std::make_unique<SliderAttachment>(audioProcessor.parameters, "inputGain", inputSlider);
    sliderAttachments[1] = std::make_unique<SliderAttachment>(audioProcessor.parameters, "outputGain", outputSlider);
    sliderAttachments[2] = std::make_unique<SliderAttachment>(audioProcessor.parameters, "width", widthSlider);
    sliderAttachments[3] = std::make_unique<SliderAttachment>(audioProcessor.parameters, "mix", mixSlider);
}

void Dimension5AudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(10, 9, 8));
    auto bounds = getLocalBounds().reduced(10).toFloat();
    g.setColour(panelColour());
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(juce::Colour::fromRGB(50, 42, 28));
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);

    g.setColour(juce::Colour::fromRGB(32, 28, 20));
    g.fillRect(kMargin, kHeaderHeight + 24, getWidth() - 2 * kMargin, 1);

    g.setColour(juce::Colour::fromRGB(132, 82, 12));
    g.drawText("INPUT", inputSlider.getBounds().withY(inputSlider.getBottom() - 18), juce::Justification::centred);
    g.drawText("OUTPUT", outputSlider.getBounds().withY(outputSlider.getBottom() - 18), juce::Justification::centred);
    g.drawText("WIDTH", widthSlider.getBounds().withY(widthSlider.getBottom() - 18), juce::Justification::centred);
    g.drawText("MIX", mixSlider.getBounds().withY(mixSlider.getBottom() - 18), juce::Justification::centred);
}

void Dimension5AudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(kMargin);
    auto header = area.removeFromTop(kHeaderHeight);
    auto textArea = header.removeFromLeft(360);
    titleLabel.setBounds(textArea.removeFromTop(34));
    subtitleLabel.setBounds(textArea.removeFromTop(24));
    auto selectors = header.removeFromRight(210);
    presetBox.setBounds(selectors.removeFromTop(30));
    selectors.removeFromTop(8);
    modeBox.setBounds(selectors.removeFromTop(30));

    area.removeFromTop(34);
    const int controlWidth = area.getWidth() / 4;
    inputSlider.setBounds(area.removeFromLeft(controlWidth).withHeight(kControlHeight));
    outputSlider.setBounds(area.removeFromLeft(controlWidth).withHeight(kControlHeight));
    widthSlider.setBounds(area.removeFromLeft(controlWidth).withHeight(kControlHeight));
    mixSlider.setBounds(area.withHeight(kControlHeight));
}

void Dimension5AudioProcessorEditor::configureSlider(juce::Slider& slider, const juce::String& suffix) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::rotarySliderFillColourId, accentColour());
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB(53, 45, 31));
    slider.setColour(juce::Slider::thumbColourId, accentColour());
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour::fromRGB(208, 200, 168));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}
