#include "PluginEditor.h"

namespace {
constexpr int kMargin = 24;
constexpr int kHeaderHeight = 70;
constexpr int kControlHeight = 94;
constexpr int kMeterHeight = 12;
constexpr int kCompactHeight = 320;
constexpr int kExpandedHeight = 500;

struct AdvancedControlSpec {
    const char* id;
    const char* label;
};

constexpr AdvancedControlSpec kAdvancedControls[] = {
    { "baseDelayMs", "BASE DELAY" },
    { "depthMs", "DEPTH" },
    { "rateHz", "RATE" },
    { "hpfHz", "LOW FOCUS" },
    { "lpfHz", "COLOR" },
    { "analogAmount", "ANALOG" }
};

juce::Colour panelColour() {
    return juce::Colour::fromRGB(18, 17, 15);
}

juce::Colour accentColour() {
    return juce::Colour::fromRGB(218, 142, 28);
}
}

Dimension5AudioProcessorEditor::Dimension5AudioProcessorEditor(Dimension5AudioProcessor& processor)
    : AudioProcessorEditor(&processor), audioProcessor(processor) {
    setSize(620, kCompactHeight);

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

    advancedButton.setButtonText("ADVANCED");
    advancedButton.setClickingTogglesState(true);
    advancedButton.onClick = [this] {
        setAdvancedVisible(advancedButton.getToggleState());
    };
    advancedButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(29, 26, 20));
    advancedButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(43, 33, 18));
    advancedButton.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(137, 129, 99));
    advancedButton.setColour(juce::TextButton::textColourOnId, accentColour());
    addAndMakeVisible(advancedButton);

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

    for (size_t i = 0; i < advancedSliders.size(); ++i) {
        configureAdvancedSlider(advancedSliders[i]);
        advancedLabels[i].setText(kAdvancedControls[i].label, juce::dontSendNotification);
        advancedLabels[i].setJustificationType(juce::Justification::centredLeft);
        advancedLabels[i].setColour(juce::Label::textColourId, juce::Colour::fromRGB(132, 82, 12));
        advancedLabels[i].setFont(juce::Font(11.0f, juce::Font::bold));
        addAndMakeVisible(advancedLabels[i]);
        addAndMakeVisible(advancedSliders[i]);
        advancedAttachments[i] = std::make_unique<SliderAttachment>(
            audioProcessor.parameters, kAdvancedControls[i].id, advancedSliders[i]);
    }
    setAdvancedVisible(false);

    startTimerHz(30);
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

    if (advancedVisible) {
        auto advancedBounds = getLocalBounds().reduced(kMargin);
        advancedBounds.removeFromTop(kHeaderHeight + 128);
        advancedBounds.removeFromBottom(34);
        g.setColour(juce::Colour::fromRGB(32, 28, 20));
        g.fillRect(advancedBounds.removeFromTop(1));
    }

    auto meterArea = getLocalBounds().reduced(kMargin).removeFromBottom(24);
    drawMeter(g, meterArea.removeFromTop(kMeterHeight), meterLeft, "L");
    meterArea.removeFromTop(4);
    drawMeter(g, meterArea.removeFromTop(kMeterHeight), meterRight, "R");
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
    mixSlider.setBounds(area.removeFromLeft(controlWidth).withHeight(kControlHeight));
    advancedButton.setBounds(getWidth() - kMargin - 104, kHeaderHeight + kMargin + 5, 104, 26);

    if (advancedVisible) {
        auto advancedArea = getLocalBounds().reduced(kMargin);
        advancedArea.removeFromTop(kHeaderHeight + 128);
        advancedArea.removeFromBottom(34);
        advancedArea.removeFromTop(14);

        const int rowHeight = 28;
        const int labelWidth = 96;
        for (size_t i = 0; i < advancedSliders.size(); ++i) {
            auto row = advancedArea.removeFromTop(rowHeight);
            advancedLabels[i].setBounds(row.removeFromLeft(labelWidth));
            row.removeFromLeft(10);
            advancedSliders[i].setBounds(row);
            advancedArea.removeFromTop(4);
        }
    }
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

void Dimension5AudioProcessorEditor::configureAdvancedSlider(juce::Slider& slider) {
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 76, 18);
    slider.setColour(juce::Slider::trackColourId, accentColour());
    slider.setColour(juce::Slider::backgroundColourId, juce::Colour::fromRGB(25, 22, 17));
    slider.setColour(juce::Slider::thumbColourId, accentColour());
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour::fromRGB(208, 200, 168));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void Dimension5AudioProcessorEditor::setAdvancedVisible(bool shouldBeVisible) {
    advancedVisible = shouldBeVisible;
    for (size_t i = 0; i < advancedSliders.size(); ++i) {
        advancedLabels[i].setVisible(advancedVisible);
        advancedSliders[i].setVisible(advancedVisible);
    }
    setSize(getWidth(), advancedVisible ? kExpandedHeight : kCompactHeight);
    resized();
    repaint();
}

void Dimension5AudioProcessorEditor::drawMeter(juce::Graphics& g, juce::Rectangle<int> bounds, float value, const juce::String& label) {
    const auto labelArea = bounds.removeFromLeft(18);
    g.setColour(juce::Colour::fromRGB(99, 91, 69));
    g.drawText(label, labelArea, juce::Justification::centredLeft);

    auto meterBounds = bounds.reduced(0, 2);
    g.setColour(juce::Colour::fromRGB(25, 22, 17));
    g.fillRect(meterBounds);

    const int fillWidth = juce::roundToInt((float)meterBounds.getWidth() * juce::jlimit(0.0f, 1.0f, value));
    auto fillBounds = meterBounds.withWidth(fillWidth);
    g.setColour(accentColour());
    g.fillRect(fillBounds);

    g.setColour(juce::Colour::fromRGB(54, 45, 30));
    g.drawRect(meterBounds);
}

void Dimension5AudioProcessorEditor::timerCallback() {
    const float decay = 0.82f;
    meterLeft = juce::jmax(audioProcessor.getOutputMeterLeft(), meterLeft * decay);
    meterRight = juce::jmax(audioProcessor.getOutputMeterRight(), meterRight * decay);
    repaint();
}
