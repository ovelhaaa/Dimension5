#include "PluginEditor.h"

namespace {
juce::Colour bgColour()         { return juce::Colour(0x10, 0x17, 0x1C); }
juce::Colour surfaceColour()    { return juce::Colour(0x1B, 0x27, 0x2D); }
juce::Colour dividerColour()    { return juce::Colour(0x34, 0x44, 0x4B); }
juce::Colour primaryText()      { return juce::Colour(0xE8, 0xEF, 0xEC); }
juce::Colour secondaryText()    { return juce::Colour(0x89, 0x9D, 0xA3); }
juce::Colour spatialAccent()    { return juce::Colour(0x65, 0xDF, 0xC8); }
juce::Colour analogAccent()     { return juce::Colour(0xE2, 0xA6, 0x5A); }
juce::Colour warningAccent()    { return juce::Colour(0xE2, 0x78, 0x65); }

constexpr int kMargin = 16;
constexpr int kHeaderHeight = 64;
constexpr int kBottomBarHeight = 56;
constexpr int kBaseWidth = 850;
constexpr int kBaseHeight = 540;
constexpr int kExpandedHeight = 700;

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

// ==============================================================================
// Dimension5LookAndFeel Implementation
// ==============================================================================
Dimension5LookAndFeel::Dimension5LookAndFeel() {
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxTextColourId, primaryText());
    setColour(juce::ComboBox::backgroundColourId, surfaceColour());
    setColour(juce::ComboBox::outlineColourId, dividerColour());
    setColour(juce::ComboBox::textColourId, primaryText());
    setColour(juce::ComboBox::arrowColourId, secondaryText());
    setColour(juce::PopupMenu::backgroundColourId, bgColour());
    setColour(juce::PopupMenu::textColourId, primaryText());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, surfaceColour());
    setColour(juce::PopupMenu::highlightedTextColourId, spatialAccent());
}

void Dimension5LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPosProportional, float rotaryStartAngle,
                                             float rotaryEndAngle, juce::Slider& slider) {
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.48f;
    const auto center = bounds.getCentre();

    const auto arcColour = slider.findColour(juce::Slider::rotarySliderFillColourId);

    // Outer background circular body
    g.setColour(surfaceColour());
    g.fillEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour(dividerColour());
    g.drawEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f, 1.2f);

    // Track background arc
    const float trackRadius = radius - 4.5f;
    juce::Path backgroundArc;
    backgroundArc.addCentredArc(center.x, center.y, trackRadius, trackRadius, 0.0f,
                                rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(dividerColour().withAlpha(0.6f));
    g.strokePath(backgroundArc, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value Arc
    const float currentAngle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    if (currentAngle > rotaryStartAngle + 0.01f) {
        juce::Path valueArc;
        valueArc.addCentredArc(center.x, center.y, trackRadius, trackRadius, 0.0f,
                               rotaryStartAngle, currentAngle, true);
        g.setColour(arcColour);
        g.strokePath(valueArc, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Pointer needle
    const float pointerLength = radius * 0.65f;
    const float pointerAngle = currentAngle - juce::MathConstants<float>::halfPi;
    const float px = center.x + std::cos(pointerAngle) * pointerLength;
    const float py = center.y + std::sin(pointerAngle) * pointerLength;

    g.setColour(arcColour);
    g.drawLine(center.x, center.y, px, py, 2.0f);

    // Center pivot dot
    g.setColour(primaryText());
    g.fillEllipse(center.x - 2.5f, center.y - 2.5f, 5.0f, 5.0f);
}

void Dimension5LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float minSliderPos, float maxSliderPos,
                                             const juce::Slider::SliderStyle style, juce::Slider& slider) {
    juce::ignoreUnused(minSliderPos, maxSliderPos, style);
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float trackY = bounds.getCentreY();
    const float trackHeight = 4.0f;
    const auto trackBounds = juce::Rectangle<float>(bounds.getX(), trackY - trackHeight * 0.5f, bounds.getWidth(), trackHeight);

    // Background track
    g.setColour(surfaceColour());
    g.fillRoundedRectangle(trackBounds, 2.0f);
    g.setColour(dividerColour());
    g.drawRoundedRectangle(trackBounds, 2.0f, 0.75f);

    // Active track fill
    const auto fillColour = slider.findColour(juce::Slider::trackColourId);
    auto fillBounds = trackBounds.withWidth(juce::jmax(0.0f, sliderPos - bounds.getX()));
    g.setColour(fillColour);
    g.fillRoundedRectangle(fillBounds, 2.0f);

    // Thumb
    const float thumbRadius = 6.5f;
    g.setColour(primaryText());
    g.fillEllipse(sliderPos - thumbRadius, trackY - thumbRadius, thumbRadius * 2.0f, thumbRadius * 2.0f);
    g.setColour(dividerColour());
    g.drawEllipse(sliderPos - thumbRadius, trackY - thumbRadius, thumbRadius * 2.0f, thumbRadius * 2.0f, 1.0f);
}

void Dimension5LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                 const juce::Colour& backgroundColour,
                                                 bool shouldDrawButtonAsHighlighted,
                                                 bool shouldDrawButtonAsDown) {
    juce::ignoreUnused(backgroundColour);
    auto bounds = button.getLocalBounds().toFloat();
    const bool isToggled = button.getToggleState();

    juce::Colour fill = isToggled ? bgColour() : (shouldDrawButtonAsHighlighted ? surfaceColour().brighter(0.08f) : surfaceColour());
    if (shouldDrawButtonAsDown) fill = bgColour();

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 4.0f);

    juce::Colour border = dividerColour();
    if (isToggled) {
        border = (button.getButtonText() == "BYPASS") ? warningAccent() : spatialAccent();
    } else if (shouldDrawButtonAsHighlighted) {
        border = secondaryText();
    }

    g.setColour(border);
    g.drawRoundedRectangle(bounds, 4.0f, isToggled ? 1.5f : 1.0f);
}

void Dimension5LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                           bool shouldDrawButtonAsHighlighted,
                                           bool shouldDrawButtonAsDown) {
    juce::ignoreUnused(shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
    const bool isToggled = button.getToggleState();

    juce::Colour textCol = isToggled ? ((button.getButtonText() == "BYPASS") ? warningAccent() : spatialAccent())
                                     : secondaryText();

    g.setFont(juce::Font(11.5f, juce::Font::bold));
    g.setColour(textCol);
    g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}

void Dimension5LookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                         int buttonX, int buttonY, int buttonW, int buttonH,
                                         juce::ComboBox& box) {
    juce::ignoreUnused(isButtonDown, buttonX, buttonY, buttonW, buttonH);
    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();

    g.setColour(surfaceColour());
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(dividerColour());
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    // Arrow icon
    const float arrowX = (float)width - 16.0f;
    const float arrowY = (float)height * 0.5f - 2.0f;
    juce::Path arrow;
    arrow.startNewSubPath(arrowX - 4.0f, arrowY);
    arrow.lineTo(arrowX + 4.0f, arrowY);
    arrow.lineTo(arrowX, arrowY + 4.5f);
    arrow.closeSubPath();

    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.fillPath(arrow);
}

void Dimension5LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
    g.setColour(bgColour());
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(dividerColour());
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
}

// ==============================================================================
// Dimension5AudioProcessorEditor Implementation
// ==============================================================================
Dimension5AudioProcessorEditor::Dimension5AudioProcessorEditor(Dimension5AudioProcessor& ownerProcessor)
    : AudioProcessorEditor(&ownerProcessor), audioProcessor(ownerProcessor) {
    setLookAndFeel(&lnf);
    setSize(kBaseWidth, kBaseHeight);
    setResizable(true, true);
    setResizeLimits(780, 500, 1400, 950);

    // --------------------------------------------------------------------------
    // Header setup
    // --------------------------------------------------------------------------
    titleLabel.setText("DIMENSION5", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, primaryText());
    titleLabel.setFont(juce::Font(22.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("BBD STEREO ENHANCER", juce::dontSendNotification);
    subtitleLabel.setJustificationType(juce::Justification::centredLeft);
    subtitleLabel.setColour(juce::Label::textColourId, spatialAccent());
    subtitleLabel.setFont(juce::Font(10.5f, juce::Font::bold));
    addAndMakeVisible(subtitleLabel);

    taglineLabel.setText("Analog-inspired spatial processing", juce::dontSendNotification);
    taglineLabel.setJustificationType(juce::Justification::centredLeft);
    taglineLabel.setColour(juce::Label::textColourId, secondaryText());
    taglineLabel.setFont(juce::Font(10.5f));
    addAndMakeVisible(taglineLabel);

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
    configureSmallButton(compareAButton);
    addAndMakeVisible(compareAButton);

    compareBButton.setButtonText("B");
    compareBButton.setClickingTogglesState(false);
    compareBButton.onClick = [this] {
        audioProcessor.selectCompareSlot(1);
        updateCompareButtons();
    };
    configureSmallButton(compareBButton);
    addAndMakeVisible(compareBButton);

    compareCopyButton.setButtonText("COPY");
    compareCopyButton.onClick = [this] {
        audioProcessor.copyCompareToOtherSlot();
    };
    configureSmallButton(compareCopyButton);
    addAndMakeVisible(compareCopyButton);
    updateCompareButtons();

    bypassButton.setButtonText("BYPASS");
    bypassButton.setClickingTogglesState(true);
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "bypass", bypassButton);

    // --------------------------------------------------------------------------
    // Stereo Field Monitor
    // --------------------------------------------------------------------------
    addAndMakeVisible(stereoFieldComponent);

    // --------------------------------------------------------------------------
    // Main Controls Setup
    // --------------------------------------------------------------------------
    configureRotaryKnob(spreadSlider, spreadLabel, "SPREAD", "", true);
    configureRotaryKnob(mixSlider, mixLabel, "MIX", "%", true);
    configureRotaryKnob(depthSlider, depthLabel, "DEPTH", " ms", false);
    configureRotaryKnob(motionSlider, motionLabel, "MOTION", " Hz", false);
    configureRotaryKnob(colorSlider, colorLabel, "COLOR", " Hz", false);

    spreadAttachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "width", spreadSlider);
    mixAttachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "mix", mixSlider);
    depthAttachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "depthMs", depthSlider);
    motionAttachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "rateHz", motionSlider);
    colorAttachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "lpfHz", colorSlider);

    // --------------------------------------------------------------------------
    // Bottom Bar Controls Setup
    // --------------------------------------------------------------------------
    configureBottomSlider(inputSlider, inputLabel, "INPUT", "x");
    configureBottomSlider(outputSlider, outputLabel, "OUTPUT", "x");
    inputAttachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "inputGain", inputSlider);
    outputAttachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "outputGain", outputSlider);

    advancedButton.setButtonText("ADVANCED DSP");
    advancedButton.setClickingTogglesState(true);
    advancedButton.onClick = [this] {
        setAdvancedVisible(advancedButton.getToggleState());
    };
    addAndMakeVisible(advancedButton);

    // --------------------------------------------------------------------------
    // Advanced Panel Controls Setup
    // --------------------------------------------------------------------------
    advancedTitleLabel.setText("ADVANCED DSP PARAMETERS", juce::dontSendNotification);
    advancedTitleLabel.setJustificationType(juce::Justification::centredLeft);
    advancedTitleLabel.setColour(juce::Label::textColourId, primaryText());
    advancedTitleLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(advancedTitleLabel);

    advancedHintLabel.setText("Edits switch mode to Custom automatically", juce::dontSendNotification);
    advancedHintLabel.setJustificationType(juce::Justification::centredRight);
    advancedHintLabel.setColour(juce::Label::textColourId, secondaryText());
    advancedHintLabel.setFont(juce::Font(11.0f));
    addAndMakeVisible(advancedHintLabel);

    for (size_t i = 0; i < advancedSliders.size(); ++i) {
        configureAdvancedSlider(advancedSliders[i], advancedLabels[i], kAdvancedSpecs[i].label);
        advancedAttachments[i] = std::make_unique<SliderAttachment>(
            audioProcessor.parameters, kAdvancedSpecs[i].id, advancedSliders[i]);
    }

    setAdvancedVisible(false);

    startTimerHz(30);
}

Dimension5AudioProcessorEditor::~Dimension5AudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void Dimension5AudioProcessorEditor::configureRotaryKnob(juce::Slider& slider, juce::Label& label,
                                                         const juce::String& labelText, const juce::String& suffix,
                                                         bool isSpatial) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 68, 16);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::rotarySliderFillColourId, isSpatial ? spatialAccent() : analogAccent());
    addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, isSpatial ? spatialAccent() : analogAccent());
    label.setFont(juce::Font(11.0f, juce::Font::bold));
    addAndMakeVisible(label);
}

void Dimension5AudioProcessorEditor::configureBottomSlider(juce::Slider& slider, juce::Label& label,
                                                           const juce::String& labelText, const juce::String& suffix) {
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 16);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::trackColourId, analogAccent());
    addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, primaryText());
    label.setFont(juce::Font(11.0f, juce::Font::bold));
    addAndMakeVisible(label);
}

void Dimension5AudioProcessorEditor::configureAdvancedSlider(juce::Slider& slider, juce::Label& label,
                                                             const juce::String& labelText) {
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 16);
    slider.setColour(juce::Slider::trackColourId, analogAccent());
    addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, secondaryText());
    label.setFont(juce::Font(10.5f, juce::Font::bold));
    addAndMakeVisible(label);
}

void Dimension5AudioProcessorEditor::configureSmallButton(juce::TextButton& button) {
    button.setColour(juce::TextButton::buttonColourId, surfaceColour());
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

    // Main header frame divider
    g.setColour(dividerColour());
    g.drawLine((float)kMargin, (float)kHeaderHeight, (float)getWidth() - (float)kMargin, (float)kHeaderHeight, 1.0f);

    // Bottom bar frame divider
    const int bottomY = getHeight() - (advancedVisible ? (kExpandedHeight - kBaseHeight + kBottomBarHeight) : kBottomBarHeight);
    g.setColour(dividerColour());
    g.drawLine((float)kMargin, (float)bottomY, (float)getWidth() - (float)kMargin, (float)bottomY, 1.0f);

    // Draw Input and Output Peak Meters
    auto bottomArea = juce::Rectangle<int>(kMargin, bottomY, getWidth() - 2 * kMargin, kBottomBarHeight);
    const int meterWidth = 90;
    const int meterHeight = 14;

    auto inMeterRect = juce::Rectangle<int>(inputSlider.getRight() + 8, bottomY + 21, meterWidth, meterHeight);
    drawStereoMeter(g, inMeterRect, inMeterLeft, inMeterRight, "IN");

    auto outMeterRect = juce::Rectangle<int>(outputSlider.getRight() + 8, bottomY + 21, meterWidth, meterHeight);
    drawStereoMeter(g, outMeterRect, outMeterLeft, outMeterRight, "OUT");

    // Advanced Section Background
    if (advancedVisible) {
        auto advArea = juce::Rectangle<int>(kMargin, bottomY + kBottomBarHeight + 6, getWidth() - 2 * kMargin, kExpandedHeight - kBaseHeight - 12).toFloat();
        g.setColour(surfaceColour());
        g.fillRoundedRectangle(advArea, 6.0f);
        g.setColour(dividerColour());
        g.drawRoundedRectangle(advArea, 6.0f, 1.0f);
    }
}

void Dimension5AudioProcessorEditor::drawStereoMeter(juce::Graphics& g, juce::Rectangle<int> bounds,
                                                     float leftVal, float rightVal, const juce::String& label) {
    auto labelArea = bounds.removeFromLeft(22);
    g.setFont(juce::Font(9.5f, juce::Font::bold));
    g.setColour(secondaryText());
    g.drawText(label, labelArea, juce::Justification::centredLeft);

    const int barH = (bounds.getHeight() - 2) / 2;
    auto lRect = bounds.removeFromTop(barH);
    bounds.removeFromTop(2);
    auto rRect = bounds.removeFromTop(barH);

    auto drawSingleBar = [&](juce::Rectangle<int> barBounds, float val) {
        g.setColour(bgColour());
        g.fillRect(barBounds);
        const int fillW = juce::roundToInt((float)barBounds.getWidth() * juce::jlimit(0.0f, 1.0f, val));
        if (fillW > 0) {
            auto fillBounds = barBounds.withWidth(fillW);
            g.setColour(val > 0.95f ? warningAccent() : analogAccent());
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
    // Header Layout
    // =========================================================================
    auto headerArea = area.removeFromTop(kHeaderHeight - 8);

    auto brandArea = headerArea.removeFromLeft(250);
    titleLabel.setBounds(brandArea.removeFromTop(24));
    subtitleLabel.setBounds(brandArea.removeFromTop(16));
    taglineLabel.setBounds(brandArea.removeFromTop(15));

    auto rightHeaderArea = headerArea.removeFromRight(330);
    bypassButton.setBounds(rightHeaderArea.removeFromRight(82).withHeight(28));
    rightHeaderArea.removeFromRight(10);
    compareCopyButton.setBounds(rightHeaderArea.removeFromRight(48).withHeight(28));
    rightHeaderArea.removeFromRight(6);
    compareBButton.setBounds(rightHeaderArea.removeFromRight(28).withHeight(28));
    rightHeaderArea.removeFromRight(4);
    compareAButton.setBounds(rightHeaderArea.removeFromRight(28).withHeight(28));

    // Preset & Mode selector in remaining header space
    headerArea.removeFromLeft(12);
    headerArea.removeFromRight(12);
    const int comboHeight = 28;
    auto comboCol1 = headerArea.removeFromLeft(headerArea.getWidth() / 2 - 4);
    headerArea.removeFromLeft(8);
    auto comboCol2 = headerArea;
    presetBox.setBounds(comboCol1.withHeight(comboHeight));
    modeBox.setBounds(comboCol2.withHeight(comboHeight));

    area.removeFromTop(12);

    // =========================================================================
    // Bottom Bar Layout
    // =========================================================================
    const int advHeight = advancedVisible ? (kExpandedHeight - kBaseHeight) : 0;
    auto bottomArea = area.removeFromBottom(kBottomBarHeight + advHeight);
    auto barOnly = bottomArea.removeFromTop(kBottomBarHeight);

    // Advanced toggle button on the right
    advancedButton.setBounds(barOnly.removeFromRight(120).reduced(0, 14));
    barOnly.removeFromRight(20);

    // Output section
    auto outSection = barOnly.removeFromRight(220);
    outputLabel.setBounds(outSection.removeFromLeft(54).reduced(0, 16));
    outputSlider.setBounds(outSection.removeFromLeft(80).reduced(0, 12));

    barOnly.removeFromRight(20);

    // Input section
    auto inSection = barOnly.removeFromRight(220);
    inputLabel.setBounds(inSection.removeFromLeft(46).reduced(0, 16));
    inputSlider.setBounds(inSection.removeFromLeft(80).reduced(0, 12));

    // =========================================================================
    // Advanced Panel Layout
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
    // Center Area: Stereo Field Monitor (Left) & Main Controls (Right)
    // =========================================================================
    const int controlsWidth = juce::jlimit(260, 310, (int)((float)area.getWidth() * 0.36f));
    auto controlsArea = area.removeFromRight(controlsWidth);
    area.removeFromRight(14); // Gap between monitor and knobs
    stereoFieldComponent.setBounds(area);

    // Arrange 5 knobs inside controlsArea:
    // Top row: SPREAD (prominent wide) & MIX
    // Bottom row: DEPTH, MOTION, COLOR
    const int topRowHeight = controlsArea.getHeight() * 0.52f;
    auto topRow = controlsArea.removeFromTop(topRowHeight);
    controlsArea.removeFromTop(8);
    auto bottomRow = controlsArea;

    // Top Row: SPREAD and MIX
    const int topKnobW = topRow.getWidth() / 2;
    auto spreadBox = topRow.removeFromLeft(topKnobW).reduced(6, 4);
    auto mixBox = topRow.reduced(6, 4);

    spreadLabel.setBounds(spreadBox.removeFromTop(18));
    spreadSlider.setBounds(spreadBox);

    mixLabel.setBounds(mixBox.removeFromTop(18));
    mixSlider.setBounds(mixBox);

    // Bottom Row: DEPTH, MOTION, COLOR
    const int botKnobW = bottomRow.getWidth() / 3;
    auto depthBox = bottomRow.removeFromLeft(botKnobW).reduced(4, 2);
    auto motionBox = bottomRow.removeFromLeft(botKnobW).reduced(4, 2);
    auto colorBox = bottomRow.reduced(4, 2);

    depthLabel.setBounds(depthBox.removeFromTop(16));
    depthSlider.setBounds(depthBox);

    motionLabel.setBounds(motionBox.removeFromTop(16));
    motionSlider.setBounds(motionBox);

    colorLabel.setBounds(colorBox.removeFromTop(16));
    colorSlider.setBounds(colorBox);
}

void Dimension5AudioProcessorEditor::timerCallback() {
    // Consume stereo field telemetry safely
    StereoTelemetrySnapshot teleSnapshot;
    if (audioProcessor.getStereoTelemetry(teleSnapshot)) {
        stereoFieldComponent.updateTelemetry(teleSnapshot);
    }

    // Synchronize auxiliary parameter geometry
    stereoFieldComponent.updateParameterGeometry(
        audioProcessor.getFloatParam("width"),
        audioProcessor.getFloatParam("depthMs"),
        audioProcessor.getFloatParam("rateHz"),
        audioProcessor.parameters.getRawParameterValue("mix")->load()
    );

    // Smooth peak meters for UI presentation
    const float decay = 0.82f;
    inMeterLeft = juce::jmax(audioProcessor.getInputMeterLeft(), inMeterLeft * decay);
    inMeterRight = juce::jmax(audioProcessor.getInputMeterRight(), inMeterRight * decay);
    outMeterLeft = juce::jmax(audioProcessor.getOutputMeterLeft(), outMeterLeft * decay);
    outMeterRight = juce::jmax(audioProcessor.getOutputMeterRight(), outMeterRight * decay);

    // Preset selector sync with host program state
    const int curProg = audioProcessor.getCurrentProgram();
    if (presetBox.getSelectedItemIndex() != curProg) {
        presetBox.setSelectedItemIndex(curProg, juce::dontSendNotification);
    }

    updateCompareButtons();
    repaint();
}
