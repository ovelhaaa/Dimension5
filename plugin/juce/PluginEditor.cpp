#include "PluginEditor.h"

namespace {
using namespace Dimension5UI;

constexpr int kMargin = 14;
constexpr int kHeaderHeight = 52;
constexpr int kBottomBarHeight = 50;
constexpr int kBaseWidth = 1000;
constexpr int kBaseHeight = 620;
constexpr int kExpandedHeight = 780;

struct AdvancedSpec {
    const char* id;
    const char* label;
    const char* suffix;
};

constexpr AdvancedSpec kAdvancedSpecs[7] = {
    { "baseDelayMs",     "BASE DELAY", " ms" },
    { "hpfHz",           "LOW FOCUS",  " Hz" },
    { "analogAmount",    "ANALOG",     "" },
    { "companderAmount", "COMPANDER",  "" },
    { "dryGain",         "DRY",        "x" },
    { "wetDirectGain",   "WET DIRECT", "x" },
    { "wetCrossGain",    "WET CROSS",  "x" }
};
} // namespace

Dimension5AudioProcessorEditor::Dimension5AudioProcessorEditor(Dimension5AudioProcessor& ownerProcessor)
    : AudioProcessorEditor(&ownerProcessor), audioProcessor(ownerProcessor) {
    setLookAndFeel(&lnf);
    setSize(kBaseWidth, kBaseHeight);
    setResizable(true, true);
    setResizeLimits(840, 540, 1600, 1000);

    // =========================================================================
    // 1. Header Section Setup
    // =========================================================================
    titleLabel.setText("DIMENSION5", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, primaryText());
    titleLabel.setFont(juce::Font(20.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("BBD STEREO ENHANCER", juce::dontSendNotification);
    subtitleLabel.setJustificationType(juce::Justification::centredLeft);
    subtitleLabel.setColour(juce::Label::textColourId, spatialAccent());
    subtitleLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(subtitleLabel);

    // Wide preset dropdown to avoid truncation
    presetBox.addItemList(Dimension5AudioProcessor::factoryPresetNames(), 1);
    presetBox.setSelectedItemIndex(audioProcessor.getCurrentProgram(), juce::dontSendNotification);
    presetBox.onChange = [this] {
        const int selected = presetBox.getSelectedItemIndex();
        if (selected >= 0) {
            audioProcessor.setCurrentProgram(selected);
        }
    };
    addAndMakeVisible(presetBox);

    modeBox.addItemList({ "MODE I", "MODE II", "MODE III", "MODE IV", "CUSTOM" }, 1);
    addAndMakeVisible(modeBox);
    modeAttachment = std::make_unique<ComboAttachment>(audioProcessor.parameters, "mode", modeBox);

    compareAButton.setButtonText("A");
    compareAButton.setClickingTogglesState(false);
    compareAButton.onClick = [this] {
        audioProcessor.selectCompareSlot(0);
        updateCompareButtons();
    };
    configureHeaderButton(compareAButton);
    addAndMakeVisible(compareAButton);

    compareBButton.setButtonText("B");
    compareBButton.setClickingTogglesState(false);
    compareBButton.onClick = [this] {
        audioProcessor.selectCompareSlot(1);
        updateCompareButtons();
    };
    configureHeaderButton(compareBButton);
    addAndMakeVisible(compareBButton);

    compareCopyButton.setButtonText("COPY");
    compareCopyButton.onClick = [this] {
        audioProcessor.copyCompareToOtherSlot();
    };
    configureHeaderButton(compareCopyButton);
    addAndMakeVisible(compareCopyButton);
    updateCompareButtons();

    bypassButton.setButtonText("BYPASS");
    bypassButton.setClickingTogglesState(true);
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "bypass", bypassButton);

    // =========================================================================
    // 2. Central Section Setup
    // =========================================================================
    addAndMakeVisible(stereoFieldComponent);

    spatialControlsComponent.attachParameters(audioProcessor.parameters);
    addAndMakeVisible(spatialControlsComponent);

    // =========================================================================
    // 3. Bottom Bar Setup
    // =========================================================================
    configureBottomSlider(inputSlider, inputLabel, "INPUT", "x");
    configureBottomSlider(outputSlider, outputLabel, "OUTPUT", "x");
    inputAttachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "inputGain", inputSlider);
    outputAttachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "outputGain", outputSlider);

    inputSlider.setTextValueSuffix("");
    inputSlider.setNumDecimalPlacesToDisplay(2);
    inputSlider.textFromValueFunction = [](double val) { return juce::String(val, 2) + "x"; };
    inputSlider.updateText();

    outputSlider.setTextValueSuffix("");
    outputSlider.setNumDecimalPlacesToDisplay(2);
    outputSlider.textFromValueFunction = [](double val) { return juce::String(val, 2) + "x"; };
    outputSlider.updateText();

    advancedButton.setButtonText("ADVANCED DSP");
    advancedButton.setClickingTogglesState(true);
    advancedButton.onClick = [this] {
        setAdvancedVisible(advancedButton.getToggleState());
    };
    addAndMakeVisible(advancedButton);

    // =========================================================================
    // 4. Advanced Drawer Setup
    // =========================================================================
    advancedTitleLabel.setText("ADVANCED DSP PARAMETERS", juce::dontSendNotification);
    advancedTitleLabel.setJustificationType(juce::Justification::centredLeft);
    advancedTitleLabel.setColour(juce::Label::textColourId, primaryText());
    advancedTitleLabel.setFont(juce::Font(11.5f, juce::Font::bold));
    addAndMakeVisible(advancedTitleLabel);

    advancedHintLabel.setText("Edits switch mode to Custom automatically", juce::dontSendNotification);
    advancedHintLabel.setJustificationType(juce::Justification::centredRight);
    advancedHintLabel.setColour(juce::Label::textColourId, secondaryText());
    advancedHintLabel.setFont(juce::Font(10.5f));
    addAndMakeVisible(advancedHintLabel);

    for (size_t i = 0; i < advancedSliders.size(); ++i) {
        configureAdvancedSlider(advancedSliders[i], advancedLabels[i], kAdvancedSpecs[i].label);
        advancedAttachments[i] = std::make_unique<SliderAttachment>(
            audioProcessor.parameters, kAdvancedSpecs[i].id, advancedSliders[i]);

        const juce::String sfx = kAdvancedSpecs[i].suffix;
        advancedSliders[i].setTextValueSuffix("");
        advancedSliders[i].setNumDecimalPlacesToDisplay(2);
        advancedSliders[i].textFromValueFunction = [sfx](double val) {
            return juce::String(val, 2) + sfx;
        };
        advancedSliders[i].updateText();
    }

    setAdvancedVisible(false);

    startTimerHz(30);
}

Dimension5AudioProcessorEditor::~Dimension5AudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void Dimension5AudioProcessorEditor::configureHeaderButton(juce::TextButton& button) {
    button.setColour(juce::TextButton::buttonColourId, surfaceColour());
}

void Dimension5AudioProcessorEditor::configureBottomSlider(juce::Slider& slider, juce::Label& label,
                                                           const juce::String& labelText, const juce::String& suffix) {
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 16);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::trackColourId, analogAccent());
    addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, primaryText());
    label.setFont(juce::Font(10.5f, juce::Font::bold));
    addAndMakeVisible(label);
}

void Dimension5AudioProcessorEditor::configureAdvancedSlider(juce::Slider& slider, juce::Label& label,
                                                             const juce::String& labelText) {
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 16);
    slider.setColour(juce::Slider::trackColourId, analogAccent());
    addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, secondaryText());
    label.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(label);
}

void Dimension5AudioProcessorEditor::setAdvancedVisible(bool shouldBeVisible) {
    advancedVisible = shouldBeVisible;
    advancedTitleLabel.setVisible(advancedVisible);
    advancedHintLabel.setVisible(advancedVisible);
    for (size_t i = 0; i < advancedSliders.size(); ++i) {
        advancedLabels[i].setVisible(advancedVisible);
        advancedSliders[i].setVisible(advancedVisible);
    }
    setSize(getWidth(), advancedVisible ? kExpandedHeight : kBaseHeight);
    resized();
    repaint();
}

void Dimension5AudioProcessorEditor::updateCompareButtons() {
    const bool aActive = audioProcessor.getActiveCompareSlot() == 0;
    compareAButton.setToggleState(aActive, juce::dontSendNotification);
    compareBButton.setToggleState(!aActive, juce::dontSendNotification);
}

void Dimension5AudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(bgColour());

    // Top Header separator
    g.setColour(dividerColour());
    g.drawLine((float)kMargin, (float)kHeaderHeight, (float)getWidth() - (float)kMargin, (float)kHeaderHeight, 1.0f);

    // Bottom Bar separator
    const int bottomY = getHeight() - (advancedVisible ? (kExpandedHeight - kBaseHeight + kBottomBarHeight) : kBottomBarHeight);
    g.setColour(dividerColour());
    g.drawLine((float)kMargin, (float)bottomY, (float)getWidth() - (float)kMargin, (float)bottomY, 1.0f);

    // Draw Input and Output Stereo Peak Meters
    if (!inMeterBounds.isEmpty()) {
        drawStereoMeter(g, inMeterBounds, inMeterLeft, inMeterRight, "IN");
    }
    if (!outMeterBounds.isEmpty()) {
        drawStereoMeter(g, outMeterBounds, outMeterLeft, outMeterRight, "OUT");
    }

    // Advanced Drawer Card Background
    if (advancedVisible) {
        auto advArea = juce::Rectangle<int>(kMargin, bottomY + kBottomBarHeight + 6,
                                           getWidth() - 2 * kMargin,
                                           kExpandedHeight - kBaseHeight - 12).toFloat();
        g.setColour(surfaceColour());
        g.fillRoundedRectangle(advArea, 6.0f);
        g.setColour(dividerColour());
        g.drawRoundedRectangle(advArea, 6.0f, 1.0f);
    }
}

void Dimension5AudioProcessorEditor::drawStereoMeter(juce::Graphics& g, juce::Rectangle<int> bounds,
                                                     float leftVal, float rightVal, const juce::String& label) {
    auto labelArea = bounds.removeFromLeft(22);
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.setColour(secondaryText());
    g.drawText(label, labelArea, juce::Justification::centredLeft);

    const int barH = (bounds.getHeight() - 2) / 2;
    auto lRect = bounds.removeFromTop(barH);
    bounds.removeFromTop(2);
    auto rRect = bounds.removeFromTop(barH);

    auto drawSingleBar = [&](juce::Rectangle<int> barBounds, float val) {
        g.setColour(surfaceColour());
        g.fillRect(barBounds);
        const int fillW = juce::roundToInt((float)barBounds.getWidth() * juce::jlimit(0.0f, 1.0f, val));
        if (fillW > 0) {
            auto fillBounds = barBounds.withWidth(fillW);
            g.setColour(val > 0.95f ? warningAccent() : (val > 0.75f ? analogAccent() : spatialAccent()));
            g.fillRect(fillBounds);
        }
        g.setColour(dividerColour());
        g.drawRect(barBounds, 1);
    };

    drawSingleBar(lRect, leftVal);
    drawSingleBar(rRect, rightVal);
}

void Dimension5AudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(kMargin);

    // =========================================================================
    // 1. Header Layout
    // =========================================================================
    auto headerArea = area.removeFromTop(kHeaderHeight - 8);

    // Left Branding
    auto brandArea = headerArea.removeFromLeft(220);
    titleLabel.setBounds(brandArea.removeFromTop(24));
    subtitleLabel.setBounds(brandArea.removeFromTop(16));

    // Right Action Buttons
    auto rightHeaderArea = headerArea.removeFromRight(190);
    bypassButton.setBounds(rightHeaderArea.removeFromRight(74).withHeight(28));
    rightHeaderArea.removeFromRight(8);
    compareCopyButton.setBounds(rightHeaderArea.removeFromRight(44).withHeight(28));
    rightHeaderArea.removeFromRight(4);
    compareBButton.setBounds(rightHeaderArea.removeFromRight(26).withHeight(28));
    rightHeaderArea.removeFromRight(3);
    compareAButton.setBounds(rightHeaderArea.removeFromRight(26).withHeight(28));

    // Center Selectors: Preset (wide ~200px) and Mode (~115px)
    headerArea.removeFromLeft(14);
    headerArea.removeFromRight(14);
    const int comboH = 28;
    const int modeW = 115;
    const int presetW = juce::jmin(220, headerArea.getWidth() - modeW - 8);

    presetBox.setBounds(headerArea.removeFromLeft(presetW).withHeight(comboH));
    headerArea.removeFromLeft(8);
    modeBox.setBounds(headerArea.removeFromLeft(modeW).withHeight(comboH));

    area.removeFromTop(10);

    // =========================================================================
    // 2. Bottom Bar Layout
    // =========================================================================
    const int advHeight = advancedVisible ? (kExpandedHeight - kBaseHeight) : 0;
    auto bottomArea = area.removeFromBottom(kBottomBarHeight + advHeight);
    auto barOnly = bottomArea.removeFromTop(kBottomBarHeight);

    // Advanced toggle button on right
    advancedButton.setBounds(barOnly.removeFromRight(120).reduced(0, 12));
    barOnly.removeFromRight(20);

    // Output section (Label + Slider + Meter)
    auto outSection = barOnly.removeFromRight(260);
    outputLabel.setBounds(outSection.removeFromLeft(48).reduced(0, 14));
    outputSlider.setBounds(outSection.removeFromLeft(82).reduced(0, 10));
    outSection.removeFromLeft(8);
    outMeterBounds = outSection.removeFromLeft(115).reduced(0, 15);

    barOnly.removeFromRight(20);

    // Input section (Label + Slider + Meter)
    auto inSection = barOnly.removeFromRight(260);
    inputLabel.setBounds(inSection.removeFromLeft(44).reduced(0, 14));
    inputSlider.setBounds(inSection.removeFromLeft(82).reduced(0, 10));
    inSection.removeFromLeft(8);
    inMeterBounds = inSection.removeFromLeft(115).reduced(0, 15);

    // =========================================================================
    // 3. Advanced Drawer Layout
    // =========================================================================
    if (advancedVisible) {
        auto advArea = bottomArea.reduced(10, 8);
        auto advHeader = advArea.removeFromTop(20);
        advancedTitleLabel.setBounds(advHeader.removeFromLeft(250));
        advancedHintLabel.setBounds(advHeader);

        advArea.removeFromTop(6);
        const int numSliders = (int)advancedSliders.size();
        const int colW = advArea.getWidth() / 2 - 16;
        const int rowH = 26;

        for (int i = 0; i < numSliders; ++i) {
            juce::Rectangle<int> row;
            if (i < 4) {
                row = juce::Rectangle<int>(advArea.getX(), advArea.getY() + i * (rowH + 4), colW, rowH);
            } else {
                const int rightIdx = i - 4;
                row = juce::Rectangle<int>(advArea.getX() + colW + 32, advArea.getY() + rightIdx * (rowH + 4), colW, rowH);
            }
            advancedLabels[(size_t)i].setBounds(row.removeFromLeft(100));
            advancedSliders[(size_t)i].setBounds(row);
        }
    }

    // =========================================================================
    // 4. Central Area: Stereo Field Monitor (64%) & Spatial Controls (36%)
    // =========================================================================
    area.removeFromBottom(8);
    const int totalW = area.getWidth();
    const int controlsW = juce::jlimit(280, 360, (int)((float)totalW * 0.35f));
    const int gap = 12;

    auto controlsArea = area.removeFromRight(controlsW);
    area.removeFromRight(gap);
    auto monitorArea = area;

    stereoFieldComponent.setBounds(monitorArea);
    spatialControlsComponent.setBounds(controlsArea);
}

void Dimension5AudioProcessorEditor::timerCallback() {
    // 1. Consume stereo telemetry
    StereoTelemetrySnapshot teleSnapshot;
    if (audioProcessor.getStereoTelemetry(teleSnapshot)) {
        stereoFieldComponent.updateTelemetry(teleSnapshot);
    }

    // 2. Synchronize auxiliary parameter geometry
    stereoFieldComponent.updateParameterGeometry(
        audioProcessor.getFloatParam("width"),
        audioProcessor.getFloatParam("depthMs"),
        audioProcessor.getFloatParam("rateHz"),
        audioProcessor.parameters.getRawParameterValue("mix")->load()
    );

    // 3. Synchronize bypass visual state
    const bool isBypassed = (audioProcessor.parameters.getRawParameterValue("bypass")->load() > 0.5f);
    stereoFieldComponent.setBypassState(isBypassed);

    // 4. Smooth peak meters for presentation
    const float decay = 0.82f;
    inMeterLeft  = juce::jmax(audioProcessor.getInputMeterLeft(), inMeterLeft * decay);
    inMeterRight = juce::jmax(audioProcessor.getInputMeterRight(), inMeterRight * decay);
    outMeterLeft = juce::jmax(audioProcessor.getOutputMeterLeft(), outMeterLeft * decay);
    outMeterRight = juce::jmax(audioProcessor.getOutputMeterRight(), outMeterRight * decay);

    // 5. Preset selector sync with host program state
    const int curProg = audioProcessor.getCurrentProgram();
    if (presetBox.getSelectedItemIndex() != curProg) {
        presetBox.setSelectedItemIndex(curProg, juce::dontSendNotification);
    }

    updateCompareButtons();
    repaint();
}
