#pragma once

#include <JuceHeader.h>

namespace Dimension5UI {

inline juce::Colour bgColour()         { return juce::Colour(0x10, 0x17, 0x1C); }
inline juce::Colour surfaceColour()    { return juce::Colour(0x18, 0x22, 0x28); }
inline juce::Colour elevatedColour()   { return juce::Colour(0x22, 0x31, 0x38); }
inline juce::Colour dividerColour()    { return juce::Colour(0x2D, 0x3E, 0x45); }
inline juce::Colour primaryText()      { return juce::Colour(0xE8, 0xEF, 0xEC); }
inline juce::Colour secondaryText()    { return juce::Colour(0x7E, 0x96, 0x9E); }
inline juce::Colour spatialAccent()    { return juce::Colour(0x5C, 0xE2, 0xCA); }
inline juce::Colour analogAccent()     { return juce::Colour(0xE2, 0xA6, 0x5A); }
inline juce::Colour warningAccent()    { return juce::Colour(0xE5, 0x6B, 0x55); }
inline juce::Colour successAccent()    { return juce::Colour(0x56, 0xD3, 0x8C); }

} // namespace Dimension5UI

/**
 * Dimension5LookAndFeel
 * 
 * Contemporary high-precision analog-digital studio instrument aesthetic.
 * Features:
 * - Bespoke rotary knobs (Hero & Precision tiers with calibrated arcs and glow).
 * - High-legibility illuminated sliders with recessed beds.
 * - Tactile instrument buttons with active states and bypass alerts.
 * - Sleek technical combo boxes with custom dropdown menus.
 */
class Dimension5LookAndFeel : public juce::LookAndFeel_V4 {
public:
    Dimension5LookAndFeel();
    ~Dimension5LookAndFeel() override = default;

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;
};
