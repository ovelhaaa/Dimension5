#include "PluginEditor.h"

#include <array>
#include <cmath>

namespace {
using namespace Dimension5UI;

constexpr int kMargin = 14;
constexpr int kHeaderHeight = 52;
constexpr int kBottomBarHeight = 90;
constexpr int kAdvancedPanelHeight = 200;
constexpr int kBaseWidth = 1000;
constexpr int kBaseHeight = 620;
constexpr int kMinWidth = Dimension5AudioProcessorEditor::kMinWidth;
constexpr int kMinHeight = Dimension5AudioProcessorEditor::kMinHeightClosed;
constexpr int kMinHeightClosed = Dimension5AudioProcessorEditor::kMinHeightClosed;
constexpr int kMinHeightOpen = Dimension5AudioProcessorEditor::kMinHeightOpen;
constexpr int kMaxWidth = Dimension5AudioProcessorEditor::kMaxWidth;
constexpr int kMaxHeight = Dimension5AudioProcessorEditor::kMaxHeight;
constexpr int kWatermarkSafeWidth = 126;

struct AdvancedSpec {
    const char* id;
    const char* label;
    const char* suffix;
};

constexpr AdvancedSpec kAdvancedSpecs[10] = {
    { "baseDelayMs",     "BASE DELAY",    " ms" },
    { "hpfHz",           "LOW FOCUS",     " Hz" },
    { "analogAmount",    "ANALOG",        "" },
    { "companderAmount", "COMPANDER",     "" },
    { "stereoWidth",     "STEREO WIDTH",  "x" },
    { "dryGain",         "DRY",           "x" },
    { "wetDirectGain",   "WET DIRECT",    "x" },
    { "wetCrossGain",    "WET CROSS",     "x" },
    { "bassFocusHz",     "BASS FOCUS",    " Hz" },
    { "bbdCharacter",    "BBD CHARACTER", "%" }
};

float meterToNormalised(float value) {
    if (!(value > 1.0e-3f)) {
        return 0.0f;
    }
    const float db = 20.0f * std::log10(value);
    return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
}
} // namespace

Dimension5AudioProcessorEditor::Dimension5AudioProcessorEditor(Dimension5AudioProcessor& ownerProcessor)
    : AudioProcessorEditor(&ownerProcessor), audioProcessor(ownerProcessor) {
    setLookAndFeel(&lnf);
    setSize(kBaseWidth, kBaseHeight);
    setResizable(true, true);
    setResizeLimits(kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);

    // =========================================================================
    // 1. Header Section Setup (Off-White Precision Chassis)
    // =========================================================================
    titleLabel.setText("DIMENSION5", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, textOnChassis());
    titleLabel.setFont(Dimension5LookAndFeel::makeTechnicalFont(20.0f, true));
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("BBD STEREO ENHANCER", juce::dontSendNotification);
    subtitleLabel.setJustificationType(juce::Justification::centredLeft);
    subtitleLabel.setColour(juce::Label::textColourId, textOnChassisSec());
    subtitleLabel.setFont(Dimension5LookAndFeel::makeTechnicalFont(9.5f, true));
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
    // 3. Bottom Bar Setup (Graphite I/O & Metering Bay)
    // =========================================================================
    configureBottomSlider(inputSlider, inputLabel, "INPUT", "x");
    configureBottomSlider(outputSlider, outputLabel, "OUTPUT", "x");
    inputAttachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "inputGain", inputSlider);
    outputAttachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "outputGain", outputSlider);

    inputSlider.setDoubleClickReturnValue(true, 1.0);
    inputSlider.setTextValueSuffix("");
    inputSlider.setNumDecimalPlacesToDisplay(2);
    inputSlider.textFromValueFunction = [](double val) { return juce::String(val, 2) + "x"; };
    inputSlider.updateText();

    outputSlider.setDoubleClickReturnValue(true, 1.0);
    outputSlider.setTextValueSuffix("");
    outputSlider.setNumDecimalPlacesToDisplay(2);
    outputSlider.textFromValueFunction = [](double val) { return juce::String(val, 2) + "x"; };
    outputSlider.updateText();

    auto setupDbLabel = [this](juce::Label& label) {
        label.setText("+0.0 dB", juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centredRight);
        label.setColour(juce::Label::textColourId, textSecondary());
        label.setFont(Dimension5LookAndFeel::makeTechnicalFont(9.5f, true));
        addAndMakeVisible(label);
    };
    setupDbLabel(inputDbLabel);
    setupDbLabel(outputDbLabel);

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
    advancedTitleLabel.setColour(juce::Label::textColourId, textOnPanel());
    advancedTitleLabel.setFont(Dimension5LookAndFeel::makeTechnicalFont(11.0f, true));
    addAndMakeVisible(advancedTitleLabel);

    advancedHintLabel.setText("EDITS SWITCH MODE TO CUSTOM AUTOMATICALLY", juce::dontSendNotification);
    advancedHintLabel.setJustificationType(juce::Justification::centredRight);
    advancedHintLabel.setColour(juce::Label::textColourId, textMuted());
    advancedHintLabel.setFont(Dimension5LookAndFeel::makeTechnicalFont(9.2f, false));
    addAndMakeVisible(advancedHintLabel);

    for (size_t i = 0; i < advancedSliders.size(); ++i) {
        configureAdvancedSlider(advancedSliders[i], advancedLabels[i], kAdvancedSpecs[i].label);
        advancedAttachments[i] = std::make_unique<SliderAttachment>(
            audioProcessor.parameters, kAdvancedSpecs[i].id, advancedSliders[i]);

        if (const auto* param = audioProcessor.parameters.getParameter(kAdvancedSpecs[i].id)) {
            advancedSliders[i].setDoubleClickReturnValue(
                true, (double)param->convertFrom0to1(param->getDefaultValue()));
        }

        const juce::String sfx = kAdvancedSpecs[i].suffix;
        advancedSliders[i].setTextValueSuffix("");
        advancedSliders[i].setNumDecimalPlacesToDisplay(2);
        const juce::String specId = kAdvancedSpecs[i].id;
        if (specId == "bassFocusHz") {
            advancedSliders[i].setNumDecimalPlacesToDisplay(0);
            advancedSliders[i].textFromValueFunction = [](double val) {
                if (val <= 0.5) return juce::String("OFF");
                return juce::String(juce::roundToInt(val)) + " Hz";
            };
            advancedSliders[i].valueFromTextFunction = [](const juce::String& text) {
                if (text.trim().equalsIgnoreCase("off")) return 0.0;
                return text.getDoubleValue();
            };
        } else if (specId == "bbdCharacter") {
            advancedSliders[i].setNumDecimalPlacesToDisplay(0);
            advancedSliders[i].textFromValueFunction = [](double val) {
                if (val <= 0.005) return juce::String("OFF");
                return juce::String(juce::roundToInt(val * 100.0)) + "%";
            };
            advancedSliders[i].valueFromTextFunction = [](const juce::String& text) {
                if (text.trim().equalsIgnoreCase("off")) return 0.0;
                auto t = text.trim();
                if (t.endsWithChar('%')) t = t.dropLastCharacters(1).trim();
                return t.getDoubleValue() * 0.01;
            };
        } else {
            advancedSliders[i].textFromValueFunction = [sfx](double val) {
                return juce::String(val, 2) + sfx;
            };
        }
        advancedSliders[i].updateText();
    }

    setAdvancedVisible(false);

    lastTimerMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(30);
}

Dimension5AudioProcessorEditor::~Dimension5AudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void Dimension5AudioProcessorEditor::configureHeaderButton(juce::TextButton& button) {
    button.setColour(juce::TextButton::buttonColourId, panelElevated());
}

void Dimension5AudioProcessorEditor::configureBottomSlider(juce::Slider& slider, juce::Label& label,
                                                           const juce::String& labelText, const juce::String& suffix) {
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 20);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::trackColourId, primaryAccent());
    addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, textOnPanel());
    label.setFont(Dimension5LookAndFeel::makeTechnicalFont(10.5f, true));
    addAndMakeVisible(label);
}

void Dimension5AudioProcessorEditor::configureAdvancedSlider(juce::Slider& slider, juce::Label& label,
                                                             const juce::String& labelText) {
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 74, 18);
    slider.setColour(juce::Slider::trackColourId, primaryAccent());
    addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, textSecondary());
    label.setFont(Dimension5LookAndFeel::makeTechnicalFont(9.5f, true));
    addAndMakeVisible(label);
}

void Dimension5AudioProcessorEditor::setAdvancedVisible(bool shouldBeVisible) {
    if (advancedVisible == shouldBeVisible) {
        advancedTitleLabel.setVisible(advancedVisible);
        advancedHintLabel.setVisible(advancedVisible);
        for (size_t i = 0; i < advancedSliders.size(); ++i) {
            advancedLabels[i].setVisible(advancedVisible);
            advancedSliders[i].setVisible(advancedVisible);
        }
        resized();
        repaint();
        return;
    }

    if (shouldBeVisible) {
        heightBeforeAdvanced = getHeight();
        advancedVisible = true;
        advancedTitleLabel.setVisible(true);
        advancedHintLabel.setVisible(true);
        for (size_t i = 0; i < advancedSliders.size(); ++i) {
            advancedLabels[i].setVisible(true);
            advancedSliders[i].setVisible(true);
        }
        const int targetHeight = juce::jlimit(kMinHeightOpen, kMaxHeight, heightBeforeAdvanced + kAdvancedPanelHeight);
        setResizeLimits(kMinWidth, kMinHeightOpen, kMaxWidth, kMaxHeight);
        setSize(getWidth(), targetHeight);
    } else {
        advancedVisible = false;
        advancedTitleLabel.setVisible(false);
        advancedHintLabel.setVisible(false);
        for (size_t i = 0; i < advancedSliders.size(); ++i) {
            advancedLabels[i].setVisible(false);
            advancedSliders[i].setVisible(false);
        }
        setResizeLimits(kMinWidth, kMinHeightClosed, kMaxWidth, kMaxHeight);
        const int fallback = getHeight() - kAdvancedPanelHeight;
        const int targetHeight = juce::jlimit(kMinHeightClosed, kMaxHeight,
                                              heightBeforeAdvanced > 0 ? heightBeforeAdvanced : fallback);
        setSize(getWidth(), targetHeight);
    }

    advancedButton.setToggleState(advancedVisible, juce::dontSendNotification);
    resized();
    repaint();
}

void Dimension5AudioProcessorEditor::updateCompareButtons() {
    const bool aActive = audioProcessor.getActiveCompareSlot() == 0;
    compareAButton.setToggleState(aActive, juce::dontSendNotification);
    compareBButton.setToggleState(!aActive, juce::dontSendNotification);
}

void Dimension5AudioProcessorEditor::paint(juce::Graphics& g) {
    using namespace Dimension5UI;

    const auto totalBounds = getLocalBounds();
    const bool isCompact = getHeight() < 680;
    const int headerH = isCompact ? 46 : kHeaderHeight;

    // 1. Warm Off-White Precision Instrument Chassis (#F0EDE6)
    g.fillAll(chassisBackground());
    g.setColour(chassisBorder());
    g.drawRect(totalBounds, 1);

    // 2. Header Divider & Registration Mark on Off-White Chassis
    g.setColour(chassisDivider());
    g.drawHorizontalLine(headerH, kMargin, getWidth() - kMargin);

    g.setColour(primaryAccent());
    g.fillRect((float)kMargin, (float)headerH - 1.5f, 26.0f, 2.0f);

    // 3. Bespoke 5-Bar Geometric Emblem for DIMENSION5 (distinct from Driftbrigade's 4-bar emblem)
    // Five vertical precision bars representing the 5th Dimension stereo field, with the 4th bar in orange.
    {
        const float emblemX = (float)kMargin;
        const float emblemCY = (float)headerH * 0.47f;
        constexpr std::array<float, 5> barHeights { 10.0f, 16.0f, 22.0f, 26.0f, 14.0f };
        for (size_t i = 0; i < 5; ++i) {
            const float bx = emblemX + (float)i * 4.4f;
            const float bh = isCompact ? (barHeights[i] * 0.86f) : barHeights[i];
            g.setColour(i == 3 ? primaryAccent() : textOnChassis());
            g.fillRoundedRectangle(bx, emblemCY - bh * 0.5f, 2.8f, bh, 1.0f);
        }

        // Small orange index square next to "BBD STEREO ENHANCER" subtitle
        const auto subBounds = subtitleLabel.getBounds().toFloat();
        if (!subBounds.isEmpty()) {
            g.setColour(primaryAccent());
            g.fillRect(subBounds.getX() - 7.5f, subBounds.getCentreY() - 2.2f, 4.4f, 4.4f);
        }
    }

    // 4. Recessed Graphite Housing around A / B / COPY Comparison Cluster in Header
    {
        const auto aBounds = compareAButton.getBounds();
        const auto copyBounds = compareCopyButton.getBounds();
        if (!aBounds.isEmpty() && !copyBounds.isEmpty()) {
            const auto clusterRect = aBounds.getUnion(copyBounds).toFloat().expanded(3.0f, 3.0f);
            g.setColour(panelBackground());
            g.fillRoundedRectangle(clusterRect, 4.5f);
            g.setColour(panelBorder());
            g.drawRoundedRectangle(clusterRect, 4.5f, 1.0f);
        }
    }

    // 5. Bottom Input / Output & Metering Module Bay (Graphite #292B2D over Off-White Chassis)
    if (!bottomBarBounds.isEmpty()) {
        // Keep the rightmost watermark safety zone on the off-white chassis clean
        const int moduleRight = std::max(bottomBarBounds.getX() + 320,
                                         advancedButton.getRight() + 12);
        const auto ioBay = juce::Rectangle<float>((float)bottomBarBounds.getX(),
                                                  (float)bottomBarBounds.getY() + 2.0f,
                                                  (float)(moduleRight - bottomBarBounds.getX()),
                                                  (float)bottomBarBounds.getHeight() - 4.0f);
        constexpr float corner = 5.5f;
        g.setColour(panelBackground());
        g.fillRoundedRectangle(ioBay, corner);
        g.setColour(panelBorder());
        g.drawRoundedRectangle(ioBay, corner, 1.0f);

        // Top orange index tick on the I/O module
        g.setColour(primaryAccent());
        g.fillRoundedRectangle(ioBay.getX() + 10.0f, ioBay.getY() + 8.0f, 3.0f, 10.0f, 1.0f);

        // Vertical dividers separating INPUT, OUTPUT, and ADVANCED toggle
        const float divTop = ioBay.getY() + 8.0f;
        const float divBot = ioBay.getBottom() - 8.0f;
        g.setColour(panelBorder().withAlpha(0.75f));

        if (!inMeterBounds.isEmpty() && !outputSlider.getBounds().isEmpty()) {
            const float midDivX = 0.5f * ((float)inMeterBounds.getRight() + (float)outputSlider.getX());
            g.drawVerticalLine((int)midDivX, divTop, divBot);
        }
        if (!outMeterBounds.isEmpty() && !advancedButton.getBounds().isEmpty()) {
            const float rightDivX = 0.5f * ((float)outMeterBounds.getRight() + (float)advancedButton.getX());
            g.drawVerticalLine((int)rightDivX, divTop, divBot);
        }
    }

    if (!inMeterBounds.isEmpty()) {
        drawStereoMeter(g, inMeterBounds, inMeterLeft, inMeterRight, inPeakHoldLeft, inPeakHoldRight);
    }
    if (!outMeterBounds.isEmpty()) {
        drawStereoMeter(g, outMeterBounds, outMeterLeft, outMeterRight, outPeakHoldLeft, outPeakHoldRight);
    }

    // 6. Expandable Advanced Drawer Module (Graphite #292B2D with Recessed Header #212325)
    if (advancedVisible && !advancedBounds.isEmpty()) {
        const auto advArea = advancedBounds.reduced(0, 3).toFloat();
        constexpr float corner = 5.5f;
        const float advHdrH = isCompact ? 22.0f : 26.0f;

        g.setColour(panelBackground());
        g.fillRoundedRectangle(advArea, corner);

        const juce::Rectangle<float> hdrRect(advArea.getX() + 1.0f, advArea.getY() + 1.0f,
                                             advArea.getWidth() - 2.0f, advHdrH);
        g.setColour(panelRecessed());
        g.fillRoundedRectangle(hdrRect, corner);
        g.fillRect(advArea.getX() + 1.0f, advArea.getY() + advHdrH - corner, advArea.getWidth() - 2.0f, corner);

        g.setColour(panelBorder());
        g.drawRoundedRectangle(advArea, corner, 1.0f);
        g.drawHorizontalLine((int)(advArea.getY() + advHdrH), advArea.getX() + 1.0f, advArea.getRight() - 1.0f);

        // Minimalist orange index bar in Advanced Drawer header
        g.setColour(primaryAccent());
        g.fillRoundedRectangle(advArea.getX() + 10.0f, advArea.getY() + (advHdrH - 10.0f) * 0.5f, 3.0f, 10.0f, 1.0f);

        // Central vertical divider between the two parameter columns
        g.setColour(panelBorder().withAlpha(0.75f));
        g.drawVerticalLine((int)advArea.getCentreX(), advArea.getY() + advHdrH + 6.0f, advArea.getBottom() - 6.0f);
    }
}

void Dimension5AudioProcessorEditor::drawStereoMeter(juce::Graphics& g, juce::Rectangle<int> bounds,
                                                     float leftVal, float rightVal,
                                                     float leftPeak, float rightPeak) {
    using namespace Dimension5UI;

    if (bounds.getWidth() < 24 || bounds.getHeight() < 12) {
        return;
    }

    const int labelW = 12;
    const int rowGap = 4;
    const int rowH = juce::jmax(6, (bounds.getHeight() - rowGap) / 2);

    auto drawRow = [&](juce::Rectangle<int> row, float value, float peak, const char* tag) {
        auto tagArea = row.removeFromLeft(labelW);
        g.setFont(Dimension5LookAndFeel::makeTechnicalFont(8.2f, true));
        g.setColour(textSecondary());
        g.drawText(tag, tagArea, juce::Justification::centredLeft);

        auto bar = row;
        g.setColour(displayBackground());
        g.fillRoundedRectangle(bar.toFloat(), 2.0f);
        g.setColour(panelBorder());
        g.drawRoundedRectangle(bar.toFloat(), 2.0f, 0.8f);

        // dB reference ticks at -24, -12 and -6 dBFS
        const float tickDbs[3] = { -24.0f, -12.0f, -6.0f };
        g.setColour(displayAxis().withAlpha(0.85f));
        for (float db : tickDbs) {
            const float norm = (db + 60.0f) / 60.0f;
            const float x = (float)bar.getX() + norm * (float)bar.getWidth();
            g.drawLine(x, (float)bar.getY() + 1.0f, x, (float)bar.getBottom() - 1.0f, 0.7f);
        }

        const float norm = meterToNormalised(value);
        const int fillW = juce::roundToInt((float)bar.getWidth() * norm);
        const float db = (value > 1.0e-3f) ? 20.0f * std::log10(value) : -60.0f;
        const float peakDb = (peak > 1.0e-3f) ? 20.0f * std::log10(peak) : -60.0f;
        const bool isClipping = (db > -0.2f || peakDb > -0.1f);

        if (fillW > 1) {
            auto fill = bar.withWidth(fillW).toFloat().reduced(1.0f, 1.0f);
            // Single chromatic family: primary orange for normal range, brighter intense orange near 0 dBFS
            const juce::Colour fillColour = isClipping ? accentHover()
                                          : (db > -6.0f) ? primaryAccent()
                                                         : primaryAccent().withAlpha(0.84f);
            g.setColour(fillColour);
            g.fillRoundedRectangle(fill, 1.5f);
        }

        const float peakNorm = meterToNormalised(peak);
        if (peakNorm > 0.001f) {
            const float px = (float)bar.getX() + peakNorm * (float)bar.getWidth();
            g.setColour(textOnPanel().withAlpha(0.92f));
            g.drawLine(px, (float)bar.getY() + 1.0f, px, (float)bar.getBottom() - 1.0f, 1.4f);
        }

        // Explicit clipping indicator by shape/text + intense orange highlight
        if (isClipping) {
            const auto clipCap = juce::Rectangle<float>((float)bar.getRight() - 4.0f, (float)bar.getY() + 1.0f,
                                                        3.0f, (float)bar.getHeight() - 2.0f);
            g.setColour(accentHover());
            g.fillRoundedRectangle(clipCap, 1.0f);
            g.drawRoundedRectangle(bar.toFloat(), 2.0f, 1.1f);
        }
    };

    auto rowL = bounds.removeFromTop(rowH);
    bounds.removeFromTop(rowGap);
    auto rowR = bounds.removeFromTop(rowH);

    drawRow(rowL, leftVal, leftPeak, "L");
    drawRow(rowR, rightVal, rightPeak, "R");
}

void Dimension5AudioProcessorEditor::layoutBottomSection(juce::Rectangle<int> section, juce::Slider& slider,
                                                         juce::Label& label, juce::Label& dbLabel,
                                                         juce::Rectangle<int>& meterBounds) {
    auto areaLocal = section.reduced(12, 8);

    auto labelRow = areaLocal.removeFromTop(15);
    label.setBounds(labelRow.removeFromLeft(juce::jlimit(52, 84, areaLocal.getWidth() / 3)));
    dbLabel.setBounds(labelRow);

    areaLocal.removeFromTop(2);

    const int meterW = juce::jlimit(76, 116, juce::roundToInt((float)areaLocal.getWidth() * 0.39f));
    meterBounds = areaLocal.removeFromRight(meterW).reduced(0, 4);

    areaLocal.removeFromRight(10);
    slider.setBounds(areaLocal.reduced(0, 1));
}

void Dimension5AudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(kMargin);
    const int totalH = getHeight();
    const bool isCompact = totalH < 680;

    // =========================================================================
    // 1. Header Layout (Off-White Chassis Header)
    // =========================================================================
    const int headerH = isCompact ? 46 : kHeaderHeight;
    auto headerArea = area.removeFromTop(headerH - 6);

    const int brandW = juce::jlimit(165, 240, juce::roundToInt((float)headerArea.getWidth() * 0.21f));
    auto brandArea = headerArea.removeFromLeft(brandW);
    brandArea.removeFromLeft(28); // Space for the 5-bar Dimension5 geometric emblem
    titleLabel.setFont(Dimension5LookAndFeel::makeTechnicalFont(isCompact ? 16.5f : 19.0f, true));
    titleLabel.setBounds(brandArea.removeFromTop(isCompact ? 20 : 23));
    auto subArea = brandArea.removeFromTop(isCompact ? 14 : 16);
    subArea.removeFromLeft(8); // Space for the small orange square index marker
    subtitleLabel.setBounds(subArea);

    const int btnH = isCompact ? 24 : 26;
    const int btnY = headerArea.getY() + (headerArea.getHeight() - btnH) / 2;
    auto rightHeaderArea = headerArea.removeFromRight(194);
    bypassButton.setBounds(rightHeaderArea.removeFromRight(74).withY(btnY).withHeight(btnH));
    rightHeaderArea.removeFromRight(8);
    compareCopyButton.setBounds(rightHeaderArea.removeFromRight(42).withY(btnY).withHeight(btnH));
    rightHeaderArea.removeFromRight(3);
    compareBButton.setBounds(rightHeaderArea.removeFromRight(26).withY(btnY).withHeight(btnH));
    rightHeaderArea.removeFromRight(3);
    compareAButton.setBounds(rightHeaderArea.removeFromRight(26).withY(btnY).withHeight(btnH));

    headerArea.removeFromLeft(10);
    headerArea.removeFromRight(10);
    const int comboH = btnH;
    const int modeW = juce::jlimit(92, 124, juce::roundToInt((float)headerArea.getWidth() * 0.27f));
    const int presetW = juce::jlimit(140, 236, headerArea.getWidth() - modeW - 8);

    presetBox.setBounds(headerArea.removeFromLeft(presetW).withY(btnY).withHeight(comboH));
    headerArea.removeFromLeft(8);
    modeBox.setBounds(headerArea.removeFromLeft(juce::jmin(modeW, headerArea.getWidth())).withY(btnY).withHeight(comboH));

    area.removeFromTop(isCompact ? 8 : 12);

    // =========================================================================
    // 2. Bottom Bar + Advanced Drawer
    // =========================================================================
    const int bottomH = isCompact ? 72 : kBottomBarHeight;
    const int advHeight = advancedVisible ? (isCompact ? 156 : kAdvancedPanelHeight) : 0;
    auto bottomArea = area.removeFromBottom(bottomH + advHeight);
    bottomBarBounds = bottomArea.removeFromTop(bottomH);
    advancedBounds = advancedVisible ? bottomArea : juce::Rectangle<int>();

    auto bar = bottomBarBounds;
    auto safeArea = bar.removeFromRight(juce::jmin(kWatermarkSafeWidth, juce::jmax(0, bar.getWidth() - 320)));
    juce::ignoreUnused(safeArea);

    bar.removeFromRight(10);
    const int advW = juce::jlimit(98, 126, bar.getWidth() / 6);
    const int advBtnH = isCompact ? 26 : 30;
    advancedButton.setBounds(bar.removeFromRight(advW).withSizeKeepingCentre(advW, advBtnH));
    bar.removeFromRight(10);

    const int sectionW = juce::jlimit(200, 360, bar.getWidth() / 2);

    auto inSection = bar.removeFromLeft(sectionW);
    auto outSection = bar;

    layoutBottomSection(inSection, inputSlider, inputLabel, inputDbLabel, inMeterBounds);
    layoutBottomSection(outSection, outputSlider, outputLabel, outputDbLabel, outMeterBounds);

    // =========================================================================
    // 3. Advanced Drawer Layout
    // =========================================================================
    if (advancedVisible && !advancedBounds.isEmpty()) {
        auto advArea = advancedBounds.reduced(16, isCompact ? 5 : 8);
        const int advHdrH = isCompact ? 18 : 22;
        auto advHeader = advArea.removeFromTop(advHdrH);
        advHeader.removeFromLeft(6); // Clear of orange index bar
        advancedTitleLabel.setBounds(advHeader.removeFromLeft(juce::jmin(260, advHeader.getWidth() / 2)));
        advancedHintLabel.setBounds(advHeader);

        advArea.removeFromTop(isCompact ? 4 : 8);
        const int numSliders = (int)advancedSliders.size();
        const int colGap = isCompact ? 24 : 34;
        const int colW = juce::jmax(150, (advArea.getWidth() - colGap) / 2);
        const int rowH = juce::jlimit(20, 30, (advArea.getHeight() - 12) / 5);
        const int labelW = juce::jlimit(82, 120, juce::roundToInt((float)colW * 0.35f));

        for (int i = 0; i < numSliders; ++i) {
            const int col = (i < 5) ? 0 : 1;
            const int rowIdx = (i < 5) ? i : (i - 5);
            juce::Rectangle<int> row(advArea.getX() + col * (colW + colGap),
                                     advArea.getY() + rowIdx * (rowH + (isCompact ? 2 : 4)),
                                     colW, rowH);
            advancedLabels[(size_t)i].setBounds(row.removeFromLeft(labelW));
            advancedSliders[(size_t)i].setBounds(row);
        }
    }

    // =========================================================================
    // 4. Central Area: Stereo Field Monitor + Spatial Controls
    // =========================================================================
    area.removeFromBottom(isCompact ? 6 : 10);
    const int totalW = area.getWidth();
    const int controlsW = juce::jlimit(280, 420, juce::roundToInt((float)totalW * 0.35f));
    const int gap = isCompact ? 10 : 12;

    auto controlsArea = area.removeFromRight(controlsW);
    area.removeFromRight(gap);
    auto monitorArea = area;

    stereoFieldComponent.setBounds(monitorArea);
    spatialControlsComponent.setBounds(controlsArea);
}

void Dimension5AudioProcessorEditor::timerCallback() {
    const double now = juce::Time::getMillisecondCounterHiRes();
    double dtSec = (lastTimerMs > 0.0) ? (now - lastTimerMs) * 0.001 : 0.033;
    lastTimerMs = now;
    dtSec = juce::jlimit(0.001, 0.25, dtSec);
    const float dt = static_cast<float>(dtSec);

    // 1. Consume stereo telemetry (real measured audio only)
    StereoTelemetrySnapshot teleSnapshot;
    if (audioProcessor.getStereoTelemetry(teleSnapshot)) {
        stereoFieldComponent.updateTelemetry(teleSnapshot);
    }

    // 2. Synchronize auxiliary parameter geometry
    stereoFieldComponent.updateParameterGeometry(
        audioProcessor.getFloatParam("width"),
        audioProcessor.getFloatParam("depthMs"),
        audioProcessor.getFloatParam("rateHz"),
        audioProcessor.parameters.getRawParameterValue("mix")->load());

    // 3. Synchronize bypass visual state
    const bool isBypassed = (audioProcessor.parameters.getRawParameterValue("bypass")->load() > 0.5f);
    stereoFieldComponent.setBypassState(isBypassed);

    // 4. Smooth meters and peak-hold markers (time based)
    const float meterRelease = std::exp(-dt / 0.18f);
    const float peakRelease = std::exp(-dt / 1.10f);

    const float rawInL = audioProcessor.getInputMeterLeft();
    const float rawInR = audioProcessor.getInputMeterRight();
    const float rawOutL = audioProcessor.getOutputMeterLeft();
    const float rawOutR = audioProcessor.getOutputMeterRight();

    inMeterLeft  = juce::jmax(rawInL, inMeterLeft * meterRelease);
    inMeterRight = juce::jmax(rawInR, inMeterRight * meterRelease);
    outMeterLeft  = juce::jmax(rawOutL, outMeterLeft * meterRelease);
    outMeterRight = juce::jmax(rawOutR, outMeterRight * meterRelease);

    inPeakHoldLeft  = juce::jmax(rawInL, inPeakHoldLeft * peakRelease);
    inPeakHoldRight = juce::jmax(rawInR, inPeakHoldRight * peakRelease);
    outPeakHoldLeft  = juce::jmax(rawOutL, outPeakHoldLeft * peakRelease);
    outPeakHoldRight = juce::jmax(rawOutR, outPeakHoldRight * peakRelease);

    // 5. Preset selector sync with host program state
    const int curProg = audioProcessor.getCurrentProgram();
    if (presetBox.getSelectedItemIndex() != curProg) {
        presetBox.setSelectedItemIndex(curProg, juce::dontSendNotification);
    }

    updateCompareButtons();

    // UI-only dB readout; internal gain values and parameter IDs are unchanged.
    auto formatGainDb = [](float gain) {
        if (gain <= 1.0e-3f) {
            return juce::String("OFF");
        }
        return juce::String::formatted("%+.1f dB", (double)(20.0f * std::log10(gain)));
    };
    const float inPeakMax = juce::jmax(inMeterLeft, inMeterRight, inPeakHoldLeft, inPeakHoldRight);
    const float outPeakMax = juce::jmax(outMeterLeft, outMeterRight, outPeakHoldLeft, outPeakHoldRight);
    const bool inClip = (inPeakMax > 0.985f);
    const bool outClip = (outPeakMax > 0.985f);

    const juce::String inDb = inClip ? "CLIP  " + formatGainDb(audioProcessor.getFloatParam("inputGain"))
                                     : formatGainDb(audioProcessor.getFloatParam("inputGain"));
    const juce::String outDb = outClip ? "CLIP  " + formatGainDb(audioProcessor.getFloatParam("outputGain"))
                                       : formatGainDb(audioProcessor.getFloatParam("outputGain"));
    if (inputDbLabel.getText() != inDb) {
        inputDbLabel.setText(inDb, juce::dontSendNotification);
        inputDbLabel.setColour(juce::Label::textColourId, inClip ? primaryAccent() : textSecondary());
    }
    if (outputDbLabel.getText() != outDb) {
        outputDbLabel.setText(outDb, juce::dontSendNotification);
        outputDbLabel.setColour(juce::Label::textColourId, outClip ? primaryAccent() : textSecondary());
    }

    // Repaint only the regions that actually change.
    stereoFieldComponent.repaint();
    if (!bottomBarBounds.isEmpty()) {
        repaint(bottomBarBounds);
    }
}
