#include "StereoFieldComponent.h"
#include <cmath>

namespace {
constexpr float kTwoPi = juce::MathConstants<float>::twoPi;
constexpr float kCloudHalfLifeSec = 0.20f;
constexpr float kTargetVisualPeak = 0.92f;
constexpr float kMaxVisualGain = 48.0f;
constexpr float kMinVisualScale = 0.45f;
constexpr float kVisualNoiseFloor = 0.0020f;
constexpr float kScaleAttackSec = 0.055f;
constexpr float kScaleReleaseSec = 0.22f;
constexpr float kPointRemovalAge = 0.05f;
constexpr float kVectorMinAge = 0.08f;

float smoothScale(float current, float target, float attackSec, float releaseSec, float dt) {
    if (!std::isfinite(current) || !std::isfinite(target)) {
        return 1.0f;
    }
    const float tau = (target < current) ? attackSec : releaseSec;
    const float coeff = 1.0f - std::exp(-dt / juce::jmax(1.0e-3f, tau));
    return current + coeff * (target - current);
}

float targetScaleForPeak(float peak) {
    // Do not amplify below the noise floor: this prevents noise from being
    // visually blown up while still making low-level musical signals legible.
    if (!(peak > kVisualNoiseFloor)) {
        return 1.0f;
    }
    return juce::jlimit(kMinVisualScale, kMaxVisualGain, kTargetVisualPeak / peak);
}
} // namespace

StereoFieldComponent::StereoFieldComponent() {
    setOpaque(false);

    auto setupModeButton = [this](juce::TextButton& btn, const juce::String& text, ViewMode mode) {
        btn.setButtonText(text);
        btn.setClickingTogglesState(false);
        btn.onClick = [this, mode] { setViewMode(mode); };
        addAndMakeVisible(btn);
    };

    setupModeButton(fieldModeButton, "FIELD", ViewMode::Field);
    setupModeButton(vectorModeButton, "VECTOR", ViewMode::Vector);
    setupModeButton(deltaModeButton, "DELTA", ViewMode::Delta);

    addAndMakeVisible(metricsComponent);

    updateModeButtons();
}

void StereoFieldComponent::setViewMode(ViewMode mode) {
    if (currentMode != mode) {
        currentMode = mode;
        updateModeButtons();
        repaint();
    }
}

void StereoFieldComponent::updateModeButtons() {
    fieldModeButton.setToggleState(currentMode == ViewMode::Field, juce::dontSendNotification);
    vectorModeButton.setToggleState(currentMode == ViewMode::Vector, juce::dontSendNotification);
    deltaModeButton.setToggleState(currentMode == ViewMode::Delta, juce::dontSendNotification);
}

void StereoFieldComponent::setBypassState(bool isBypassed) {
    if (bypassActive != isBypassed) {
        bypassActive = isBypassed;
        repaint();
    }
    metricsComponent.setBypassState(isBypassed);
}

void StereoFieldComponent::resetDisplayState() {
    for (auto& pt : points) {
        pt.age = 0.0f;
        pt.sequence = 0;
    }
    pointWritePos = 0;
    pointCount = 0;
    lastIngestedSequence = 0;
    lastUpdateTimeMs = 0.0;
    displayScale = 1.0f;
    deltaDisplayScale = 1.0f;
    repaint();
}

void StereoFieldComponent::updateTelemetry(const StereoTelemetrySnapshot& snapshot) {
    lastTelemetry = snapshot;
    hasSignal = snapshot.frame.hasSignal;

    const double now = juce::Time::getMillisecondCounterHiRes();
    double dtSec = (lastUpdateTimeMs > 0.0) ? (now - lastUpdateTimeMs) * 0.001 : 0.033;
    lastUpdateTimeMs = now;
    dtSec = juce::jlimit(0.001, 0.25, dtSec);
    const float dt = static_cast<float>(dtSec);

    // Time-based motion phase: independent from the repaint rate.
    motionPhase += kTwoPi * juce::jmax(0.0f, paramRateHz) * dt;
    if (motionPhase >= kTwoPi) {
        motionPhase = std::fmod(motionPhase, kTwoPi);
    }

    // Perceptual persistence: exponential half-life, independent from FPS.
    const float decay = std::exp(-std::log(2.0f) * dt / kCloudHalfLifeSec);
    decayPoints(decay);

    ingestComparisonPoints(snapshot);
    updateVisualScale(snapshot, dtSec);

    metricsComponent.updateMetrics(snapshot.frame, dtSec);
}

void StereoFieldComponent::decayPoints(float decayFactor) {
    for (auto& pt : points) {
        if (pt.age > 0.0f) {
            pt.age *= decayFactor;
            if (pt.age < kPointRemovalAge) {
                pt.age = 0.0f;
                pt.sequence = 0;
            }
        }
    }
}

void StereoFieldComponent::ingestComparisonPoints(const StereoTelemetrySnapshot& snapshot) {
    const uint32_t n = juce::jmin<uint32_t>(snapshot.comparisonCount,
                                            (uint32_t)StereoTelemetrySnapshot::kMaxComparisonPoints);
    if (n == 0) {
        return;
    }

    const uint64_t endSequence = snapshot.comparisonEndSequence;

    // In silence the analyzer still produces zero-valued samples. They are
    // real, but drawing them would create a false point at the origin, so we
    // advance the sequence without ingesting them.
    if (!snapshot.frame.hasSignal) {
        lastIngestedSequence = endSequence;
        return;
    }
    uint32_t toIngest = 0;

    if (lastIngestedSequence == 0 || endSequence < lastIngestedSequence || endSequence == 0) {
        // First update or analyzer reset: take the whole available window.
        toIngest = n;
    } else if (endSequence > lastIngestedSequence) {
        const uint64_t fresh = endSequence - lastIngestedSequence;
        toIngest = (uint32_t)juce::jmin<uint64_t>(fresh, (uint64_t)n);
    }

    if (toIngest > 0) {
        const uint32_t start = n - toIngest;
        for (uint32_t i = start; i < n; ++i) {
            const auto& src = snapshot.comparisonPoints[i];
            auto& dst = points[pointWritePos];
            dst.inputSide = src.inputSide;
            dst.inputMid = src.inputMid;
            dst.outputSide = src.outputSide;
            dst.outputMid = src.outputMid;
            dst.age = 1.0f;
            dst.sequence = endSequence - (uint64_t)(n - 1 - i);

            pointWritePos = (pointWritePos + 1) % kMaxPoints;
            if (pointCount < kMaxPoints) {
                ++pointCount;
            }
        }
    }

    lastIngestedSequence = endSequence;
}

void StereoFieldComponent::updateVisualScale(const StereoTelemetrySnapshot& snapshot, double deltaSeconds) {
    float outputPeak = 0.0f;
    float combinedPeak = 0.0f;

    for (size_t k = 0; k < pointCount; ++k) {
        const auto& pt = points[chronologicalIndex(k)];
        if (pt.age <= kPointRemovalAge) {
            continue;
        }
        outputPeak = juce::jmax(outputPeak, std::abs(pt.outputMid), std::abs(pt.outputSide));
        const float inputPointPeak = juce::jmax(std::abs(pt.inputMid), std::abs(pt.inputSide));
        combinedPeak = juce::jmax(combinedPeak, outputPeak, inputPointPeak);
    }

    if (outputPeak <= 1.0e-6f) {
        outputPeak = juce::jmax(snapshot.frame.outputPeakL, snapshot.frame.outputPeakR);
    }
    if (combinedPeak <= 1.0e-6f) {
        combinedPeak = juce::jmax(outputPeak, snapshot.frame.inputPeakL, snapshot.frame.inputPeakR);
    }

    const float dt = static_cast<float>(deltaSeconds);
    const float outTarget = targetScaleForPeak(outputPeak);
    const float combinedTarget = targetScaleForPeak(combinedPeak);

    displayScale = smoothScale(displayScale, outTarget, kScaleAttackSec, kScaleReleaseSec, dt);
    deltaDisplayScale = smoothScale(deltaDisplayScale, combinedTarget, kScaleAttackSec, kScaleReleaseSec, dt);

    displayScale = juce::jlimit(kMinVisualScale, kMaxVisualGain, displayScale);
    deltaDisplayScale = juce::jlimit(kMinVisualScale, kMaxVisualGain, deltaDisplayScale);
}

void StereoFieldComponent::updateParameterGeometry(float spread, float depthMs, float rateHz, float mix) {
    paramSpread = spread;
    paramDepthMs = depthMs;
    paramRateHz = rateHz;
    paramMix = mix;
}

uint32_t StereoFieldComponent::getActiveOutputPointCount() const noexcept {
    uint32_t n = 0;
    for (size_t k = 0; k < pointCount; ++k) {
        if (points[chronologicalIndex(k)].age > kPointRemovalAge) {
            ++n;
        }
    }
    return n;
}

uint32_t StereoFieldComponent::getActiveInputPointCount() const noexcept {
    uint32_t n = 0;
    for (size_t k = 0; k < pointCount; ++k) {
        const auto& pt = points[chronologicalIndex(k)];
        if (pt.age > kPointRemovalAge &&
            (std::abs(pt.inputMid) > 1.0e-7f || std::abs(pt.inputSide) > 1.0e-7f)) {
            ++n;
        }
    }
    return n;
}

uint32_t StereoFieldComponent::getVectorSegmentCount() const noexcept {
    uint32_t segments = 0;
    uint64_t previousSequence = 0;
    bool hasPrevious = false;

    for (size_t k = 0; k < pointCount; ++k) {
        const auto& pt = points[chronologicalIndex(k)];
        if (pt.age <= kVectorMinAge) {
            hasPrevious = false;
            continue;
        }
        if (hasPrevious && pt.sequence == previousSequence + 1) {
            ++segments;
        }
        previousSequence = pt.sequence;
        hasPrevious = true;
    }
    return segments;
}

void StereoFieldComponent::resized() {
    auto bounds = getLocalBounds();

    headerBounds = bounds.removeFromTop(34);
    auto modeButtonsArea = headerBounds.removeFromRight(juce::jlimit(156, 214, headerBounds.getWidth() / 2));
    modeButtonsArea = modeButtonsArea.reduced(0, 4);
    const int btnW = modeButtonsArea.getWidth() / 3;
    fieldModeButton.setBounds(modeButtonsArea.removeFromLeft(btnW).reduced(2, 0));
    vectorModeButton.setBounds(modeButtonsArea.removeFromLeft(btnW).reduced(2, 0));
    deltaModeButton.setBounds(modeButtonsArea.reduced(2, 0));

    const int metricsHeight = juce::jlimit(86, 108, juce::roundToInt((float)bounds.getHeight() * 0.23f));
    metricsBounds = bounds.removeFromBottom(metricsHeight);
    metricsComponent.setBounds(metricsBounds.reduced(8, 3));

    scopeBounds = bounds;
}

void StereoFieldComponent::paint(juce::Graphics& g) {
    using namespace Dimension5UI;

    auto totalArea = getLocalBounds().toFloat();

    g.setColour(surfaceColour());
    g.fillRoundedRectangle(totalArea, 6.0f);
    g.setColour(dividerColour());
    g.drawRoundedRectangle(totalArea, 6.0f, 1.0f);

    g.setColour(dividerColour());
    g.drawLine(totalArea.getX() + 8.0f, (float)headerBounds.getBottom(),
               totalArea.getRight() - 8.0f, (float)headerBounds.getBottom(), 1.0f);

    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(secondaryText());
    auto titleArea = headerBounds.toFloat().reduced(12.0f, 0.0f);
    g.drawText("STEREO FIELD MONITOR 2.1", titleArea.removeFromLeft(196.0f), juce::Justification::centredLeft);

    juce::String modeDesc;
    if (currentMode == ViewMode::Field)       modeDesc = "FIELD / Measured Phosphor Cloud";
    else if (currentMode == ViewMode::Vector) modeDesc = "VECTOR / Continuous Lissajous";
    else                                      modeDesc = "DELTA / True Input vs Output";

    if (titleArea.getWidth() > 110.0f) {
        g.setFont(juce::Font(9.5f, juce::Font::plain));
        g.setColour(secondaryText().withAlpha(0.6f));
        g.drawText(modeDesc, titleArea, juce::Justification::centredLeft);
    }

    g.setColour(dividerColour());
    g.drawLine(totalArea.getX() + 8.0f, (float)metricsBounds.getY(),
               totalArea.getRight() - 8.0f, (float)metricsBounds.getY(), 1.0f);

    auto scopeArea = scopeBounds.toFloat().reduced(8.0f, 6.0f);

    if (currentMode == ViewMode::Field) {
        drawFieldMode(g, scopeArea);
    } else if (currentMode == ViewMode::Vector) {
        drawVectorMode(g, scopeArea);
    } else {
        drawDeltaMode(g, scopeArea);
    }
}

// ==============================================================================
// 1. FIELD MODE: Measured Phosphor Cloud + Parametric Configuration Aperture
// ==============================================================================
void StereoFieldComponent::drawFieldMode(juce::Graphics& g, juce::Rectangle<float> area) {
    using namespace Dimension5UI;

    const auto center = area.getCentre();
    const float radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.47f;

    drawReticle(g, center, radius, true);

    if (!bypassActive) {
        drawAuxiliaryAperture(g, center, radius);
    }

    // A small, explicit label: this is a configuration guide, never a measurement.
    if (!bypassActive) {
        g.setFont(juce::Font(8.0f, juce::Font::plain));
        g.setColour(secondaryText().withAlpha(0.38f));
        g.drawText("CONFIG GUIDE", area.getX() + 4.0f, area.getY() + 2.0f,
                   90.0f, 10.0f, juce::Justification::centredLeft);
    }

    const float scale = radius * 0.92f;
    const auto accent = spatialAccent();
    const float scaleFactor = displayScale;

    for (size_t k = 0; k < pointCount; ++k) {
        const auto& pt = points[chronologicalIndex(k)];
        if (pt.age <= kPointRemovalAge) {
            continue;
        }

        const float px = center.x + juce::jlimit(-1.20f, 1.20f, pt.outputSide * scaleFactor) * scale;
        const float py = center.y - juce::jlimit(-1.20f, 1.20f, pt.outputMid * scaleFactor) * scale;

        // Discrete halo + small bright core: density and amplitude stay distinct.
        g.setColour(accent.withAlpha(0.05f + 0.16f * pt.age));
        g.fillEllipse(px - 3.0f, py - 3.0f, 6.0f, 6.0f);

        g.setColour(accent.withAlpha(0.30f + 0.60f * pt.age));
        g.fillEllipse(px - 1.05f, py - 1.05f, 2.1f, 2.1f);

        if (pt.age > 0.72f) {
            g.setColour(primaryText().withAlpha(0.55f * (pt.age - 0.72f) / 0.28f));
            g.fillEllipse(px - 0.55f, py - 0.55f, 1.1f, 1.1f);
        }
    }

    if (getActiveOutputPointCount() == 0 && !hasSignal) {
        g.setFont(juce::Font(10.5f, juce::Font::bold));
        g.setColour(secondaryText().withAlpha(0.55f));
        g.drawText("AWAITING SIGNAL",
                   center.x - 100.0f, center.y + radius * 0.52f,
                   200.0f, 16.0f, juce::Justification::centred);
    }

    if (bypassActive) {
        drawBypassBadge(g, area);
    }

    drawAutoScaleBadge(g, area, scaleFactor);
}

// ==============================================================================
// 2. VECTOR MODE: Continuous Lissajous, no invented segments
// ==============================================================================
juce::Path StereoFieldComponent::buildVectorPath(juce::Rectangle<float> area) const {
    const auto center = area.getCentre();
    const float radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.47f;
    const float scale = radius * 0.92f;

    juce::Path path;
    bool hasPrevious = false;
    uint64_t previousSequence = 0;

    auto moveTo = [&path](float x, float y) { path.startNewSubPath(x, y); };

    for (size_t k = 0; k < pointCount; ++k) {
        const auto& pt = points[chronologicalIndex(k)];
        if (pt.age <= kVectorMinAge) {
            hasPrevious = false;
            continue;
        }

        const float px = center.x + juce::jlimit(-1.20f, 1.20f, pt.outputSide * displayScale) * scale;
        const float py = center.y - juce::jlimit(-1.20f, 1.20f, pt.outputMid * displayScale) * scale;

        if (!hasPrevious) {
            moveTo(px, py);
        } else if (pt.sequence == previousSequence + 1) {
            // Only connect samples with known consecutive acquisition indices.
            path.lineTo(px, py);
        } else {
            moveTo(px, py);
        }

        previousSequence = pt.sequence;
        hasPrevious = true;
    }

    return path;
}

void StereoFieldComponent::drawVectorMode(juce::Graphics& g, juce::Rectangle<float> area) {
    using namespace Dimension5UI;

    const auto center = area.getCentre();
    const float radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.47f;

    drawReticle(g, center, radius, true);

    g.setColour(dividerColour().withAlpha(0.30f));
    g.drawEllipse(center.x - radius * 0.75f, center.y - radius * 0.75f, radius * 1.5f, radius * 1.5f, 0.75f);
    g.drawEllipse(center.x - radius * 0.50f, center.y - radius * 0.50f, radius * 1.0f, radius * 1.0f, 0.75f);
    g.drawEllipse(center.x - radius * 0.25f, center.y - radius * 0.25f, radius * 0.5f, radius * 0.5f, 0.75f);

    g.setFont(juce::Font(8.5f, juce::Font::plain));
    g.setColour(secondaryText().withAlpha(0.45f));
    g.drawText("-6 dB", center.x + 4.0f, center.y - radius * 0.75f - 9.0f, 35.0f, 10.0f, juce::Justification::centredLeft);
    g.drawText("-12 dB", center.x + 4.0f, center.y - radius * 0.50f - 9.0f, 35.0f, 10.0f, juce::Justification::centredLeft);

    if (getVectorSegmentCount() == 0) {
        if (!hasSignal) {
            g.setFont(juce::Font(10.5f, juce::Font::bold));
            g.setColour(secondaryText().withAlpha(0.55f));
            g.drawText("VECTOR SCOPE IDLE",
                       center.x - 100.0f, center.y + radius * 0.52f,
                       200.0f, 16.0f, juce::Justification::centred);
        }
        return;
    }

    const auto path = buildVectorPath(area);

    g.setColour(spatialAccent().withAlpha(0.18f));
    g.strokePath(path, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(spatialAccent().withAlpha(0.82f));
    g.strokePath(path, juce::PathStrokeType(1.05f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (bypassActive) {
        drawBypassBadge(g, area);
    }
    drawAutoScaleBadge(g, area, displayScale);
}

// ==============================================================================
// 3. DELTA MODE: True Input vs Output from time-aligned measured samples
// ==============================================================================
void StereoFieldComponent::drawDeltaMode(juce::Graphics& g, juce::Rectangle<float> area) {
    using namespace Dimension5UI;

    auto working = area;
    const float panelW = working.getWidth() * 0.415f;
    auto leftArea = working.removeFromLeft(panelW);
    auto rightArea = working.removeFromRight(panelW);
    auto centerBadgeArea = working;

    const float radius = juce::jmax(18.0f, juce::jmin(leftArea.getWidth(), leftArea.getHeight()) * 0.44f);
    const auto centerL = leftArea.getCentre();
    const auto centerR = rightArea.getCentre();
    const float scale = radius * 0.92f;
    const auto amber = analogAccent();
    const auto teal = spatialAccent();

    drawReticle(g, centerL, radius, false);
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.setColour(amber);
    g.drawText("INPUT / BEFORE", leftArea.getX(), leftArea.getY() + 2.0f,
               leftArea.getWidth(), 14.0f, juce::Justification::centred);

    drawReticle(g, centerR, radius, false);
    g.setColour(teal);
    g.drawText("OUTPUT / AFTER", rightArea.getX(), rightArea.getY() + 2.0f,
               rightArea.getWidth(), 14.0f, juce::Justification::centred);

    const float deltaScaleFactor = deltaDisplayScale;

    for (size_t k = 0; k < pointCount; ++k) {
        const auto& pt = points[chronologicalIndex(k)];
        if (pt.age <= kPointRemovalAge) {
            continue;
        }

        // Input cloud (amber). Same common scale as the output cloud.
        {
            const float px = centerL.x + juce::jlimit(-1.20f, 1.20f, pt.inputSide * deltaScaleFactor) * scale;
            const float py = centerL.y - juce::jlimit(-1.20f, 1.20f, pt.inputMid * deltaScaleFactor) * scale;
            g.setColour(amber.withAlpha(0.04f + 0.13f * pt.age));
            g.fillEllipse(px - 2.8f, py - 2.8f, 5.6f, 5.6f);
            g.setColour(amber.withAlpha(0.28f + 0.55f * pt.age));
            g.fillEllipse(px - 1.0f, py - 1.0f, 2.0f, 2.0f);
        }

        // Output cloud (teal). Same common scale as the input cloud.
        {
            const float px = centerR.x + juce::jlimit(-1.20f, 1.20f, pt.outputSide * deltaScaleFactor) * scale;
            const float py = centerR.y - juce::jlimit(-1.20f, 1.20f, pt.outputMid * deltaScaleFactor) * scale;
            g.setColour(teal.withAlpha(0.04f + 0.13f * pt.age));
            g.fillEllipse(px - 2.8f, py - 2.8f, 5.6f, 5.6f);
            g.setColour(teal.withAlpha(0.28f + 0.55f * pt.age));
            g.fillEllipse(px - 1.0f, py - 1.0f, 2.0f, 2.0f);
        }
    }

    if (!hasSignal) {
        if (getActiveInputPointCount() == 0) {
            g.setFont(juce::Font(9.5f, juce::Font::plain));
            g.setColour(secondaryText().withAlpha(0.45f));
            g.drawText("NO INPUT", centerL.x - 50.0f, centerL.y + radius * 0.55f, 100.0f, 14.0f, juce::Justification::centred);
        }
        if (getActiveOutputPointCount() == 0) {
            g.setFont(juce::Font(9.5f, juce::Font::plain));
            g.setColour(secondaryText().withAlpha(0.45f));
            g.drawText("NO OUTPUT", centerR.x - 50.0f, centerR.y + radius * 0.55f, 100.0f, 14.0f, juce::Justification::centred);
        }
    }

    // Central transfer readout: measured Side/Mid delta in dB.
    {
        const auto badgeCenter = centerBadgeArea.getCentre();
        const float badgeW = juce::jmax(70.0f, centerBadgeArea.getWidth());

        g.setFont(juce::Font(8.5f, juce::Font::bold));
        g.setColour(secondaryText().withAlpha(0.75f));
        g.drawText("STEREO DELTA", badgeCenter.x - badgeW * 0.5f, badgeCenter.y - 34.0f,
                   badgeW, 12.0f, juce::Justification::centred);

        const auto& f = lastTelemetry.frame;
        juce::String valueStr = "--";
        juce::String statusStr = "N/A";
        juce::Colour valueColour = secondaryText();

        const bool deltaValid = f.hasSignal && f.isStereoDeltaValid;
        if (bypassActive) {
            statusStr = "BYPASS";
            valueColour = warningAccent();
            valueStr = deltaValid ? juce::String::formatted("%+.1f dB", (double)f.stereoDeltaDb) : juce::String("--");
        } else if (deltaValid) {
            valueStr = juce::String::formatted("%+.1f dB", (double)f.stereoDeltaDb);
            if (f.stereoDeltaDb > 0.4f) {
                statusStr = "EXPANDED";
                valueColour = spatialAccent();
            } else if (f.stereoDeltaDb < -0.4f) {
                statusStr = "NARROWED";
                valueColour = analogAccent();
            } else {
                statusStr = "UNITY";
                valueColour = primaryText();
            }
        } else if (f.hasSignal) {
            constexpr float kFloor = 1.0e-5f;
            valueStr = "N/A";
            if (!f.isInputSideMidValid) {
                if (f.inputSideRms > kFloor && f.inputMidRms <= kFloor) {
                    statusStr = "FROM SIDE";
                } else if (f.inputMidRms > kFloor && f.inputSideRms <= kFloor) {
                    statusStr = "FROM MONO";
                } else {
                    statusStr = "INPUT N/A";
                }
            } else if (f.sideRms > kFloor && f.midRms <= kFloor) {
                statusStr = "SIDE ONLY";
            } else {
                statusStr = "LIMITED";
            }
            valueColour = secondaryText();
        }

        g.setFont(juce::Font(14.5f, juce::Font::bold));
        g.setColour(valueColour);
        g.drawText(valueStr, badgeCenter.x - badgeW * 0.5f, badgeCenter.y - 20.0f,
                   badgeW, 20.0f, juce::Justification::centred);

        g.setFont(juce::Font(8.5f, juce::Font::bold));
        g.setColour(valueColour.withAlpha(0.85f));
        g.drawText(statusStr, badgeCenter.x - badgeW * 0.5f, badgeCenter.y + 1.0f,
                   badgeW, 12.0f, juce::Justification::centred);

        if (std::abs(deltaScaleFactor - 1.0f) > 0.06f) {
            g.setFont(juce::Font(7.5f, juce::Font::plain));
            g.setColour(secondaryText().withAlpha(0.55f));
            g.drawText(juce::String::formatted("COMMON SCALE x%.1f", (double)deltaScaleFactor),
                       badgeCenter.x - badgeW * 0.5f, badgeCenter.y + 15.0f,
                       badgeW, 10.0f, juce::Justification::centred);
        }
    }

    if (bypassActive) {
        drawBypassBadge(g, area);
    }
}

// ==============================================================================
// Common Reticle and Guides
// ==============================================================================
void StereoFieldComponent::drawReticle(juce::Graphics& g, juce::Point<float> center, float radius, bool showDiagonals) {
    using namespace Dimension5UI;

    g.setColour(bgColour());
    g.fillEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour(dividerColour().withAlpha(0.85f));
    g.drawEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);

    g.setColour(dividerColour().withAlpha(0.55f));
    g.drawLine(center.x, center.y - radius, center.x, center.y + radius, 1.0f);
    g.drawLine(center.x - radius, center.y, center.x + radius, center.y, 1.0f);

    if (showDiagonals) {
        const float diag = radius * 0.7071f;
        juce::Path diagPath;
        diagPath.startNewSubPath(center.x - diag, center.y - diag);
        diagPath.lineTo(center.x + diag, center.y + diag);
        diagPath.startNewSubPath(center.x - diag, center.y + diag);
        diagPath.lineTo(center.x + diag, center.y - diag);

        float dashLengths[2] = { 3.0f, 4.0f };
        juce::Path dashedDiag;
        juce::PathStrokeType(0.75f).createDashedStroke(dashedDiag, diagPath, dashLengths, 2);
        g.setColour(dividerColour().withAlpha(0.28f));
        g.fillPath(dashedDiag);

        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.setColour(secondaryText().withAlpha(0.70f));
        g.drawText("M", center.x - 10.0f, center.y - radius - 2.0f, 20.0f, 12.0f, juce::Justification::centred);
        g.drawText("+S", center.x + radius - 18.0f, center.y - 12.0f, 22.0f, 12.0f, juce::Justification::centred);
        g.drawText("-S", center.x - radius - 4.0f, center.y - 12.0f, 22.0f, 12.0f, juce::Justification::centred);
        g.drawText("L", center.x + diag - 6.0f, center.y - diag - 12.0f, 16.0f, 12.0f, juce::Justification::centred);
        g.drawText("R", center.x - diag - 10.0f, center.y - diag - 12.0f, 16.0f, 12.0f, juce::Justification::centred);
    }
}

// ==============================================================================
// Configuration aperture: a smooth superellipse, explicitly a config guide
// ==============================================================================
void StereoFieldComponent::drawAuxiliaryAperture(juce::Graphics& g, juce::Point<float> center, float radius) {
    using namespace Dimension5UI;

    const float spreadNorm = juce::jlimit(0.0f, 1.0f, paramSpread / 2.0f);
    const float depthNorm = juce::jlimit(0.0f, 1.0f, paramDepthMs / 4.0f);
    const float mixNorm = juce::jlimit(0.0f, 1.0f, paramMix);

    const float baseWidth = (0.22f + 0.48f * spreadNorm) * (0.35f + 0.65f * mixNorm);
    const float breathe = 0.030f * depthNorm * std::sin(motionPhase * 2.0f);
    const float effWidth = juce::jlimit(0.10f, 0.96f, baseWidth + breathe);
    const float effHeight = juce::jlimit(0.22f, 0.90f, 0.48f + 0.38f * mixNorm);

    const float rx = radius * effWidth;
    const float ry = radius * effHeight;

    juce::Path aperture;
    constexpr int segments = 96;
    const float exponent = 2.0f / 2.7f; // smooth between ellipse and diamond

    for (int s = 0; s <= segments; ++s) {
        const float theta = kTwoPi * (float)s / (float)segments;
        const float c = std::cos(theta);
        const float sn = std::sin(theta);
        const float sx = (c >= 0.0f ? 1.0f : -1.0f) * std::pow(std::abs(c), exponent);
        const float sy = (sn >= 0.0f ? 1.0f : -1.0f) * std::pow(std::abs(sn), exponent);
        const float breath = 1.0f + breathe * std::sin(theta * 2.0f + motionPhase);
        const float px = center.x + rx * sx * breath;
        const float py = center.y - ry * sy * breath;

        if (s == 0) {
            aperture.startNewSubPath(px, py);
        } else {
            aperture.lineTo(px, py);
        }
    }
    aperture.closeSubPath();

    g.setColour(spatialAccent().withAlpha(0.035f * mixNorm));
    g.fillPath(aperture);

    float dashLengths[2] = { 5.0f, 4.0f };
    juce::Path dashedAperture;
    juce::PathStrokeType(1.0f).createDashedStroke(dashedAperture, aperture, dashLengths, 2);
    g.setColour(spatialAccent().withAlpha(0.26f * mixNorm));
    g.fillPath(dashedAperture);
}

void StereoFieldComponent::drawAutoScaleBadge(juce::Graphics& g, juce::Rectangle<float> area, float scale) {
    using namespace Dimension5UI;

    if (!(scale > 1.08f) && !(scale < 0.98f)) {
        return;
    }

    const juce::String text = juce::String::formatted("AUTO x%.2f", (double)scale);
    const auto badge = juce::Rectangle<float>(area.getRight() - 96.0f, area.getBottom() - 15.0f, 92.0f, 12.0f);

    g.setFont(juce::Font(8.0f, juce::Font::plain));
    g.setColour(secondaryText().withAlpha(0.55f));
    g.drawText(text, badge, juce::Justification::centredRight);
}

void StereoFieldComponent::drawBypassBadge(juce::Graphics& g, juce::Rectangle<float> area) {
    using namespace Dimension5UI;

    const auto badge = juce::Rectangle<float>(area.getRight() - 74.0f, area.getY() + 2.0f, 70.0f, 16.0f);
    g.setColour(warningAccent().withAlpha(0.10f));
    g.fillRoundedRectangle(badge, 4.0f);
    g.setColour(warningAccent().withAlpha(0.65f));
    g.drawRoundedRectangle(badge, 4.0f, 1.0f);
    g.setFont(juce::Font(8.5f, juce::Font::bold));
    g.setColour(warningAccent().withAlpha(0.9f));
    g.drawText("BYPASS", badge, juce::Justification::centred);
}
