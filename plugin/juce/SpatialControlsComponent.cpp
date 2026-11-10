#include "SpatialControlsComponent.h"

namespace {
void placeKnob(juce::Rectangle<int> cell, int knobSize,
               juce::Label& label, juce::Slider& slider, int labelHeight) {
    if (cell.isEmpty()) {
        label.setBounds(0, 0, 0, 0);
        slider.setBounds(0, 0, 0, 0);
        return;
    }
    const int effectiveLabelH = juce::jlimit(12, 18, juce::jmin(labelHeight, cell.getHeight() / 3));
    label.setBounds(cell.removeFromTop(effectiveLabelH));

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
    slider.setColour(juce::Slider::rotarySliderFillColourId, isSpatialHero ? spatialAccent() : analogAccent());
    slider.setTextValueSuffix("");
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, isSpatialHero ? spatialAccent() : analogAccent());
    label.setFont(juce::Font(isSpatialHero ? 11.5f : 10.5f, juce::Font::bold));
    addAndMakeVisible(label);
}

void SpatialControlsComponent::attachParameters(juce::AudioProcessorValueTreeState& apvts) {
    spreadAttachment = std::make_unique<SliderAttachment>(apvts, "width", spreadSlider);
    mixAttachment    = std::make_unique<SliderAttachment>(apvts, "mix", mixSlider);
    depthAttachment  = std::make_unique<SliderAttachment>(apvts, "depthMs", depthSlider);
    motionAttachment = std::make_unique<SliderAttachment>(apvts, "rateHz", motionSlider);
    colorAttachment  = std::make_unique<SliderAttachment>(apvts, "lpfHz", colorSlider);

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
    const bool isCompactHeight = bounds.getHeight() < 240;
    const int titleH = isCompactHeight ? 22 : 28;
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
    placeKnob(spreadBox, heroSize, spreadLabel, spreadSlider, heroLabelH);
    placeKnob(mixBox, heroSize, mixLabel, mixSlider, heroLabelH);

    const int secWidth = secondaryRow.getWidth() / 3;
    auto depthBox = secondaryRow.removeFromLeft(secWidth).reduced(2, 0);
    auto motionBox = secondaryRow.removeFromLeft(secWidth).reduced(2, 0);
    auto colorBox = secondaryRow.reduced(2, 0);

    const int maxSecKnobFromH = juce::jmax(20, secondaryRowH - 28);
    const int maxSecKnobFromW = juce::jmax(20, secWidth - 12);
    const int secondarySize = juce::jlimit(28, 120, juce::jmin(maxSecKnobFromW, maxSecKnobFromH));

    const int secLabelH = isCompactHeight ? 13 : 16;
    placeKnob(depthBox, secondarySize, depthLabel, depthSlider, secLabelH);
    placeKnob(motionBox, secondarySize, motionLabel, motionSlider, secLabelH);
    placeKnob(colorBox, secondarySize, colorLabel, colorSlider, secLabelH);
}

void SpatialControlsComponent::paint(juce::Graphics& g) {
    using namespace Dimension5UI;

    auto bounds = getLocalBounds().toFloat();

    g.setColour(surfaceColour());
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(dividerColour());
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    const float headerH = (getHeight() < 240) ? 24.0f : 32.0f;
    g.setColour(dividerColour());
    g.drawLine(bounds.getX() + 8.0f, bounds.getY() + headerH, bounds.getRight() - 8.0f, bounds.getY() + headerH, 1.0f);

    g.setFont(juce::Font((getHeight() < 240) ? 10.0f : 11.0f, juce::Font::bold));
    g.setColour(secondaryText());
    g.drawText("SPATIAL MATRIX CONTROLS", bounds.getX() + 12.0f, bounds.getY(),
               bounds.getWidth() - 24.0f, headerH, juce::Justification::centredLeft);

    // Footer identification: displayed when sufficient vertical clearance exists
    if (getHeight() >= 190) {
        g.setFont(juce::Font(8.0f, juce::Font::plain));
        g.setColour(secondaryText().withAlpha(0.45f));
        g.drawText("BBD-LITE ENGINE / STEREO MATRIX",
                   bounds.getX() + 12.0f, bounds.getBottom() - 18.0f,
                   bounds.getWidth() - 24.0f, 14.0f, juce::Justification::centredLeft);
    }
}
