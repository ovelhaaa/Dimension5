#include "Dimension5LookAndFeel.h"
#include <cmath>

Dimension5LookAndFeel::Dimension5LookAndFeel() {
    using namespace Dimension5UI;

    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxTextColourId, primaryText());
    setColour(juce::Slider::textBoxBackgroundColourId, surfaceColour());
    setColour(juce::Slider::textBoxHighlightColourId, spatialAccent().withAlpha(0.25f));

    setColour(juce::ComboBox::backgroundColourId, surfaceColour());
    setColour(juce::ComboBox::outlineColourId, dividerColour());
    setColour(juce::ComboBox::textColourId, primaryText());
    setColour(juce::ComboBox::arrowColourId, secondaryText());

    setColour(juce::PopupMenu::backgroundColourId, bgColour());
    setColour(juce::PopupMenu::textColourId, primaryText());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, elevatedColour());
    setColour(juce::PopupMenu::highlightedTextColourId, spatialAccent());
}

void Dimension5LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPosProportional, float rotaryStartAngle,
                                             float rotaryEndAngle, juce::Slider& slider) {
    using namespace Dimension5UI;

    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float minDim = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto center = bounds.getCentre();
    const bool isHero = (minDim > 80.0f); // SPREAD & MIX hero knobs
    const float radius = minDim * (isHero ? 0.45f : 0.44f);

    const auto arcColour = slider.findColour(juce::Slider::rotarySliderFillColourId);
    const bool isHovered = slider.isMouseOverOrDragging();

    // 1. Outer Chassis Ring / Bezel
    g.setColour(surfaceColour());
    g.fillEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f);

    g.setColour(isHovered ? dividerColour().brighter(0.2f) : dividerColour());
    g.drawEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f, 1.2f);

    // 2. Tick marks along the periphery for hero knobs
    if (isHero) {
        constexpr int numTicks = 11;
        const float tickInnerR = radius + 2.5f;
        const float tickOuterR = radius + 5.5f;
        g.setColour(secondaryText().withAlpha(0.4f));

        for (int i = 0; i < numTicks; ++i) {
            const float frac = (float)i / (float)(numTicks - 1);
            const float angle = rotaryStartAngle + frac * (rotaryEndAngle - rotaryStartAngle) - juce::MathConstants<float>::halfPi;
            const float cosA = std::cos(angle);
            const float sinA = std::sin(angle);
            g.drawLine(center.x + cosA * tickInnerR, center.y + sinA * tickInnerR,
                       center.x + cosA * tickOuterR, center.y + sinA * tickOuterR, 1.0f);
        }
    }

    // 3. Track background groove
    const float trackRadius = radius - (isHero ? 7.0f : 5.0f);
    const float trackStroke = isHero ? 4.5f : 3.5f;

    juce::Path backgroundArc;
    backgroundArc.addCentredArc(center.x, center.y, trackRadius, trackRadius, 0.0f,
                                rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(elevatedColour());
    g.strokePath(backgroundArc, juce::PathStrokeType(trackStroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // 4. Value Arc with glow
    const float currentAngle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    if (currentAngle > rotaryStartAngle + 0.01f) {
        juce::Path valueArc;
        valueArc.addCentredArc(center.x, center.y, trackRadius, trackRadius, 0.0f,
                               rotaryStartAngle, currentAngle, true);

        // Soft glow for active arc
        if (isHero) {
            g.setColour(arcColour.withAlpha(0.25f));
            g.strokePath(valueArc, juce::PathStrokeType(trackStroke + 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour(arcColour);
        g.strokePath(valueArc, juce::PathStrokeType(trackStroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 5. Inner dial cap
    const float capRadius = trackRadius - (isHero ? 7.0f : 5.0f);
    g.setColour(elevatedColour());
    g.fillEllipse(center.x - capRadius, center.y - capRadius, capRadius * 2.0f, capRadius * 2.0f);
    g.setColour(dividerColour());
    g.drawEllipse(center.x - capRadius, center.y - capRadius, capRadius * 2.0f, capRadius * 2.0f, 1.0f);

    // 6. Indicator Needle & Center Dot
    const float pointerAngle = currentAngle - juce::MathConstants<float>::halfPi;
    const float pointerStartR = capRadius * 0.25f;
    const float pointerEndR = capRadius * 0.90f;

    const float px1 = center.x + std::cos(pointerAngle) * pointerStartR;
    const float py1 = center.y + std::sin(pointerAngle) * pointerStartR;
    const float px2 = center.x + std::cos(pointerAngle) * pointerEndR;
    const float py2 = center.y + std::sin(pointerAngle) * pointerEndR;

    g.setColour(arcColour);
    g.drawLine(px1, py1, px2, py2, isHero ? 2.5f : 2.0f);

    // Center pivot jewel
    g.setColour(primaryText());
    const float jewelR = isHero ? 3.0f : 2.2f;
    g.fillEllipse(center.x - jewelR, center.y - jewelR, jewelR * 2.0f, jewelR * 2.0f);
}

void Dimension5LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float minSliderPos, float maxSliderPos,
                                             const juce::Slider::SliderStyle style, juce::Slider& slider) {
    juce::ignoreUnused(minSliderPos, maxSliderPos, style);
    using namespace Dimension5UI;

    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float trackY = bounds.getCentreY();
    const float trackHeight = 5.0f;
    const auto trackBounds = juce::Rectangle<float>(bounds.getX(), trackY - trackHeight * 0.5f, bounds.getWidth(), trackHeight);

    // Track groove
    g.setColour(elevatedColour());
    g.fillRoundedRectangle(trackBounds, 2.5f);
    g.setColour(dividerColour());
    g.drawRoundedRectangle(trackBounds, 2.5f, 0.75f);

    // Active fill
    const auto fillColour = slider.findColour(juce::Slider::trackColourId);
    auto fillBounds = trackBounds.withWidth(juce::jmax(0.0f, sliderPos - bounds.getX()));
    g.setColour(fillColour.withAlpha(0.85f));
    g.fillRoundedRectangle(fillBounds, 2.5f);

    // Precision Thumb
    const float thumbWidth = 9.0f;
    const float thumbHeight = 14.0f;
    const auto thumbBounds = juce::Rectangle<float>(sliderPos - thumbWidth * 0.5f, trackY - thumbHeight * 0.5f, thumbWidth, thumbHeight);

    g.setColour(primaryText());
    g.fillRoundedRectangle(thumbBounds, 2.0f);
    g.setColour(dividerColour().brighter(0.2f));
    g.drawRoundedRectangle(thumbBounds, 2.0f, 1.0f);

    // Center indicator line on thumb
    g.setColour(bgColour());
    g.drawLine(sliderPos, trackY - thumbHeight * 0.35f, sliderPos, trackY + thumbHeight * 0.35f, 1.0f);
}

void Dimension5LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                 const juce::Colour& backgroundColour,
                                                 bool shouldDrawButtonAsHighlighted,
                                                 bool shouldDrawButtonAsDown) {
    juce::ignoreUnused(backgroundColour);
    using namespace Dimension5UI;

    auto bounds = button.getLocalBounds().toFloat();
    const bool isToggled = button.getToggleState();
    const bool isBypass = (button.getButtonText() == "BYPASS");

    juce::Colour fill = surfaceColour();
    if (isToggled) {
        fill = isBypass ? warningAccent().withAlpha(0.20f) : spatialAccent().withAlpha(0.18f);
    } else if (shouldDrawButtonAsHighlighted) {
        fill = elevatedColour();
    }
    if (shouldDrawButtonAsDown) {
        fill = bgColour();
    }

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 4.0f);

    juce::Colour border = dividerColour();
    if (isToggled) {
        border = isBypass ? warningAccent() : spatialAccent();
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
    using namespace Dimension5UI;

    const bool isToggled = button.getToggleState();
    const bool isBypass = (button.getButtonText() == "BYPASS");

    juce::Colour textCol = secondaryText();
    if (isToggled) {
        textCol = isBypass ? warningAccent() : spatialAccent();
    } else if (shouldDrawButtonAsHighlighted) {
        textCol = primaryText();
    }

    g.setFont(juce::Font(11.5f, juce::Font::bold));
    g.setColour(textCol);
    g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}

void Dimension5LookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                         int buttonX, int buttonY, int buttonW, int buttonH,
                                         juce::ComboBox& box) {
    juce::ignoreUnused(isButtonDown, buttonX, buttonY, buttonW, buttonH);
    using namespace Dimension5UI;

    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();

    g.setColour(surfaceColour());
    g.fillRoundedRectangle(bounds, 4.0f);

    const bool isHovered = box.isMouseOver(true);
    g.setColour(isHovered ? secondaryText() : dividerColour());
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    // Crisp dropdown arrow
    const float arrowX = (float)width - 16.0f;
    const float arrowY = (float)height * 0.5f - 2.0f;
    juce::Path arrow;
    arrow.startNewSubPath(arrowX - 4.0f, arrowY);
    arrow.lineTo(arrowX + 4.0f, arrowY);
    arrow.lineTo(arrowX, arrowY + 4.5f);
    arrow.closeSubPath();

    g.setColour(isHovered ? spatialAccent() : box.findColour(juce::ComboBox::arrowColourId));
    g.fillPath(arrow);
}

void Dimension5LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    using namespace Dimension5UI;
    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
    g.setColour(surfaceColour());
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(dividerColour());
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
}

void Dimension5LookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                              bool isSeparator, bool isActive, bool isHighlighted,
                                              bool isTicked, bool hasSubMenu,
                                              const juce::String& text, const juce::String& shortcutKeyText,
                                              const juce::Drawable* icon, const juce::Colour* textColour) {
    juce::ignoreUnused(isTicked, hasSubMenu, shortcutKeyText, icon);
    using namespace Dimension5UI;

    if (isSeparator) {
        auto r = area.reduced(5, 0);
        g.setColour(dividerColour());
        g.fillRect(r.removeFromTop(1));
        return;
    }

    if (isHighlighted && isActive) {
        g.setColour(elevatedColour());
        g.fillRoundedRectangle(area.toFloat().reduced(2.0f, 1.0f), 3.0f);
    }

    juce::Colour tc = primaryText();
    if (!isActive) {
        tc = secondaryText().withAlpha(0.5f);
    } else if (isHighlighted) {
        tc = spatialAccent();
    } else if (textColour != nullptr) {
        tc = *textColour;
    }

    g.setColour(tc);
    g.setFont(juce::Font(12.5f, juce::Font::plain));
    g.drawText(text, area.reduced(10, 0), juce::Justification::centredLeft, true);
}
