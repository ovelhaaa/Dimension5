#include "SpatialControlsComponent.h"

namespace {
void placeKnob(juce::Rectangle<int> cell, int knobSize,
               juce::Label& label, juce::Slider& slider, int labelHeight) {
    const int sliderHeight = knobSize + 22;
    const int totalHeight = labelHeight + sliderHeight;
    const int boxWidth = juce::jmin(cell.getWidth(), knobSize + 24);
    auto box = cell.withSizeKeepingCentre(boxWidth, juce::jmin(cell.getHeight(), totalHeight));

    label.setBounds(box.removeFromTop(labelHeight));
    auto sliderArea = box.withSizeKeepingCentre(juce::jmin(box.getWidth(), knobSize), sliderHeight);
    slider.setBounds(sliderArea);
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
    auto bounds = getLocalBounds().reduced(10);

    // Section title row
    bounds.removeFromTop(30);

    // Leave a slim footer at the bottom for engine identification.
    bounds.removeFromBottom(22);

    const int heroSize = juce::jlimit(96, 184, juce::jmin(bounds.getWidth() / 2 - 16,
                                                          juce::roundToInt((float)bounds.getHeight() * 0.42f)));
    const int secondarySize = juce::jlimit(70, 120, juce::jmin(bounds.getWidth() / 3 - 12,
                                                               juce::roundToInt((float)bounds.getHeight() * 0.26f)));

    const int heroRowH = 18 + heroSize + 22;
    const int secondaryRowH = 16 + secondarySize + 22;
    const int contentH = heroRowH + 18 + secondaryRowH;

    auto content = bounds.withHeight(juce::jmin(bounds.getHeight(), contentH));
    content.setY(bounds.getY() + juce::jmax(0, (bounds.getHeight() - content.getHeight()) / 2));

    auto heroRow = content.removeFromTop(heroRowH);
    content.removeFromTop(18);
    auto secondaryRow = content.removeFromTop(secondaryRowH);

    const int heroWidth = heroRow.getWidth() / 2;
    auto spreadBox = heroRow.removeFromLeft(heroWidth).reduced(4, 0);
    auto mixBox = heroRow.reduced(4, 0);
    placeKnob(spreadBox, heroSize, spreadLabel, spreadSlider, 18);
    placeKnob(mixBox, heroSize, mixLabel, mixSlider, 18);

    const int secWidth = secondaryRow.getWidth() / 3;
    auto depthBox = secondaryRow.removeFromLeft(secWidth).reduced(3, 0);
    auto motionBox = secondaryRow.removeFromLeft(secWidth).reduced(3, 0);
    auto colorBox = secondaryRow.reduced(3, 0);
    placeKnob(depthBox, secondarySize, depthLabel, depthSlider, 16);
    placeKnob(motionBox, secondarySize, motionLabel, motionSlider, 16);
    placeKnob(colorBox, secondarySize, colorLabel, colorSlider, 16);
}

void SpatialControlsComponent::paint(juce::Graphics& g) {
    using namespace Dimension5UI;

    auto bounds = getLocalBounds().toFloat();

    g.setColour(surfaceColour());
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(dividerColour());
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    const float headerH = 32.0f;
    g.setColour(dividerColour());
    g.drawLine(bounds.getX() + 8.0f, bounds.getY() + headerH, bounds.getRight() - 8.0f, bounds.getY() + headerH, 1.0f);

    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(secondaryText());
    g.drawText("SPATIAL MATRIX CONTROLS", bounds.getX() + 12.0f, bounds.getY(),
               bounds.getWidth() - 24.0f, headerH, juce::Justification::centredLeft);

    // Footer identification: clearly informational, never fabricated measurements.
    g.setFont(juce::Font(8.0f, juce::Font::plain));
    g.setColour(secondaryText().withAlpha(0.45f));
    g.drawText("BBD-LITE ENGINE / STEREO MATRIX",
               bounds.getX() + 12.0f, bounds.getBottom() - 20.0f,
               bounds.getWidth() - 24.0f, 14.0f, juce::Justification::centredLeft);
}
