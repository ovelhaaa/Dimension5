#include "PluginEditor.h"

#include <cmath>

namespace {
using namespace Dimension5UI;

constexpr int kMargin = 14;
constexpr int kHeaderHeight = 52;
constexpr int kBottomBarHeight = 90;
constexpr int kAdvancedPanelHeight = 200;
constexpr int kBaseWidth = 1000;
constexpr int kBaseHeight = 620;
constexpr int kMinWidth = 840;
constexpr int kMinHeight = 540;
constexpr int kMaxWidth = 1600;
constexpr int kMaxHeight = 1000;
constexpr int kWatermarkSafeWidth = 126;

struct AdvancedSpec {
    const char* id;
    const char* label;
    const char* suffix;
};

constexpr AdvancedSpec kAdvancedSpecs[9] = {
    { "baseDelayMs",     "BASE DELAY", " ms" },
    { "hpfHz",           "LOW FOCUS",  " Hz" },
    { "analogAmount",    "ANALOG",     "" },
    { "companderAmount", "COMPANDER",  "" },
    { "stereoWidth",     "STEREO WIDTH", "x" },
    { "dryGain",         "DRY",        "x" },
    { "wetDirectGain",   "WET DIRECT", "x" },
    { "wetCrossGain",    "WET CROSS",  "x" },
    { "bassFocusHz",     "BASS FOCUS", " Hz" }
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

    auto setupDbLabel = [this](juce::Label& label) {
        label.setText("+0.0 dB", juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centredRight);
        label.setColour(juce::Label::textColourId, secondaryText());
        label.setFont(juce::Font(9.0f, juce::Font::plain));
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
    advancedTitleLabel.setColour(juce::Label::textColourId, primaryText());
    advancedTitleLabel.setFont(juce::Font(11.5f, juce::Font::bold));
    addAndMakeVisible(advancedTitleLabel);

    advancedHintLabel.setText("Edits switch mode to Custom automatically", juce::dontSendNotification);
    advancedHintLabel.setJustificationType(juce::Justification::centredRight);
    advancedHintLabel.setColour(juce::Label::textColourId, secondaryText());
    advancedHintLabel.setFont(juce::Font(10.0f));
    addAndMakeVisible(advancedHintLabel);

    for (size_t i = 0; i < advancedSliders.size(); ++i) {
        configureAdvancedSlider(advancedSliders[i], advancedLabels[i], kAdvancedSpecs[i].label);
        advancedAttachments[i] = std::make_unique<SliderAttachment>(
            audioProcessor.parameters, kAdvancedSpecs[i].id, advancedSliders[i]);

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
    button.setColour(juce::TextButton::buttonColourId, surfaceColour());
}

void Dimension5AudioProcessorEditor::configureBottomSlider(juce::Slider& slider, juce::Label& label,
                                                           const juce::String& labelText, const juce::String& suffix) {
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 20);
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
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 74, 18);
    slider.setColour(juce::Slider::trackColourId, analogAccent());
    addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, secondaryText());
    label.setFont(juce::Font(9.5f, juce::Font::bold));
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
        const int targetHeight = juce::jlimit(kMinHeight, kMaxHeight, getHeight() + kAdvancedPanelHeight);
        setSize(getWidth(), targetHeight);
    } else {
        advancedVisible = false;
        advancedTitleLabel.setVisible(false);
        advancedHintLabel.setVisible(false);
        for (size_t i = 0; i < advancedSliders.size(); ++i) {
            advancedLabels[i].setVisible(false);
            advancedSliders[i].setVisible(false);
        }
        const int fallback = getHeight() - kAdvancedPanelHeight;
        const int targetHeight = juce::jlimit(kMinHeight, kMaxHeight,
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

    g.fillAll(bgColour());

    g.setColour(dividerColour());
    g.drawLine((float)kMargin, (float)kHeaderHeight, (float)getWidth() - (float)kMargin, (float)kHeaderHeight, 1.0f);

    if (!bottomBarBounds.isEmpty()) {
        g.setColour(dividerColour());
        g.drawLine((float)bottomBarBounds.getX(), (float)bottomBarBounds.getY(),
                   (float)bottomBarBounds.getRight(), (float)bottomBarBounds.getY(), 1.0f);
    }

    if (!inMeterBounds.isEmpty()) {
        drawStereoMeter(g, inMeterBounds, inMeterLeft, inMeterRight, inPeakHoldLeft, inPeakHoldRight);
    }
    if (!outMeterBounds.isEmpty()) {
        drawStereoMeter(g, outMeterBounds, outMeterLeft, outMeterRight, outPeakHoldLeft, outPeakHoldRight);
    }

    if (advancedVisible && !advancedBounds.isEmpty()) {
        auto advArea = advancedBounds.reduced(10, 6).toFloat();
        g.setColour(surfaceColour());
        g.fillRoundedRectangle(advArea, 6.0f);
        g.setColour(dividerColour());
        g.drawRoundedRectangle(advArea, 6.0f, 1.0f);
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
        g.setFont(juce::Font(8.0f, juce::Font::bold));
        g.setColour(secondaryText().withAlpha(0.8f));
        g.drawText(tag, tagArea, juce::Justification::centredLeft);

        auto bar = row;
        g.setColour(elevatedColour());
        g.fillRoundedRectangle(bar.toFloat(), 2.0f);
        g.setColour(dividerColour());
        g.drawRoundedRectangle(bar.toFloat(), 2.0f, 0.8f);

        // dB ticks at -24, -12 and -6 dBFS
        const float tickDbs[3] = { -24.0f, -12.0f, -6.0f };
        g.setColour(dividerColour().withAlpha(0.55f));
        for (float db : tickDbs) {
            const float norm = (db + 60.0f) / 60.0f;
            const float x = (float)bar.getX() + norm * (float)bar.getWidth();
            g.drawLine(x, (float)bar.getY() + 1.0f, x, (float)bar.getBottom() - 1.0f, 0.6f);
        }

        const float norm = meterToNormalised(value);
        const int fillW = juce::roundToInt((float)bar.getWidth() * norm);
        if (fillW > 1) {
            auto fill = bar.withWidth(fillW).toFloat().reduced(1.0f, 1.0f);
            const float db = (value > 1.0e-3f) ? 20.0f * std::log10(value) : -60.0f;
            const juce::Colour fillColour = (db > -0.5f) ? warningAccent()
                                          : (db > -6.0f) ? analogAccent()
                                                         : spatialAccent();
            g.setColour(fillColour.withAlpha(0.82f));
            g.fillRoundedRectangle(fill, 1.5f);
        }

        const float peakNorm = meterToNormalised(peak);
        if (peakNorm > 0.001f) {
            const float px = (float)bar.getX() + peakNorm * (float)bar.getWidth();
            g.setColour(primaryText().withAlpha(0.85f));
            g.drawLine(px, (float)bar.getY() + 1.0f, px, (float)bar.getBottom() - 1.0f, 1.2f);
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
    auto areaLocal = section;

    auto labelRow = areaLocal.removeFromTop(16);
    label.setBounds(labelRow.removeFromLeft(juce::jlimit(52, 84, section.getWidth() / 3)));
    dbLabel.setBounds(labelRow);

    areaLocal.removeFromTop(2);

    const int meterW = juce::jlimit(78, 118, juce::roundToInt((float)areaLocal.getWidth() * 0.40f));
    meterBounds = areaLocal.removeFromRight(meterW).reduced(0, 5);

    areaLocal.removeFromRight(10);
    slider.setBounds(areaLocal.reduced(0, 2));
}

void Dimension5AudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(kMargin);

    // =========================================================================
    // 1. Header Layout (adaptive)
    // =========================================================================
    auto headerArea = area.removeFromTop(kHeaderHeight - 8);

    const int brandW = juce::jlimit(140, 220, juce::roundToInt((float)headerArea.getWidth() * 0.17f));
    auto brandArea = headerArea.removeFromLeft(brandW);
    titleLabel.setBounds(brandArea.removeFromTop(24));
    subtitleLabel.setBounds(brandArea.removeFromTop(16));

    auto rightHeaderArea = headerArea.removeFromRight(190);
    bypassButton.setBounds(rightHeaderArea.removeFromRight(78).withHeight(28));
    rightHeaderArea.removeFromRight(8);
    compareCopyButton.setBounds(rightHeaderArea.removeFromRight(44).withHeight(28));
    rightHeaderArea.removeFromRight(4);
    compareBButton.setBounds(rightHeaderArea.removeFromRight(26).withHeight(28));
    rightHeaderArea.removeFromRight(3);
    compareAButton.setBounds(rightHeaderArea.removeFromRight(26).withHeight(28));

    headerArea.removeFromLeft(14);
    headerArea.removeFromRight(14);
    const int comboH = 28;
    const int modeW = juce::jlimit(96, 120, juce::roundToInt((float)headerArea.getWidth() * 0.25f));
    const int presetW = juce::jlimit(150, 240, headerArea.getWidth() - modeW - 8);

    presetBox.setBounds(headerArea.removeFromLeft(presetW).withHeight(comboH));
    headerArea.removeFromLeft(8);
    modeBox.setBounds(headerArea.removeFromLeft(juce::jmin(modeW, headerArea.getWidth())).withHeight(comboH));

    area.removeFromTop(10);

    // =========================================================================
    // 2. Bottom Bar + Advanced Drawer
    // =========================================================================
    const int advHeight = advancedVisible ? kAdvancedPanelHeight : 0;
    auto bottomArea = area.removeFromBottom(kBottomBarHeight + advHeight);
    bottomBarBounds = bottomArea.removeFromTop(kBottomBarHeight);
    advancedBounds = advancedVisible ? bottomArea : juce::Rectangle<int>();

    auto bar = bottomBarBounds;
    auto safeArea = bar.removeFromRight(juce::jmin(kWatermarkSafeWidth, juce::jmax(0, bar.getWidth() - 320)));
    juce::ignoreUnused(safeArea);

    const int advW = juce::jlimit(96, 124, bar.getWidth() / 7);
    advancedButton.setBounds(bar.removeFromRight(advW).reduced(0, (kBottomBarHeight - 30) / 2));
    bar.removeFromRight(14);

    const int sectionW = juce::jlimit(220, 330, (bar.getWidth() - 28) / 2);

    auto inSection = bar.removeFromLeft(sectionW);
    auto outSection = bar.removeFromRight(sectionW);

    layoutBottomSection(inSection, inputSlider, inputLabel, inputDbLabel, inMeterBounds);
    layoutBottomSection(outSection, outputSlider, outputLabel, outputDbLabel, outMeterBounds);

    // =========================================================================
    // 3. Advanced Drawer Layout
    // =========================================================================
    if (advancedVisible && !advancedBounds.isEmpty()) {
        auto advArea = advancedBounds.reduced(20, 10);
        auto advHeader = advArea.removeFromTop(20);
        advancedTitleLabel.setBounds(advHeader.removeFromLeft(juce::jmin(260, advHeader.getWidth() / 2)));
        advancedHintLabel.setBounds(advHeader);

        advArea.removeFromTop(8);
        const int numSliders = (int)advancedSliders.size();
        const int colGap = 32;
        const int colW = juce::jmax(150, (advArea.getWidth() - colGap) / 2);
        const int rowH = juce::jlimit(22, 30, advArea.getHeight() / 5 - 4);
        const int labelW = juce::jlimit(78, 118, juce::roundToInt((float)colW * 0.34f));

        for (int i = 0; i < numSliders; ++i) {
            const int col = (i < 5) ? 0 : 1;
            const int rowIdx = (i < 5) ? i : (i - 5);
            juce::Rectangle<int> row(advArea.getX() + col * (colW + colGap),
                                     advArea.getY() + rowIdx * (rowH + 4),
                                     colW, rowH);
            advancedLabels[(size_t)i].setBounds(row.removeFromLeft(labelW));
            advancedSliders[(size_t)i].setBounds(row);
        }
    }

    // =========================================================================
    // 4. Central Area: Stereo Field Monitor + Spatial Controls
    // =========================================================================
    area.removeFromBottom(8);
    const int totalW = area.getWidth();
    const int controlsW = juce::jlimit(300, 420, juce::roundToInt((float)totalW * 0.35f));
    const int gap = 12;

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
    const juce::String inDb = formatGainDb(audioProcessor.getFloatParam("inputGain"));
    const juce::String outDb = formatGainDb(audioProcessor.getFloatParam("outputGain"));
    if (inputDbLabel.getText() != inDb) {
        inputDbLabel.setText(inDb, juce::dontSendNotification);
    }
    if (outputDbLabel.getText() != outDb) {
        outputDbLabel.setText(outDb, juce::dontSendNotification);
    }

    // Repaint only the regions that actually change.
    stereoFieldComponent.repaint();
    if (!bottomBarBounds.isEmpty()) {
        repaint(bottomBarBounds);
    }
}
