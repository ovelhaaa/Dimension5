#include "StereoMetricsComponent.h"
#include <cmath>

namespace {
float smoothingCoeff(double deltaSeconds, double tauSeconds) {
    const float dt = (float)juce::jlimit(0.001, 0.25, deltaSeconds);
    return 1.0f - std::exp(-dt / (float)juce::jmax(0.01, tauSeconds));
}
} // namespace

StereoMetricsComponent::StereoMetricsComponent() {
    setOpaque(false);
}

void StereoMetricsComponent::updateMetrics(const StereoAnalysisFrame& frame, double deltaSeconds) {
    currentFrame = frame;

    const float valueCoeff = smoothingCoeff(deltaSeconds, 0.12);

    if (frame.hasSignal) {
        if (frame.isCorrelationValid) {
            smoothedCorr += valueCoeff * (frame.correlation - smoothedCorr);
        }
        if (frame.isSideMidValid) {
            smoothedOutSmDb += valueCoeff * (frame.sideMidRatioDb - smoothedOutSmDb);
        }
        if (frame.stereoReferenceRms > 1.0e-5f) {
            smoothedMonoAttenDb += valueCoeff * (frame.monoAttenuationDb - smoothedMonoAttenDb);
        }
        if (frame.isStereoDeltaValid) {
            smoothedStereoDeltaDb += valueCoeff * (frame.stereoDeltaDb - smoothedStereoDeltaDb);
        }
    } else {
        // Values are not displayed in silence; relax them to neutral so that
        // the next valid measurement does not inherit stale text.
        const float restCoeff = smoothingCoeff(deltaSeconds, 0.06);
        smoothedCorr += restCoeff * (1.0f - smoothedCorr);
        smoothedOutSmDb += restCoeff * (-60.0f - smoothedOutSmDb);
        smoothedMonoAttenDb += restCoeff * (0.0f - smoothedMonoAttenDb);
        smoothedStereoDeltaDb += restCoeff * (0.0f - smoothedStereoDeltaDb);
    }
}

void StereoMetricsComponent::setBypassState(bool isBypassed) {
    bypassActive = isBypassed;
}

void StereoMetricsComponent::resized() {
    // Childless component; layout is computed in paint().
}

void StereoMetricsComponent::paint(juce::Graphics& g) {
    using namespace Dimension5UI;

    auto bounds = getLocalBounds().toFloat();
    const float cardSpacing = 7.0f;
    const float cardWidth = (bounds.getWidth() - cardSpacing * 3.0f) / 4.0f;

    // 1. CORRELATION
    {
        auto cardArea = bounds.removeFromLeft(cardWidth);
        bounds.removeFromLeft(cardSpacing);

        juce::String valStr = "--";
        juce::String badgeStr = "SILENT";
        juce::Colour col = secondaryText();
        float meterVal = 0.0f;
        bool valid = false;

        if (currentFrame.hasSignal) {
            if (currentFrame.isCorrelationValid) {
                valid = true;
                meterVal = juce::jlimit(-1.0f, 1.0f, smoothedCorr);
                valStr = juce::String::formatted("%+.2f", (double)smoothedCorr);

                if (smoothedCorr >= 0.60f) {
                    badgeStr = "IN PHASE";
                    col = spatialAccent();
                } else if (smoothedCorr >= 0.15f) {
                    badgeStr = "CORRELATED";
                    col = spatialAccent();
                } else if (smoothedCorr > -0.15f) {
                    badgeStr = "DECORRELATED";
                    col = primaryText();
                } else if (smoothedCorr > -0.60f) {
                    badgeStr = "PHASE RISK";
                    col = analogAccent();
                } else {
                    badgeStr = "ANTIPHASE";
                    col = warningAccent();
                }
            } else {
                if (currentFrame.correlationStatus == CorrelationStatus::LeftOnly) {
                    badgeStr = "LEFT ONLY";
                } else if (currentFrame.correlationStatus == CorrelationStatus::RightOnly) {
                    badgeStr = "RIGHT ONLY";
                } else {
                    badgeStr = "UNDEFINED";
                }
                col = secondaryText();
            }
        }

        drawMetricCard(g, cardArea, "CORRELATION", valStr, badgeStr, col, meterVal, true, valid);
    }

    // 2. SIDE / MID
    {
        auto cardArea = bounds.removeFromLeft(cardWidth);
        bounds.removeFromLeft(cardSpacing);

        juce::String valStr = "--";
        juce::String badgeStr = "SILENT";
        juce::Colour col = secondaryText();
        float meterVal = 0.0f;
        bool valid = false;

        if (currentFrame.hasSignal) {
            if (currentFrame.isSideMidValid) {
                valid = true;
                valStr = juce::String::formatted("%+.1f dB", (double)smoothedOutSmDb);
                badgeStr = "OUTPUT";
                col = spatialAccent();
                meterVal = juce::jlimit(0.0f, 1.0f, (smoothedOutSmDb + 30.0f) / 40.0f);
            } else if (currentFrame.midRms > 1.0e-5f && currentFrame.sideRms <= 1.0e-5f) {
                badgeStr = "MONO";
            } else if (currentFrame.sideRms > 1.0e-5f && currentFrame.midRms <= 1.0e-5f) {
                badgeStr = "SIDE ONLY";
                col = warningAccent();
            } else {
                badgeStr = "N/A";
            }
        }

        drawMetricCard(g, cardArea, "SIDE / MID", valStr, badgeStr, col, meterVal, false, valid);
    }

    // 3. MONO SUM
    {
        auto cardArea = bounds.removeFromLeft(cardWidth);
        bounds.removeFromLeft(cardSpacing);

        juce::String valStr = "--";
        juce::String badgeStr = "SILENT";
        juce::Colour col = secondaryText();
        float meterVal = 0.0f;
        bool valid = false;

        if (currentFrame.hasSignal && currentFrame.stereoReferenceRms > 1.0e-5f) {
            valid = true;
            valStr = juce::String::formatted("%+.1f dB", (double)smoothedMonoAttenDb);

            if (smoothedMonoAttenDb >= -1.0f) {
                badgeStr = "STABLE";
                col = spatialAccent();
            } else if (smoothedMonoAttenDb >= -3.5f) {
                badgeStr = "MILD LOSS";
                col = primaryText();
            } else if (smoothedMonoAttenDb >= -7.0f) {
                badgeStr = "ATTENUATION";
                col = analogAccent();
            } else {
                badgeStr = "CANCEL RISK";
                col = warningAccent();
            }

            meterVal = juce::jlimit(0.0f, 1.0f, 1.0f + (smoothedMonoAttenDb / 12.0f));
        }

        drawMetricCard(g, cardArea, "MONO SUM", valStr, badgeStr, col, meterVal, false, valid);
    }

    // 4. STEREO DELTA
    {
        auto cardArea = bounds;

        juce::String valStr = "--";
        juce::String badgeStr = "SILENT";
        juce::Colour col = secondaryText();
        float meterVal = 0.0f;
        bool valid = false;

        if (currentFrame.hasSignal) {
            if (bypassActive) {
                badgeStr = "BYPASS";
                col = warningAccent();
                if (currentFrame.isStereoDeltaValid) {
                    valStr = juce::String::formatted("%+.1f dB", (double)currentFrame.stereoDeltaDb);
                }
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
                constexpr float kFloor = 1.0e-5f;
                valStr = "N/A";
                if (!currentFrame.isInputSideMidValid) {
                    if (currentFrame.inputSideRms > kFloor && currentFrame.inputMidRms <= kFloor) {
                        badgeStr = "FROM SIDE";
                    } else if (currentFrame.inputMidRms > kFloor && currentFrame.inputSideRms <= kFloor) {
                        badgeStr = "FROM MONO";
                    } else {
                        badgeStr = "INPUT N/A";
                    }
                } else if (currentFrame.sideRms > kFloor && currentFrame.midRms <= kFloor) {
                    badgeStr = "SIDE ONLY";
                } else {
                    badgeStr = "LIMITED";
                }
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

    g.setColour(surfaceColour().brighter(0.015f));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(dividerColour());
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);

    auto content = bounds.reduced(9.0f, 5.0f);
    if (content.getHeight() < 40.0f || content.getWidth() < 40.0f) {
        return;
    }

    const bool compact = content.getWidth() < 112.0f;
    const float titleFont = juce::jlimit(8.2f, 10.2f, content.getWidth() * 0.075f);
    const float statusFont = juce::jlimit(7.8f, 9.5f, content.getWidth() * 0.068f);
    const float valueFont = juce::jlimit(compact ? 15.0f : 17.0f, 22.0f, content.getWidth() * 0.16f);

    // Row 1: title + status dot
    auto titleRow = content.removeFromTop(compact ? 11.0f : 12.0f);
    g.setFont(juce::Font(titleFont, juce::Font::bold));
    g.setColour(secondaryText());
    g.drawText(title, titleRow.removeFromLeft(titleRow.getWidth() - 12.0f),
               juce::Justification::centredLeft, false);

    const float dotR = 3.0f;
    g.setColour(accentColour.withAlpha(isValid ? 0.9f : 0.45f));
    g.fillEllipse(titleRow.getRight() - dotR * 2.0f, titleRow.getCentreY() - dotR, dotR * 2.0f, dotR * 2.0f);

    // Bottom rows: status, bar, value
    auto statusRow = content.removeFromBottom(compact ? 11.0f : 12.0f);
    content.removeFromBottom(3.0f);
    auto barArea = content.removeFromBottom(5.0f);
    content.removeFromBottom(3.0f);

    g.setFont(juce::Font(statusFont, juce::Font::bold));
    g.setColour(accentColour.withAlpha(isValid ? 0.95f : 0.65f));
    g.drawText(badgeText, statusRow, juce::Justification::centredLeft, false);

    // Graphical indicator
    g.setColour(elevatedColour());
    g.fillRoundedRectangle(barArea, 2.0f);
    g.setColour(dividerColour());
    g.drawRoundedRectangle(barArea, 2.0f, 0.5f);

    if (isValid) {
        if (isBipolar) {
            const float midX = barArea.getCentreX();
            g.setColour(dividerColour().brighter(0.25f));
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
            auto fill = juce::Rectangle<float>(barArea.getX(), barArea.getY() + 1.0f,
                                               juce::jmax(0.0f, fillW), barArea.getHeight() - 2.0f);
            g.setColour(accentColour);
            g.fillRoundedRectangle(fill, 1.5f);
        }
    }

    // Numeric value: omitted entirely when the measurement is invalid.
    g.setFont(juce::Font(valueFont, juce::Font::bold));
    g.setColour(isValid ? primaryText() : secondaryText().withAlpha(0.60f));
    g.drawText(valueText, content, juce::Justification::centredLeft, false);
}
