#pragma once

#include <JuceHeader.h>

namespace Dimension5UI {

// =============================================================================
// Driftbrigade Family Design System — Precision Analog Instrument
// Mandatory Core Palette: Off-White Chassis + Graphite Modules + Orange Accent
// =============================================================================

// 1. Chassis (Off-White Hardware Frame)
inline juce::Colour chassisBackground()  { return juce::Colour(0xF0, 0xED, 0xE6); } // #F0EDE6
inline juce::Colour chassisSurface()     { return juce::Colour(0xE6, 0xE2, 0xD8); } // #E6E2D8
inline juce::Colour chassisDivider()     { return juce::Colour(0xD4, 0xCF, 0xC3); } // #D4CFC3
inline juce::Colour chassisBorder()      { return juce::Colour(0xBC, 0xB6, 0xA8); } // #BCB6A8

// 2. Modules (Graphite Instrument Bays)
inline juce::Colour panelBackground()    { return juce::Colour(0x29, 0x2B, 0x2D); } // #292B2D
inline juce::Colour panelElevated()      { return juce::Colour(0x33, 0x36, 0x39); } // #333639
inline juce::Colour panelRecessed()      { return juce::Colour(0x21, 0x23, 0x25); } // #212325
inline juce::Colour panelBorder()        { return juce::Colour(0x3B, 0x3E, 0x42); } // #3B3E42
inline juce::Colour panelBorderCrisp()   { return juce::Colour(0x4D, 0x51, 0x56); } // #4D5156

// 3. Displays (Recessed Dark Telemetry Screens)
inline juce::Colour displayBackground()  { return juce::Colour(0x17, 0x19, 0x1B); } // #17191B
inline juce::Colour displayGrid()        { return juce::Colour(0x22, 0x25, 0x28); } // #222528
inline juce::Colour displayAxis()        { return juce::Colour(0x30, 0x34, 0x38); } // #303438

// 4. Single Chromatic Accent Family (Precision Orange)
inline juce::Colour primaryAccent()      { return juce::Colour(0xF3, 0x6B, 0x2B); } // #F36B2B
inline juce::Colour accentHover()        { return juce::Colour(0xF6, 0x82, 0x4B); } // #F6824B
inline juce::Colour accentPressed()      { return juce::Colour(0xD8, 0x5B, 0x20); } // #D85B20
inline juce::Colour accentHighlightDim() { return juce::Colour(0x7A, 0x34, 0x14); } // #7A3414

// 5. Machined Knob Hardware Surfaces (Strictly Neutral Grays)
inline juce::Colour knobBodyOuter()      { return juce::Colour(0x1D, 0x1F, 0x21); } // #1D1F21
inline juce::Colour knobBodyInner()      { return juce::Colour(0x23, 0x26, 0x28); } // #232628
inline juce::Colour knobCapTop()         { return juce::Colour(0x30, 0x33, 0x36); } // #303336
inline juce::Colour knobRing()           { return juce::Colour(0x40, 0x44, 0x48); } // #404448
inline juce::Colour knobTrack()          { return juce::Colour(0x17, 0x19, 0x1B); } // #17191B

// 6. Typography Tokens
inline juce::Colour textOnChassis()      { return juce::Colour(0x29, 0x2B, 0x2D); } // #292B2D
inline juce::Colour textOnChassisSec()   { return juce::Colour(0x59, 0x57, 0x53); } // #595753
inline juce::Colour textOnChassisMuted() { return juce::Colour(0x78, 0x75, 0x6E); } // #78756E

inline juce::Colour textOnPanel()        { return juce::Colour(0xF3, 0xF0, 0xE9); } // #F3F0E9
inline juce::Colour textSecondary()      { return juce::Colour(0xBA, 0xB6, 0xAD); } // #BAB6AD
inline juce::Colour textMuted()          { return juce::Colour(0x85, 0x82, 0x7A); } // #85827A

// Compatibility aliases mapped strictly to the new Off-White + Graphite + Orange tokens
inline juce::Colour bgColour()           { return chassisBackground(); }
inline juce::Colour surfaceColour()      { return panelBackground(); }
inline juce::Colour elevatedColour()     { return panelElevated(); }
inline juce::Colour dividerColour()      { return panelBorder(); }
inline juce::Colour primaryText()        { return textOnPanel(); }
inline juce::Colour secondaryText()      { return textSecondary(); }
inline juce::Colour spatialAccent()      { return primaryAccent(); }
inline juce::Colour analogAccent()       { return primaryAccent(); }
inline juce::Colour warningAccent()      { return primaryAccent(); }
inline juce::Colour successAccent()      { return primaryAccent(); }

} // namespace Dimension5UI

/**
 * Dimension5LookAndFeel
 *
 * Precision Analog Instrument aesthetic aligned with the Driftbrigade product family.
 * Features:
 * - Warm off-white hardware chassis with dark graphite instrument modules.
 * - Machined rotary knobs (Hero & Secondary tiers with 21-tick scales and orange arcs).
 * - Recessed linear sliders with orange fill, light metallic thumb, and technical value box.
 * - Tactile instrument switches and combo boxes with single-accent (#F36B2B) state coding.
 */
class Dimension5LookAndFeel : public juce::LookAndFeel_V4 {
public:
    Dimension5LookAndFeel();
    ~Dimension5LookAndFeel() override = default;

    static juce::Font makeTechnicalFont(float height, bool bold = false);

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override;

    juce::Label* createSliderTextBox(juce::Slider& slider) override;

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

    juce::Font getComboBoxFont(juce::ComboBox& box) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;

    void drawCornerResizer(juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging) override;
};
