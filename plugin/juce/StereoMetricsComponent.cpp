#include "StereoMetricsComponent.h"

StereoMetricsComponent::StereoMetricsComponent() {
    setOpaque(false);
}

void StereoMetricsComponent::updateMetrics(const StereoAnalysisFrame& frame) {
    currentFrame = frame;

    if (frame.hasSignal) {
        smoothedCorr += 0.25f * (frame.correlation - smoothedCorr);
        smoothedOutSmDb += 0.25f * (frame.sideMidRatioDb - smoothedOutSmDb);
        smoothedInSmDb += 0.25f * (frame.inputSideMidRatioDb - smoothedInSmDb);
        smoothedMonoAttenDb += 0.25f * (frame.monoAttenuationDb - smoothedMonoAttenDb);
        smoothedStereoDeltaDb += 0.25f * (frame.stereoDeltaDb - smoothedStereoDeltaDb);
    } else {
        // Smooth decay to rest on silence
        smoothedCorr += 0.35f * (1.0f - smoothedCorr);
        smoothedOutSmDb += 0.35f * (-60.0f - smoothedOutSmDb);
        smoothedInSmDb += 0.35f * (-60.0f - smoothedInSmDb);
        smoothedMonoAttenDb += 0.35f * (0.0f - smoothedMonoAttenDb);
        smoothedStereoDeltaDb += 0.35f * (0.0f - smoothedStereoDeltaDb);
    }
}

void StereoMetricsComponent::setBypassState(bool isBypassed) {
    bypassActive = isBypassed;
}

void StereoMetricsComponent::resized() {
    // Childless component; layout managed in paint
}

void StereoMetricsComponent::paint(juce::Graphics& g) {
    using namespace Dimension5UI;

    auto bounds = getLocalBounds().toFloat();
    const float cardSpacing = 8.0f;
    const float cardWidth = (bounds.getWidth() - cardSpacing * 3.0f) / 4.0f;

    // 1. CORRELATION Card
    {
        auto cardArea = bounds.removeFromLeft(cardWidth);
        bounds.removeFromLeft(cardSpacing);

        juce::String valStr = "--";
        juce::String badgeStr = "STANDBY";
        juce::Colour col = secondaryText();
        float meterVal = 0.0f;
        bool valid = false;

        if (currentFrame.hasSignal) {
            if (currentFrame.isCorrelationValid) {
                valid = true;
                meterVal = juce::jlimit(-1.0f, 1.0f, smoothedCorr);
                valStr = juce::String::formatted("%+.2f", (double)smoothedCorr);

                if (smoothedCorr >= 0.20f) {
                    badgeStr = "IN PHASE";
                    col = spatialAccent();
                } else if (smoothedCorr >= -0.05f) {
                    badgeStr = "WIDE / DECORR";
                    col = primaryText();
                } else if (smoothedCorr >= -0.25f) {
                    badgeStr = "PHASE CAUTION";
                    col = analogAccent();
                } else {
                    badgeStr = "ANTIPHASE ALERT";
                    col = warningAccent();
                }
            } else {
                if (currentFrame.correlationStatus == CorrelationStatus::LeftOnly) {
                    valStr = "L ONLY";
                    badgeStr = "MONO CHANNEL";
                } else if (currentFrame.correlationStatus == CorrelationStatus::RightOnly) {
                    valStr = "R ONLY";
                    badgeStr = "MONO CHANNEL";
                } else {
                    valStr = "N/A";
                    badgeStr = "UNDEFINED";
                }
                col = secondaryText();
            }
        }

        drawMetricCard(g, cardArea, "CORRELATION", valStr, badgeStr, col, meterVal, true, valid);
    }

    // 2. SIDE / MID Card
    {
        auto cardArea = bounds.removeFromLeft(cardWidth);
        bounds.removeFromLeft(cardSpacing);

        juce::String valStr = "--";
        juce::String badgeStr = "OUTPUT BALANCE";
        juce::Colour col = secondaryText();
        float meterVal = 0.0f;
        bool valid = false;

        if (currentFrame.hasSignal) {
            if (currentFrame.isSideMidValid) {
                valid = true;
                valStr = juce::String::formatted("%+.1f dB", (double)smoothedOutSmDb);
                col = spatialAccent();
                // Normalized meter for -30dB to +10dB
                meterVal = juce::jlimit(0.0f, 1.0f, (smoothedOutSmDb + 30.0f) / 40.0f);
            } else if (currentFrame.midRms > 1e-5f && currentFrame.sideRms <= 1e-5f) {
                valStr = "-inf dB";
                badgeStr = "PURE MONO";
                col = secondaryText();
            } else if (currentFrame.sideRms > 1e-5f && currentFrame.midRms <= 1e-5f) {
                valStr = "+inf dB";
                badgeStr = "PURE SIDE";
                col = warningAccent();
            }
        }

        drawMetricCard(g, cardArea, "SIDE / MID", valStr, badgeStr, col, meterVal, false, valid);
    }

    // 3. MONO SUM Card
    {
        auto cardArea = bounds.removeFromLeft(cardWidth);
        bounds.removeFromLeft(cardSpacing);

        juce::String valStr = "--";
        juce::String badgeStr = "MONO INTEGRITY";
        juce::Colour col = secondaryText();
        float meterVal = 0.0f;
        bool valid = false;

        if (currentFrame.hasSignal) {
            valid = true;
            valStr = juce::String::formatted("%+.1f dB", (double)smoothedMonoAttenDb);

            if (smoothedMonoAttenDb >= -1.0f) {
                badgeStr = "OPTIMAL";
                col = spatialAccent();
            } else if (smoothedMonoAttenDb >= -3.5f) {
                badgeStr = "NATURAL";
                col = primaryText();
            } else if (smoothedMonoAttenDb >= -7.0f) {
                badgeStr = "ATTENUATED";
                col = analogAccent();
            } else {
                badgeStr = "CANCELLATION";
                col = warningAccent();
            }

            // Attenuation from 0dB down to -12dB
            meterVal = juce::jlimit(0.0f, 1.0f, 1.0f + (smoothedMonoAttenDb / 12.0f));
        }

        drawMetricCard(g, cardArea, "MONO SUM", valStr, badgeStr, col, meterVal, false, valid);
    }

    // 4. STEREO DELTA Card
    {
        auto cardArea = bounds;

        juce::String valStr = "--";
        juce::String badgeStr = bypassActive ? "BYPASSED" : "STEREO DELTA";
        juce::Colour col = secondaryText();
        float meterVal = 0.0f;
        bool valid = false;

        if (currentFrame.hasSignal) {
            if (bypassActive) {
                valStr = "0.0 dB";
                badgeStr = "BYPASSED";
                col = warningAccent();
            } else if (currentFrame.isStereoDeltaValid) {
                valid = true;
                valStr = juce::String::formatted("%+.1f dB", (double)smoothedStereoDeltaDb);

                if (smoothedStereoDeltaDb > 0.4f) {
                    badgeStr = "EXPANDED";
                    col = spatialAccent();
                } else if (smoothedStereoDeltaDb < -0.4f) {
                    badgeStr = "NARROWED";
                    col = analogAccent();
                } else {
                    badgeStr = "UNITY";
                    col = primaryText();
                }

                meterVal = juce::jlimit(-1.0f, 1.0f, smoothedStereoDeltaDb / 12.0f);
            } else {
                valStr = "N/A";
                badgeStr = "BASELINE MONO";
                col = secondaryText();
            }
        }

        drawMetricCard(g, cardArea, "STEREO DELTA", valStr, badgeStr, col, meterVal, true, valid);
    }
}

void StereoMetricsComponent::drawMetricCard(juce::Graphics& g, juce::Rectangle<float> bounds,
                                            const juce::String& title, const juce::String& valueText,
                                            const juce::String& badgeText, juce::Colour accentColour,
                                            float meterValue, bool isBipolar, bool isValid) {
    using namespace Dimension5UI;

    // Card background
    g.setColour(surfaceColour());
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(dividerColour());
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);

    auto contentArea = bounds.reduced(10.0f, 7.0f);

    // Title & Badge row
    auto headerRow = contentArea.removeFromTop(13.0f);
    g.setFont(juce::Font(9.5f, juce::Font::bold));
    g.setColour(secondaryText());
    g.drawText(title, headerRow.removeFromLeft(headerRow.getWidth() * 0.6f), juce::Justification::centredLeft);

    g.setFont(juce::Font(8.5f, juce::Font::bold));
    g.setColour(accentColour.withAlpha(0.85f));
    g.drawText(badgeText, headerRow, juce::Justification::centredRight);

    contentArea.removeFromTop(4.0f);

    // Primary Value Display
    auto valueRow = contentArea.removeFromTop(22.0f);
    g.setFont(juce::Font(18.0f, juce::Font::bold));
    g.setColour(isValid ? primaryText() : secondaryText().withAlpha(0.6f));
    g.drawText(valueText, valueRow, juce::Justification::centredLeft);

    contentArea.removeFromTop(4.0f);

    // Micro Graphical Indicator Bar
    auto barArea = contentArea.removeFromTop(6.0f);
    g.setColour(elevatedColour());
    g.fillRoundedRectangle(barArea, 2.0f);
    g.setColour(dividerColour());
    g.drawRoundedRectangle(barArea, 2.0f, 0.5f);

    if (isValid) {
        if (isBipolar) {
            const float midX = barArea.getCentreX();
            // Center zero tick
            g.setColour(dividerColour().brighter(0.2f));
            g.drawLine(midX, barArea.getY(), midX, barArea.getBottom(), 1.0f);

            const float halfW = barArea.getWidth() * 0.5f;
            if (meterValue >= 0.0f) {
                const float fillW = meterValue * halfW;
                auto fill = juce::Rectangle<float>(midX, barArea.getY() + 1.0f, fillW, barArea.getHeight() - 2.0f);
                g.setColour(accentColour);
                g.fillRoundedRectangle(fill, 1.5f);
            } else {
                const float fillW = -meterValue * halfW;
                auto fill = juce::Rectangle<float>(midX - fillW, barArea.getY() + 1.0f, fillW, barArea.getHeight() - 2.0f);
                g.setColour(accentColour);
                g.fillRoundedRectangle(fill, 1.5f);
            }
        } else {
            const float fillW = meterValue * barArea.getWidth();
            auto fill = juce::Rectangle<float>(barArea.getX(), barArea.getY() + 1.0f, fillW, barArea.getHeight() - 2.0f);
            g.setColour(accentColour);
            g.fillRoundedRectangle(fill, 1.5f);
        }
    }
}
