#include "Dimension5LookAndFeel.h"
#include <algorithm>
#include <cmath>

Dimension5LookAndFeel::Dimension5LookAndFeel() {
    using namespace Dimension5UI;

    setColour(juce::Slider::textBoxOutlineColourId, panelBorder());
    setColour(juce::Slider::textBoxTextColourId, textOnPanel());
    setColour(juce::Slider::textBoxBackgroundColourId, displayBackground());
    setColour(juce::Slider::textBoxHighlightColourId, accentHighlightDim());

    setColour(juce::ComboBox::backgroundColourId, panelBackground());
    setColour(juce::ComboBox::outlineColourId, panelBorder());
    setColour(juce::ComboBox::textColourId, textOnPanel());
    setColour(juce::ComboBox::arrowColourId, textSecondary());

    setColour(juce::PopupMenu::backgroundColourId, panelBackground());
    setColour(juce::PopupMenu::textColourId, textOnPanel());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, panelElevated());
    setColour(juce::PopupMenu::highlightedTextColourId, primaryAccent());
}

juce::Font Dimension5LookAndFeel::makeTechnicalFont(float height, bool bold) {
    return juce::Font(height, bold ? juce::Font::bold : juce::Font::plain);
}

void Dimension5LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPosProportional, float rotaryStartAngle,
                                             float rotaryEndAngle, juce::Slider& slider) {
    using namespace Dimension5UI;

    const auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height).reduced(2.0f);
    const float diameter = std::min(bounds.getWidth(), bounds.getHeight());
    if (diameter <= 16.0f) {
        return;
    }

    const auto centre = bounds.getCentre();
    const bool isHero = (diameter > 76.0f);
    const float outerRadius = diameter * (isHero ? 0.46f : 0.45f);
    const float arcRadius   = outerRadius * 0.87f;
    const float bodyRadius  = outerRadius * 0.68f;
    const float capRadius   = outerRadius * 0.55f;

    const juce::Colour activeColour = primaryAccent();
    const juce::Colour activeBright = accentHover();
    const bool isHovered  = slider.isMouseOverOrDragging();
    const bool isDragging = slider.isMouseButtonDown();

    // 1. Precision scale divisions (21 ticks along the 270-degree sweep)
    constexpr int numTicks = 21;
    for (int i = 0; i < numTicks; ++i) {
        const float norm = (float)i / (float)(numTicks - 1);
        const float angle = rotaryStartAngle + norm * (rotaryEndAngle - rotaryStartAngle);
        const bool isMajor = (i == 0 || i == 5 || i == 10 || i == 15 || i == 20);
        const float innerTick = outerRadius * (isMajor ? 0.935f : 0.96f);
        const float outerTick = outerRadius * (isMajor ? 1.045f : 1.015f);
        const float sinA = std::sin(angle);
        const float cosA = -std::cos(angle);
        const bool passed = norm <= sliderPosProportional + 0.001f;

        g.setColour(passed ? activeColour.withAlpha(isMajor ? 0.82f : 0.46f)
                           : textMuted().withAlpha(isMajor ? 0.56f : 0.28f));
        g.drawLine(centre.x + sinA * innerTick, centre.y + cosA * innerTick,
                   centre.x + sinA * outerTick, centre.y + cosA * outerTick,
                   isMajor ? 1.45f : 1.0f);
    }

    // 2. Discreet default-value marker on outer scale ring (if provided)
    if (slider.getProperties().contains("defaultNorm")) {
        const float defNorm = juce::jlimit(0.0f, 1.0f, (float)slider.getProperties()["defaultNorm"]);
        const float defAngle = rotaryStartAngle + defNorm * (rotaryEndAngle - rotaryStartAngle);
        const float sinD = std::sin(defAngle);
        const float cosD = -std::cos(defAngle);
        const float markerR = outerRadius * 1.08f;
        g.setColour(textSecondary().withAlpha(0.85f));
        g.fillEllipse(centre.x + sinD * markerR - 1.7f, centre.y + cosD * markerR - 1.7f, 3.4f, 3.4f);
    }

    // 3. Recessed inactive value track arc
    const float trackThickness = std::max(isHero ? 3.2f : 2.6f, diameter * 0.050f);
    juce::Path backgroundArc;
    backgroundArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(knobTrack());
    g.strokePath(backgroundArc, juce::PathStrokeType(trackThickness + 1.4f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    g.setColour(panelBorder());
    g.strokePath(backgroundArc, juce::PathStrokeType(trackThickness * 0.72f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

    // 4. Active orange value arc
    const float valueAngle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    if (sliderPosProportional > 0.002f) {
        juce::Path valueArc;
        valueArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                               rotaryStartAngle, valueAngle, true);
        if (isHovered || isHero) {
            const float haloAlpha = isDragging ? 0.26f : (isHovered ? 0.16f : 0.10f);
            g.setColour(activeColour.withAlpha(haloAlpha));
            g.strokePath(valueArc, juce::PathStrokeType(trackThickness * 1.9f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
        }
        g.setColour(isHovered ? activeBright : activeColour);
        g.strokePath(valueArc, juce::PathStrokeType(trackThickness, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    }

    // 5. Tactile dark graphite outer collar & subtle contact shadow
    g.setColour(displayBackground().withAlpha(0.65f));
    g.fillEllipse(centre.x - bodyRadius, centre.y - bodyRadius + 1.5f, bodyRadius * 2.0f, bodyRadius * 2.0f);

    juce::ColourGradient collarGrad(knobBodyOuter().brighter(isHovered ? 0.08f : 0.02f),
                                    centre.x, centre.y - bodyRadius,
                                    displayBackground(),
                                    centre.x, centre.y + bodyRadius, false);
    g.setGradientFill(collarGrad);
    g.fillEllipse(centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f);

    g.setColour(isHovered ? panelBorderCrisp() : knobRing());
    g.drawEllipse(centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f, 1.15f);

    // 6. Machined inner cap surface
    juce::ColourGradient capGrad(knobCapTop(), centre.x, centre.y - capRadius,
                                 knobBodyInner(), centre.x, centre.y + capRadius, false);
    g.setGradientFill(capGrad);
    g.fillEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f);

    g.setColour(panelBorder());
    g.drawEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f, 1.0f);

    // 7. Crisp radial position indicator line + orange arc tip marker
    const float sinV = std::sin(valueAngle);
    const float cosV = -std::cos(valueAngle);
    const float pointerInner = capRadius * 0.22f;
    const float pointerOuter = capRadius * 0.88f;
    g.setColour(isDragging ? activeBright : textOnPanel());
    g.drawLine(centre.x + sinV * pointerInner, centre.y + cosV * pointerInner,
               centre.x + sinV * pointerOuter, centre.y + cosV * pointerOuter,
               std::max(1.9f, diameter * 0.034f));

    const float tipX = centre.x + sinV * arcRadius;
    const float tipY = centre.y + cosV * arcRadius;
    const float tipOuter = isHero ? 2.8f : 2.4f;
    const float tipInner = tipOuter * 0.5f;
    g.setColour(isHovered ? activeBright : activeColour);
    g.fillEllipse(tipX - tipOuter, tipY - tipOuter, tipOuter * 2.0f, tipOuter * 2.0f);
    g.setColour(textOnPanel());
    g.fillEllipse(tipX - tipInner, tipY - tipInner, tipInner * 2.0f, tipInner * 2.0f);
}

void Dimension5LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float minSliderPos, float maxSliderPos,
                                             const juce::Slider::SliderStyle style, juce::Slider& slider) {
    juce::ignoreUnused(minSliderPos, maxSliderPos, style);
    using namespace Dimension5UI;

    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float trackY = bounds.getCentreY();
    const float trackHeight = 6.0f;
    const auto trackBounds = juce::Rectangle<float>(bounds.getX(), trackY - trackHeight * 0.5f,
                                                    bounds.getWidth(), trackHeight);
    const bool isHovered  = slider.isMouseOverOrDragging();
    const bool isDragging = slider.isMouseButtonDown();

    // 1. Recessed graphite track bed
    g.setColour(displayBackground());
    g.fillRoundedRectangle(trackBounds, 2.5f);
    g.setColour(panelBorder());
    g.drawRoundedRectangle(trackBounds, 2.5f, 0.9f);

    // Subtle internal reference ticks along the track
    g.setColour(displayAxis().withAlpha(0.75f));
    for (int t = 1; t <= 3; ++t) {
        const float tx = trackBounds.getX() + trackBounds.getWidth() * ((float)t * 0.25f);
        g.drawLine(tx, trackBounds.getY() + 1.0f, tx, trackBounds.getBottom() - 1.0f, 0.7f);
    }

    // 2. Active orange fill
    const float fillW = juce::jlimit(0.0f, trackBounds.getWidth(), sliderPos - bounds.getX());
    if (fillW > 1.0f) {
        auto fillBounds = trackBounds.withWidth(fillW).reduced(0.8f, 0.8f);
        g.setColour(isHovered ? accentHover() : primaryAccent());
        g.fillRoundedRectangle(fillBounds, 2.0f);
    }

    // 3. Machined light-metallic handle
    const float thumbWidth  = 10.0f;
    const float thumbHeight = std::min(15.0f, std::max(11.0f, bounds.getHeight() - 3.0f));
    const float clampedX    = juce::jlimit(bounds.getX() + thumbWidth * 0.5f,
                                           bounds.getRight() - thumbWidth * 0.5f, sliderPos);
    const auto thumbBounds  = juce::Rectangle<float>(clampedX - thumbWidth * 0.5f,
                                                     trackY - thumbHeight * 0.5f,
                                                     thumbWidth, thumbHeight);

    // Subtle drop shadow
    g.setColour(displayBackground().withAlpha(0.7f));
    g.fillRoundedRectangle(thumbBounds.translated(0.0f, 1.0f), 2.0f);

    g.setColour(textOnPanel());
    g.fillRoundedRectangle(thumbBounds, 2.0f);
    g.setColour(isDragging ? primaryAccent() : (isHovered ? panelBorderCrisp().brighter(0.25f) : panelBorderCrisp()));
    g.drawRoundedRectangle(thumbBounds, 2.0f, 1.0f);

    // Precision index line inside the handle
    g.setColour(isDragging ? primaryAccent() : panelBackground());
    g.drawLine(clampedX, trackY - thumbHeight * 0.32f, clampedX, trackY + thumbHeight * 0.32f, 1.3f);
}

juce::Label* Dimension5LookAndFeel::createSliderTextBox(juce::Slider& slider) {
    using namespace Dimension5UI;

    auto* l = LookAndFeel_V4::createSliderTextBox(slider);
    const bool isRotary = slider.isRotary();
    l->setFont(makeTechnicalFont(isRotary ? 11.8f : 11.0f, true));
    l->setJustificationType(juce::Justification::centred);
    l->setColour(juce::Label::textColourId, textOnPanel());
    l->setColour(juce::Label::backgroundColourId, displayBackground());
    l->setColour(juce::Label::outlineColourId, panelBorder());
    l->setColour(juce::Label::textWhenEditingColourId, textOnPanel());
    l->setColour(juce::Label::backgroundWhenEditingColourId, displayBackground());
    l->setColour(juce::Label::outlineWhenEditingColourId, primaryAccent());
    l->setMinimumHorizontalScale(0.75f);
    return l;
}

void Dimension5LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                 const juce::Colour& backgroundColour,
                                                 bool shouldDrawButtonAsHighlighted,
                                                 bool shouldDrawButtonAsDown) {
    juce::ignoreUnused(backgroundColour);
    using namespace Dimension5UI;

    const auto bounds = button.getLocalBounds().toFloat().reduced(0.8f);
    const float corner = 3.5f;
    const bool active = button.getToggleState();
    const bool isBypass = (button.getButtonText() == "BYPASS");

    if (isBypass && active) {
        // Critical BYPASS engaged: recessed dark display interior with crisp double-weight orange frame
        // and solid orange alert bar on the left edge, distinct from normal selected toggles.
        juce::Colour fill = shouldDrawButtonAsDown ? panelElevated()
                          : (shouldDrawButtonAsHighlighted ? panelRecessed() : displayBackground());
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, corner);

        g.setColour(shouldDrawButtonAsHighlighted ? accentHover() : primaryAccent());
        g.drawRoundedRectangle(bounds, corner, 1.6f);

        const juce::Rectangle<float> alertBar(bounds.getX() + 3.5f, bounds.getY() + 4.0f,
                                              3.0f, std::max(4.0f, bounds.getHeight() - 8.0f));
        g.fillRoundedRectangle(alertBar, 1.0f);
        return;
    }

    juce::Colour fill = active ? primaryAccent() : panelElevated();
    if (shouldDrawButtonAsDown) {
        fill = active ? accentPressed() : panelElevated().brighter(0.10f);
    } else if (shouldDrawButtonAsHighlighted) {
        fill = active ? accentHover() : panelElevated().brighter(0.06f);
    }

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, corner);

    const juce::Colour border = active ? accentHover()
        : (shouldDrawButtonAsHighlighted ? panelBorderCrisp().brighter(0.15f) : panelBorder());
    g.setColour(border);
    g.drawRoundedRectangle(bounds, corner, 1.0f);

    // Subtle tactile top indicator bar on wide instrument switches
    if (bounds.getWidth() > 38.0f && bounds.getHeight() >= 20.0f) {
        const float ledW = std::min(16.0f, bounds.getWidth() * 0.24f);
        const juce::Rectangle<float> ledRect(bounds.getCentreX() - ledW * 0.5f, bounds.getY() + 2.2f, ledW, 1.5f);
        g.setColour(active ? displayBackground().withAlpha(0.55f)
                           : textMuted().withAlpha(shouldDrawButtonAsHighlighted ? 0.65f : 0.35f));
        g.fillRoundedRectangle(ledRect, 0.75f);
    }
}

void Dimension5LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                           bool shouldDrawButtonAsHighlighted,
                                           bool shouldDrawButtonAsDown) {
    juce::ignoreUnused(shouldDrawButtonAsDown);
    using namespace Dimension5UI;

    const bool active = button.getToggleState();
    const bool isBypass = (button.getButtonText() == "BYPASS");

    const float fontHeight = std::clamp((float)button.getHeight() * 0.44f, 9.8f, 11.5f);
    g.setFont(makeTechnicalFont(fontHeight, true));

    if (isBypass && active) {
        g.setColour(shouldDrawButtonAsHighlighted ? accentHover() : primaryAccent());
        auto textBounds = button.getLocalBounds().withTrimmedLeft(6).reduced(3, 1);
        g.drawFittedText(button.getButtonText(), textBounds, juce::Justification::centred, 1);
        return;
    }

    if (active) {
        g.setColour(displayBackground());
    } else {
        g.setColour(shouldDrawButtonAsHighlighted ? textOnPanel() : textSecondary());
    }

    const int topOffset = (button.getWidth() > 38 && button.getHeight() >= 20) ? 1 : 0;
    auto textBounds = button.getLocalBounds().reduced(4, 1).withTrimmedTop(topOffset);
    g.drawFittedText(button.getButtonText(), textBounds, juce::Justification::centred, 1);
}

void Dimension5LookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                         int buttonX, int buttonY, int buttonW, int buttonH,
                                         juce::ComboBox& box) {
    juce::ignoreUnused(buttonX, buttonY, buttonW, buttonH);
    using namespace Dimension5UI;

    const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.8f);
    const bool isHovered = box.isMouseOverOrDragging();
    const bool isCustomMode = (box.getText().trim().equalsIgnoreCase("CUSTOM"));

    g.setColour(isButtonDown ? panelRecessed() : panelBackground());
    g.fillRoundedRectangle(bounds, 4.0f);

    // Subtle recessed value window inside the graphite combo housing
    const auto innerRect = bounds.reduced(2.5f, 2.5f);
    g.setColour(displayBackground().withAlpha(0.55f));
    g.fillRoundedRectangle(innerRect, 2.8f);

    // Left accent index bar (orange when Custom mode or hovered, muted neutral otherwise)
    g.setColour((isCustomMode || isHovered) ? primaryAccent() : textMuted().withAlpha(0.55f));
    g.fillRoundedRectangle(bounds.getX() + 4.5f, bounds.getY() + 5.5f,
                           2.4f, std::max(6.0f, bounds.getHeight() - 11.0f), 1.0f);

    const juce::Colour borderCol = isCustomMode ? primaryAccent()
                                 : (isHovered ? panelBorderCrisp().brighter(0.15f) : panelBorder());
    g.setColour(borderCol);
    g.drawRoundedRectangle(bounds, 4.0f, isCustomMode ? 1.3f : 1.0f);

    // Crisp dropdown chevron
    const float arrowX = (float)width - 14.0f;
    const float arrowY = (float)height * 0.5f - 1.8f;
    juce::Path arrow;
    arrow.startNewSubPath(arrowX - 3.8f, arrowY);
    arrow.lineTo(arrowX + 3.8f, arrowY);
    arrow.lineTo(arrowX, arrowY + 4.4f);
    arrow.closeSubPath();

    g.setColour((isHovered || isCustomMode) ? primaryAccent() : textSecondary());
    g.fillPath(arrow);
}

juce::Font Dimension5LookAndFeel::getComboBoxFont(juce::ComboBox& box) {
    const float h = std::clamp((float)box.getHeight() * 0.45f, 10.5f, 12.2f);
    return makeTechnicalFont(h, true);
}

void Dimension5LookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(11, 1, box.getWidth() - 28, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

void Dimension5LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    using namespace Dimension5UI;
    const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
    g.setColour(panelBackground());
    g.fillRoundedRectangle(bounds, 4.5f);
    g.setColour(panelBorderCrisp());
    g.drawRoundedRectangle(bounds, 4.5f, 1.0f);
}

void Dimension5LookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                              bool isSeparator, bool isActive, bool isHighlighted,
                                              bool isTicked, bool hasSubMenu,
                                              const juce::String& text, const juce::String& shortcutKeyText,
                                              const juce::Drawable* icon, const juce::Colour* textColour) {
    juce::ignoreUnused(hasSubMenu, shortcutKeyText, icon);
    using namespace Dimension5UI;

    if (isSeparator) {
        auto r = area.reduced(6, 0);
        g.setColour(panelBorder());
        g.fillRect(r.removeFromTop(1));
        return;
    }

    const auto itemBounds = area.toFloat().reduced(3.0f, 1.5f);
    if (isHighlighted && isActive) {
        g.setColour(panelElevated());
        g.fillRoundedRectangle(itemBounds, 3.0f);
        g.setColour(primaryAccent());
        g.drawRoundedRectangle(itemBounds, 3.0f, 0.8f);
    }

    if (isTicked) {
        g.setColour(primaryAccent());
        g.fillRoundedRectangle(itemBounds.getX() + 4.0f, itemBounds.getCentreY() - 4.0f, 2.5f, 8.0f, 1.0f);
    }

    juce::Colour tc = textOnPanel();
    if (!isActive) {
        tc = textMuted().withAlpha(0.5f);
    } else if (isHighlighted || isTicked) {
        tc = primaryAccent();
    } else if (textColour != nullptr) {
        tc = *textColour;
    }

    g.setColour(tc);
    g.setFont(makeTechnicalFont(11.8f, isTicked || isHighlighted));
    g.drawText(text, area.reduced(12, 0), juce::Justification::centredLeft, true);
}

void Dimension5LookAndFeel::drawCornerResizer(juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging) {
    using namespace Dimension5UI;
    const juce::Colour c = (isMouseOver || isMouseDragging)
        ? primaryAccent()
        : textOnChassisMuted().withAlpha(0.75f);
    g.setColour(c);
    for (int i = 0; i < 3; ++i) {
        const float offset = 4.0f + (float)i * 4.0f;
        g.drawLine((float)w - offset, (float)h - 2.0f,
                   (float)w - 2.0f, (float)h - offset, 1.25f);
    }
}
