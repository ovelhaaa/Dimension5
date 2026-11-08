#include "SpatialControlsComponent.h"

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
    auto bounds = getLocalBounds().reduced(8);

    // Section title
    bounds.removeFromTop(28);

    // Split vertically: Hero Top Row (~54%), Secondary Bottom Row (~46%)
    const int topHeight = (int)((float)bounds.getHeight() * 0.54f);
    auto topArea = bounds.removeFromTop(topHeight);
    bounds.removeFromTop(6);
    auto bottomArea = bounds;

    // Top Row: SPREAD (left) and MIX (right)
    const int heroWidth = topArea.getWidth() / 2;
    auto spreadBox = topArea.removeFromLeft(heroWidth).reduced(4, 2);
    auto mixBox = topArea.reduced(4, 2);

    spreadLabel.setBounds(spreadBox.removeFromTop(18));
    spreadSlider.setBounds(spreadBox);

    mixLabel.setBounds(mixBox.removeFromTop(18));
    mixSlider.setBounds(mixBox);

    // Bottom Row: DEPTH, MOTION, COLOR (3-across)
    const int secWidth = bottomArea.getWidth() / 3;
    auto depthBox = bottomArea.removeFromLeft(secWidth).reduced(3, 2);
    auto motionBox = bottomArea.removeFromLeft(secWidth).reduced(3, 2);
    auto colorBox = bottomArea.reduced(3, 2);

    depthLabel.setBounds(depthBox.removeFromTop(16));
    depthSlider.setBounds(depthBox);

    motionLabel.setBounds(motionBox.removeFromTop(16));
    motionSlider.setBounds(motionBox);

    colorLabel.setBounds(colorBox.removeFromTop(16));
    colorSlider.setBounds(colorBox);
}

void SpatialControlsComponent::paint(juce::Graphics& g) {
    using namespace Dimension5UI;

    auto bounds = getLocalBounds().toFloat();

    // Panel Chassis
    g.setColour(surfaceColour());
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(dividerColour());
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    // Header divider line
    const float headerH = 32.0f;
    g.setColour(dividerColour());
    g.drawLine(bounds.getX() + 8.0f, bounds.getY() + headerH, bounds.getRight() - 8.0f, bounds.getY() + headerH, 1.0f);

    // Header title
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(secondaryText());
    g.drawText("SPATIAL MATRIX CONTROLS", bounds.getX() + 12.0f, bounds.getY(), bounds.getWidth() - 24.0f, headerH, juce::Justification::centredLeft);

    // Subtle divider between Hero and Secondary rows
    const float midDividerY = bounds.getY() + headerH + (bounds.getHeight() - headerH) * 0.54f;
    g.setColour(dividerColour().withAlpha(0.6f));
    g.drawLine(bounds.getX() + 14.0f, midDividerY, bounds.getRight() - 14.0f, midDividerY, 0.75f);
}
