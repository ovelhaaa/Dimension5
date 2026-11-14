#include "SpatialControlsComponent.h"
#include <algorithm>

namespace {
void placeKnob(juce::Rectangle<int> cell, int knobSize,
               juce::Label& label, juce::Slider& slider, int labelHeight, bool isHero) {
    if (cell.isEmpty()) {
        label.setBounds(0, 0, 0, 0);
        slider.setBounds(0, 0, 0, 0);
        return;
    }
    const int effectiveLabelH = juce::jlimit(12, 18, juce::jmin(labelHeight, cell.getHeight() / 3));
    label.setBounds(cell.removeFromTop(effectiveLabelH));

    // Keep textBox proportional so value readout never clips at compact or maximum sizes
    const int textBoxH = juce::jlimit(16, 20, juce::roundToInt((float)cell.getHeight() * 0.19f));
    const int textBoxW = juce::jlimit(isHero ? 60 : 54, isHero ? 82 : 72, cell.getWidth() - 8);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, textBoxW, textBoxH);

    // Slider stays strictly within the remaining cell bounds - NEVER expand vertically outside cell
    const int availW = cell.getWidth();
    const int availH = cell.getHeight();
    const int targetW = juce::jmin(availW, knobSize + 24);
    const int targetH = juce::jmin(availH, knobSize + 22);
    auto sliderBox = cell.withSizeKeepingCentre(targetW, targetH);
    slider.setBounds(sliderBox);
}
} // namespace

SpatialControlsComponent::SpatialControlsComponent() {
    using namespace Dimension5UI;

    setOpaque(false);

    // Primary Hero Knobs (SPREAD, MIX)
    configureKnob(spreadSlider, spreadLabel, "SPREAD", true);
    configureKnob(mixSlider, mixLabel, "MIX", true);

    // Secondary Precision Knobs (DEPTH, MOTION, COLOR)
    configureKnob(depthSlider, depthLabel, "DEPTH", false);
    configureKnob(motionSlider, motionLabel, "MOTION", false);
    configureKnob(colorSlider, colorLabel, "COLOR", false);
}

void SpatialControlsComponent::configureKnob(juce::Slider& slider, juce::Label& label,
                                             const juce::String& text, bool isSpatialHero) {
    using namespace Dimension5UI;

    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, isSpatialHero ? 72 : 62, 18);
    slider.setColour(juce::Slider::rotarySliderFillColourId, primaryAccent());
    slider.setTextValueSuffix("");
    slider.setSliderSnapsToMousePosition(false);
    slider.setVelocityModeParameters(1.0, 1, 0.08, true, juce::ModifierKeys::ctrlAltCommandModifiers);
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, isSpatialHero ? textOnPanel() : textSecondary());
    label.setFont(Dimension5LookAndFeel::makeTechnicalFont(isSpatialHero ? 11.2f : 10.2f, true));
    label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(label);
}

void SpatialControlsComponent::attachParameters(juce::AudioProcessorValueTreeState& apvts) {
    spreadAttachment = std::make_unique<SliderAttachment>(apvts, "width", spreadSlider);
    mixAttachment    = std::make_unique<SliderAttachment>(apvts, "mix", mixSlider);
    depthAttachment  = std::make_unique<SliderAttachment>(apvts, "depthMs", depthSlider);
    motionAttachment = std::make_unique<SliderAttachment>(apvts, "rateHz", motionSlider);
    colorAttachment  = std::make_unique<SliderAttachment>(apvts, "lpfHz", colorSlider);

    auto bindDefaultMarkerAndReset = [&apvts](juce::Slider& s, const char* id, double fallbackDefault) {
        if (const auto* param = apvts.getParameter(id)) {
            const float defNorm = param->getDefaultValue();
            s.getProperties().set("defaultNorm", defNorm);
            s.setDoubleClickReturnValue(true, (double)param->convertFrom0to1(defNorm));
        } else {
            s.setDoubleClickReturnValue(true, fallbackDefault);
        }
    };

    bindDefaultMarkerAndReset(spreadSlider, "width", 1.0);
    bindDefaultMarkerAndReset(mixSlider,    "mix", 1.0);
    bindDefaultMarkerAndReset(depthSlider,  "depthMs", 0.9);
    bindDefaultMarkerAndReset(motionSlider, "rateHz", 0.25);
    bindDefaultMarkerAndReset(colorSlider,  "lpfHz", 8000.0);

    // Explicit unit formatting applied AFTER attachment binding to override generic host defaults
    spreadSlider.setTextValueSuffix("");
    spreadSlider.setNumDecimalPlacesToDisplay(2);
    spreadSlider.textFromValueFunction = [](double val) {
        return juce::String(val, 2) + "x";
    };
    spreadSlider.valueFromTextFunction = [](const juce::String& text) {
        return text.getDoubleValue();
    };

    // Format MIX correctly: 0.0 .. 1.0 displayed as 0% .. 100%
    mixSlider.setTextValueSuffix("");
    mixSlider.setNumDecimalPlacesToDisplay(0);
    mixSlider.textFromValueFunction = [](double val) {
        return juce::String(juce::roundToInt(val * 100.0)) + "%";
    };
    mixSlider.valueFromTextFunction = [](const juce::String& text) {
        return text.getDoubleValue() * 0.01;
    };

    depthSlider.setTextValueSuffix("");
    depthSlider.setNumDecimalPlacesToDisplay(1);
    depthSlider.textFromValueFunction = [](double val) {
        return juce::String(val, 1) + " ms";
    };
    depthSlider.valueFromTextFunction = [](const juce::String& text) {
        return text.getDoubleValue();
    };

    motionSlider.setTextValueSuffix("");
    motionSlider.setNumDecimalPlacesToDisplay(2);
    motionSlider.textFromValueFunction = [](double val) {
        return juce::String(val, 2) + " Hz";
    };
    motionSlider.valueFromTextFunction = [](const juce::String& text) {
        return text.getDoubleValue();
    };

    colorSlider.setTextValueSuffix("");
    colorSlider.textFromValueFunction = [](double val) {
        if (val >= 1000.0) {
            return juce::String(val * 0.001, 1) + " kHz";
        }
        return juce::String(juce::roundToInt(val)) + " Hz";
    };
    colorSlider.valueFromTextFunction = [](const juce::String& text) {
        const juce::String trimmed = text.trim();
        if (trimmed.endsWithIgnoreCase("k") || trimmed.endsWithIgnoreCase("khz")) {
            return trimmed.getDoubleValue() * 1000.0;
        }
        return trimmed.getDoubleValue();
    };

    spreadSlider.updateText();
    mixSlider.updateText();
    depthSlider.updateText();
    motionSlider.updateText();
    colorSlider.updateText();
}

void SpatialControlsComponent::resized() {
    auto bounds = getLocalBounds().reduced(juce::jlimit(6, 10, getWidth() / 36));

    // Responsive header title
    const bool isCompactHeight = getHeight() < 240;
    const int titleH = isCompactHeight ? 24 : 30;
    bounds.removeFromTop(titleH);

    // Responsive footer (omit if vertical space is tight)
    const int footerH = isCompactHeight ? (bounds.getHeight() < 170 ? 0 : 14) : 20;
    if (footerH > 0) {
        bounds.removeFromBottom(footerH);
    }

    const int totalAvailH = juce::jmax(40, bounds.getHeight());

    // Allocate proportional vertical space between hero row and secondary row
    const int interRowGap = isCompactHeight ? 6 : juce::jlimit(8, 16, totalAvailH / 18);
    const int heightForRows = juce::jmax(30, totalAvailH - interRowGap);

    // 56% for hero knobs (SPREAD, MIX), 44% for secondary (DEPTH, MOTION, COLOR)
    const int heroRowH = (heightForRows * 56) / 100;
    const int secondaryRowH = heightForRows - heroRowH;

    auto heroRow = bounds.removeFromTop(heroRowH);
    bounds.removeFromTop(interRowGap);
    auto secondaryRow = bounds.removeFromTop(secondaryRowH);

    const int heroWidth = heroRow.getWidth() / 2;
    auto spreadBox = heroRow.removeFromLeft(heroWidth).reduced(3, 0);
    auto mixBox = heroRow.reduced(3, 0);

    const int maxHeroKnobFromH = juce::jmax(24, heroRowH - 34);
    const int maxHeroKnobFromW = juce::jmax(24, heroWidth - 16);
    const int heroSize = juce::jlimit(36, 180, juce::jmin(maxHeroKnobFromW, maxHeroKnobFromH));

    const int heroLabelH = isCompactHeight ? 14 : 18;
    placeKnob(spreadBox, heroSize, spreadLabel, spreadSlider, heroLabelH, true);
    placeKnob(mixBox, heroSize, mixLabel, mixSlider, heroLabelH, true);

    const int secWidth = secondaryRow.getWidth() / 3;
    auto depthBox = secondaryRow.removeFromLeft(secWidth).reduced(2, 0);
    auto motionBox = secondaryRow.removeFromLeft(secWidth).reduced(2, 0);
    auto colorBox = secondaryRow.reduced(2, 0);

    const int maxSecKnobFromH = juce::jmax(20, secondaryRowH - 28);
    const int maxSecKnobFromW = juce::jmax(20, secWidth - 12);
    const int secondarySize = juce::jlimit(28, 120, juce::jmin(maxSecKnobFromW, maxSecKnobFromH));

    const int secLabelH = isCompactHeight ? 13 : 16;
    placeKnob(depthBox, secondarySize, depthLabel, depthSlider, secLabelH, false);
    placeKnob(motionBox, secondarySize, motionLabel, motionSlider, secLabelH, false);
    placeKnob(colorBox, secondarySize, colorLabel, colorSlider, secLabelH, false);
}

void SpatialControlsComponent::paint(juce::Graphics& g) {
    using namespace Dimension5UI;

    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    constexpr float corner = 5.5f;

    // 1. Main graphite module body (#292B2D) over the off-white chassis
    g.setColour(panelBackground());
    g.fillRoundedRectangle(bounds, corner);

    // 2. Recessed top header band (#212325)
    const bool isCompact = getHeight() < 240;
    const float headerH = isCompact ? 24.0f : 30.0f;
    const juce::Rectangle<float> headerBgRect(bounds.getX() + 1.0f, bounds.getY() + 1.0f,
                                              bounds.getWidth() - 2.0f, headerH);
    g.setColour(panelRecessed());
    g.fillRoundedRectangle(headerBgRect, corner);
    g.fillRect(bounds.getX() + 1.0f, bounds.getY() + headerH - corner, bounds.getWidth() - 2.0f, corner);

    g.setColour(panelBorder());
    g.drawRoundedRectangle(bounds, corner, 1.0f);
    g.drawHorizontalLine((int)(bounds.getY() + headerH), bounds.getX() + 1.0f, bounds.getRight() - 1.0f);

    // Minimalist orange index bar + section title
    g.setColour(primaryAccent());
    g.fillRoundedRectangle(bounds.getX() + 10.0f, bounds.getY() + (headerH - 11.0f) * 0.5f, 3.0f, 11.0f, 1.2f);

    g.setFont(Dimension5LookAndFeel::makeTechnicalFont(isCompact ? 10.2f : 11.2f, true));
    g.setColour(textOnPanel());
    g.drawText("SPATIAL CONTROLS",
               (int)(bounds.getX() + 18.0f), (int)bounds.getY(),
               (int)(bounds.getWidth() * 0.58f), (int)headerH,
               juce::Justification::centredLeft, false);

    if (bounds.getWidth() >= 270.0f) {
        g.setFont(Dimension5LookAndFeel::makeTechnicalFont(isCompact ? 8.5f : 9.2f, false));
        g.setColour(textMuted());
        g.drawText("SPREAD / BBD / TONE",
                   (int)(bounds.getX() + bounds.getWidth() * 0.48f), (int)bounds.getY(),
                   (int)(bounds.getWidth() * 0.52f - 10.0f), (int)headerH,
                   juce::Justification::centredRight, false);
    }

    // 3. Subtle structural dividers between primary hero row and secondary precision row
    const float footerH = isCompact ? ((getHeight() < 185) ? 0.0f : 14.0f) : 20.0f;
    const float bodyTop = bounds.getY() + headerH + 6.0f;
    const float bodyBottom = bounds.getBottom() - footerH - 6.0f;
    const float bodyH = std::max(20.0f, bodyBottom - bodyTop);
    const float splitY = bodyTop + bodyH * 0.55f;

    g.setColour(panelBorder().withAlpha(0.70f));
    g.drawHorizontalLine((int)splitY, bounds.getX() + 12.0f, bounds.getRight() - 12.0f);

    // Vertical divider between SPREAD and MIX in Hero Row
    g.drawVerticalLine((int)bounds.getCentreX(), bodyTop + 4.0f, splitY - 6.0f);

    // Vertical dividers between DEPTH, MOTION and COLOR in Secondary Row
    const float thirdW = bounds.getWidth() / 3.0f;
    g.drawVerticalLine((int)(bounds.getX() + thirdW), splitY + 6.0f, bodyBottom - 4.0f);
    g.drawVerticalLine((int)(bounds.getX() + thirdW * 2.0f), splitY + 6.0f, bodyBottom - 4.0f);

    // 4. Footer identification when vertical clearance exists
    if (getHeight() >= 190) {
        g.setColour(panelBorder().withAlpha(0.55f));
        g.drawHorizontalLine((int)(bounds.getBottom() - 20.0f), bounds.getX() + 10.0f, bounds.getRight() - 10.0f);

        g.setFont(Dimension5LookAndFeel::makeTechnicalFont(8.5f, true));
        g.setColour(textMuted());
        g.drawText("BBD-LITE ENGINE  |  ANTIPHASE MATRIX",
                   (int)(bounds.getX() + 12.0f), (int)(bounds.getBottom() - 18.0f),
                   (int)(bounds.getWidth() - 24.0f), 15,
                   juce::Justification::centredLeft, false);
    }
}
