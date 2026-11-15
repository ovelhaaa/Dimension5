#include "StereoFieldComponent.h"
#include <algorithm>
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

    headerBounds = bounds.removeFromTop(32);
    auto modeButtonsArea = headerBounds.removeFromRight(juce::jlimit(162, 218, headerBounds.getWidth() / 2));
    modeButtonsArea = modeButtonsArea.reduced(6, 4);
    const int btnW = modeButtonsArea.getWidth() / 3;
    fieldModeButton.setBounds(modeButtonsArea.removeFromLeft(btnW).reduced(2, 0));
    vectorModeButton.setBounds(modeButtonsArea.removeFromLeft(btnW).reduced(2, 0));
    deltaModeButton.setBounds(modeButtonsArea.reduced(2, 0));

    const int metricsHeight = juce::jlimit(82, 106, juce::roundToInt((float)bounds.getHeight() * 0.24f));
    metricsBounds = bounds.removeFromBottom(metricsHeight);
    metricsComponent.setBounds(metricsBounds.reduced(8, 5));

    scopeBounds = bounds;
}

void StereoFieldComponent::paint(juce::Graphics& g) {
    using namespace Dimension5UI;

    const auto totalArea = getLocalBounds().toFloat().reduced(0.5f);
    constexpr float corner = 5.5f;

    // 1. Outer graphite module frame (#292B2D) over off-white chassis
    g.setColour(panelBackground());
    g.fillRoundedRectangle(totalArea, corner);

    // 2. Recessed header bar (#212325)
    const float headerH = (float)headerBounds.getHeight();
    const juce::Rectangle<float> headerBgRect(totalArea.getX() + 1.0f, totalArea.getY() + 1.0f,
                                              totalArea.getWidth() - 2.0f, headerH);
    g.setColour(panelRecessed());
    g.fillRoundedRectangle(headerBgRect, corner);
    g.fillRect(totalArea.getX() + 1.0f, totalArea.getY() + headerH - corner, totalArea.getWidth() - 2.0f, corner);

    g.setColour(panelBorder());
    g.drawRoundedRectangle(totalArea, corner, 1.0f);
    g.drawHorizontalLine(headerBounds.getBottom(), totalArea.getX() + 1.0f, totalArea.getRight() - 1.0f);

    // Minimalist orange index bar + section title
    g.setColour(primaryAccent());
    g.fillRoundedRectangle(totalArea.getX() + 10.0f, totalArea.getY() + (headerH - 11.0f) * 0.5f, 3.0f, 11.0f, 1.2f);

    g.setFont(Dimension5LookAndFeel::makeTechnicalFont(11.2f, true));
    g.setColour(textOnPanel());
    const float availableForTitle = (float)fieldModeButton.getX() - 28.0f;
    const float titleW = juce::jmin(160.0f, juce::jmax(70.0f, availableForTitle));
    g.drawText("STEREO FIELD MONITOR", 18.0f, 0.0f, titleW, (float)headerH, juce::Justification::centredLeft, true);

    juce::String modeDesc;
    if (currentMode == ViewMode::Field)       modeDesc = "FIELD  |  MEASURED PHOSPHOR CLOUD";
    else if (currentMode == ViewMode::Vector) modeDesc = "VECTOR  |  CONTINUOUS LISSAJOUS";
    else                                      modeDesc = "DELTA  |  INPUT VS OUTPUT";

    const float subX = 18.0f + titleW + 12.0f;
    const float subW = (float)fieldModeButton.getX() - subX - 10.0f;
    if (subW > 180.0f) {
        g.setFont(Dimension5LookAndFeel::makeTechnicalFont(9.2f, false));
        g.setColour(textMuted());
        g.drawText(modeDesc, (int)subX, 0, (int)subW, headerH, juce::Justification::centredLeft, false);
    }

    // 3. Recessed almost-black display interior (#17191B) inside the graphite frame
    auto displayFrame = scopeBounds.toFloat().reduced(8.0f, 6.0f);
    g.setColour(displayBackground());
    g.fillRoundedRectangle(displayFrame, 4.0f);
    g.setColour(panelBorder());
    g.drawRoundedRectangle(displayFrame, 4.0f, 1.0f);

    // Subtle technical graticule inside display screen
    g.setColour(displayGrid());
    for (int r = 1; r <= 3; ++r) {
        const float gy = displayFrame.getY() + displayFrame.getHeight() * ((float)r * 0.25f);
        g.drawHorizontalLine((int)gy, displayFrame.getX() + 2.0f, displayFrame.getRight() - 2.0f);
    }
    for (int c = 1; c <= 5; ++c) {
        const float gx = displayFrame.getX() + displayFrame.getWidth() * ((float)c / 6.0f);
        g.drawVerticalLine((int)gx, displayFrame.getY() + 2.0f, displayFrame.getBottom() - 2.0f);
    }

    // Divider line above metrics strip
    g.setColour(panelBorder());
    g.drawHorizontalLine(metricsBounds.getY(), totalArea.getX() + 8.0f, totalArea.getRight() - 8.0f);

    auto scopeArea = displayFrame.reduced(6.0f, 4.0f);

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
    const float radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.46f;

    drawReticle(g, center, radius, true);

    if (!bypassActive) {
        drawAuxiliaryAperture(g, center, radius);
    }

    // Explicit legend: configuration guide (neutral dashed) vs measured signal (orange)
    if (!bypassActive) {
        g.setFont(Dimension5LookAndFeel::makeTechnicalFont(8.2f, true));
        g.setColour(textMuted().withAlpha(0.75f));
        g.drawText("CONFIG GUIDE (DASHED)", area.getX() + 4.0f, area.getY() + 2.0f,
                   130.0f, 11.0f, juce::Justification::centredLeft);
    }

    const float scale = radius * 0.92f;
    const auto accent = primaryAccent();
    const auto hotCore = accentHover();
    const float scaleFactor = displayScale;

    for (size_t k = 0; k < pointCount; ++k) {
        const auto& pt = points[chronologicalIndex(k)];
        if (pt.age <= kPointRemovalAge) {
            continue;
        }

        const float px = center.x + juce::jlimit(-1.20f, 1.20f, pt.outputSide * scaleFactor) * scale;
        const float py = center.y - juce::jlimit(-1.20f, 1.20f, pt.outputMid * scaleFactor) * scale;

        // Warm orange phosphor halo + crisp high-intensity core
        g.setColour(accent.withAlpha(0.05f + 0.16f * pt.age));
        g.fillEllipse(px - 3.0f, py - 3.0f, 6.0f, 6.0f);

        g.setColour(accent.withAlpha(0.32f + 0.60f * pt.age));
        g.fillEllipse(px - 1.1f, py - 1.1f, 2.2f, 2.2f);

        if (pt.age > 0.72f) {
            g.setColour(hotCore.interpolatedWith(textOnPanel(), 0.45f).withAlpha(0.65f * (pt.age - 0.72f) / 0.28f));
            g.fillEllipse(px - 0.6f, py - 0.6f, 1.2f, 1.2f);
        }
    }

    if (getActiveOutputPointCount() == 0 && !hasSignal) {
        g.setFont(Dimension5LookAndFeel::makeTechnicalFont(10.0f, true));
        g.setColour(textMuted().withAlpha(0.75f));
        g.drawText("AWAITING SIGNAL",
                   center.x - 100.0f, center.y + radius * 0.50f,
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
    const float radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.46f;
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
    const float radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.46f;

    drawReticle(g, center, radius, true);

    g.setColour(displayAxis().withAlpha(0.65f));
    g.drawEllipse(center.x - radius * 0.75f, center.y - radius * 0.75f, radius * 1.5f, radius * 1.5f, 0.75f);
    g.drawEllipse(center.x - radius * 0.50f, center.y - radius * 0.50f, radius * 1.0f, radius * 1.0f, 0.75f);
    g.drawEllipse(center.x - radius * 0.25f, center.y - radius * 0.25f, radius * 0.5f, radius * 0.5f, 0.75f);

    g.setFont(Dimension5LookAndFeel::makeTechnicalFont(8.2f, false));
    g.setColour(textMuted().withAlpha(0.75f));
    g.drawText("-6 dB", center.x + 4.0f, center.y - radius * 0.75f - 9.0f, 35.0f, 10.0f, juce::Justification::centredLeft);
    g.drawText("-12 dB", center.x + 4.0f, center.y - radius * 0.50f - 9.0f, 35.0f, 10.0f, juce::Justification::centredLeft);

    if (getVectorSegmentCount() == 0) {
        if (!hasSignal) {
            g.setFont(Dimension5LookAndFeel::makeTechnicalFont(10.0f, true));
            g.setColour(textMuted().withAlpha(0.75f));
            g.drawText("VECTOR SCOPE IDLE",
                       center.x - 100.0f, center.y + radius * 0.50f,
                       200.0f, 16.0f, juce::Justification::centred);
        }
        return;
    }

    const auto path = buildVectorPath(area);

    // Multi-luminosity orange trace: soft bloom + primary orange + bright highlight filament
    g.setColour(primaryAccent().withAlpha(0.20f));
    g.strokePath(path, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(primaryAccent().withAlpha(0.88f));
    g.strokePath(path, juce::PathStrokeType(1.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(accentHover().withAlpha(0.55f));
    g.strokePath(path, juce::PathStrokeType(0.55f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (bypassActive) {
        drawBypassBadge(g, area);
    }
    drawAutoScaleBadge(g, area, displayScale);
}

// ==============================================================================
// 3. DELTA MODE: True Input vs Output from time-aligned measured samples
//    Strictly single chromatic accent:
//    - INPUT / BEFORE: neutral light gray (#BAB6AD) with hollow/fine coding
//    - OUTPUT / AFTER: primary orange (#F36B2B) with solid luminous coding
// ==============================================================================
void StereoFieldComponent::drawDeltaMode(juce::Graphics& g, juce::Rectangle<float> area) {
    using namespace Dimension5UI;

    auto working = area;
    const float panelW = working.getWidth() * 0.415f;
    auto leftArea = working.removeFromLeft(panelW);
    auto rightArea = working.removeFromRight(panelW);
    auto centerBadgeArea = working;

    const float radius = juce::jmax(18.0f, juce::jmin(leftArea.getWidth(), leftArea.getHeight()) * 0.43f);
    const auto centerL = leftArea.getCentre();
    const auto centerR = rightArea.getCentre();
    const float scale = radius * 0.92f;
    const auto beforeCol = textSecondary(); // Neutral light gray (#BAB6AD)
    const auto afterCol  = primaryAccent(); // Precision orange (#F36B2B)

    // Left Reticle + BEFORE Legend Pill (Neutral Gray + Dashed Marker)
    drawReticle(g, centerL, radius, false);
    {
        const juce::Rectangle<float> hdrL(leftArea.getX() + 6.0f, leftArea.getY() + 2.0f,
                                          leftArea.getWidth() - 12.0f, 15.0f);
        g.setFont(Dimension5LookAndFeel::makeTechnicalFont(9.5f, true));
        g.setColour(beforeCol);
        g.drawText("INPUT / BEFORE (REF)", hdrL, juce::Justification::centred, false);
    }

    // Right Reticle + AFTER Legend Pill (Orange + Solid Marker)
    drawReticle(g, centerR, radius, false);
    {
        const juce::Rectangle<float> hdrR(rightArea.getX() + 6.0f, rightArea.getY() + 2.0f,
                                          rightArea.getWidth() - 12.0f, 15.0f);
        g.setFont(Dimension5LookAndFeel::makeTechnicalFont(9.5f, true));
        g.setColour(afterCol);
        g.drawText("OUTPUT / AFTER (ACTIVE)", hdrR, juce::Justification::centred, false);
    }

    const float deltaScaleFactor = deltaDisplayScale;

    // Connect consecutive points in BEFORE with a fine dashed neutral trace and AFTER with a continuous orange trace,
    // plus distinct point clouds so both dense and sparse signals remain unmistakably differentiated.
    juce::Path beforePath;
    juce::Path afterPath;
    bool hasPrev = false;
    uint64_t prevSeq = 0;

    for (size_t k = 0; k < pointCount; ++k) {
        const auto& pt = points[chronologicalIndex(k)];
        if (pt.age <= kPointRemovalAge) {
            hasPrev = false;
            continue;
        }

        const float inX = centerL.x + juce::jlimit(-1.20f, 1.20f, pt.inputSide * deltaScaleFactor) * scale;
        const float inY = centerL.y - juce::jlimit(-1.20f, 1.20f, pt.inputMid * deltaScaleFactor) * scale;

        const float outX = centerR.x + juce::jlimit(-1.20f, 1.20f, pt.outputSide * deltaScaleFactor) * scale;
        const float outY = centerR.y - juce::jlimit(-1.20f, 1.20f, pt.outputMid * deltaScaleFactor) * scale;

        if (pt.age > 0.22f) {
            if (!hasPrev || pt.sequence != prevSeq + 1) {
                beforePath.startNewSubPath(inX, inY);
                afterPath.startNewSubPath(outX, outY);
            } else {
                beforePath.lineTo(inX, inY);
                afterPath.lineTo(outX, outY);
            }
            prevSeq = pt.sequence;
            hasPrev = true;
        }

        // Input cloud (neutral gray, fine crisp points)
        g.setColour(beforeCol.withAlpha(0.04f + 0.10f * pt.age));
        g.fillEllipse(inX - 2.2f, inY - 2.2f, 4.4f, 4.4f);
        g.setColour(beforeCol.withAlpha(0.28f + 0.52f * pt.age));
        g.fillEllipse(inX - 0.85f, inY - 0.85f, 1.7f, 1.7f);

        // Output cloud (orange, luminous points)
        g.setColour(afterCol.withAlpha(0.05f + 0.14f * pt.age));
        g.fillEllipse(outX - 2.8f, outY - 2.8f, 5.6f, 5.6f);
        g.setColour(afterCol.withAlpha(0.32f + 0.58f * pt.age));
        g.fillEllipse(outX - 1.05f, outY - 1.05f, 2.1f, 2.1f);
    }

    // Draw dashed neutral trace on BEFORE and solid orange trace on AFTER
    if (!beforePath.isEmpty()) {
        const float dashLengths[2] = { 3.5f, 3.0f };
        juce::Path dashedBefore;
        juce::PathStrokeType(0.85f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
            .createDashedStroke(dashedBefore, beforePath, dashLengths, 2);
        g.setColour(beforeCol.withAlpha(0.45f));
        g.fillPath(dashedBefore);
    }
    if (!afterPath.isEmpty()) {
        g.setColour(afterCol.withAlpha(0.48f));
        g.strokePath(afterPath, juce::PathStrokeType(1.10f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (!hasSignal) {
        if (getActiveInputPointCount() == 0) {
            g.setFont(Dimension5LookAndFeel::makeTechnicalFont(9.2f, true));
            g.setColour(textMuted().withAlpha(0.70f));
            g.drawText("NO INPUT", centerL.x - 50.0f, centerL.y + radius * 0.52f, 100.0f, 14.0f, juce::Justification::centred);
        }
        if (getActiveOutputPointCount() == 0) {
            g.setFont(Dimension5LookAndFeel::makeTechnicalFont(9.2f, true));
            g.setColour(textMuted().withAlpha(0.70f));
            g.drawText("NO OUTPUT", centerR.x - 50.0f, centerR.y + radius * 0.52f, 100.0f, 14.0f, juce::Justification::centred);
        }
    }

    // Central transfer readout: measured Side/Mid delta in dB.
    {
        const auto badgeCenter = centerBadgeArea.getCentre();
        const float badgeW = juce::jmax(72.0f, centerBadgeArea.getWidth());

        const juce::Rectangle<float> centerCard(badgeCenter.x - badgeW * 0.48f, badgeCenter.y - 38.0f,
                                                badgeW * 0.96f, 74.0f);
        g.setColour(panelRecessed().withAlpha(0.90f));
        g.fillRoundedRectangle(centerCard, 4.0f);
        g.setColour(panelBorder());
        g.drawRoundedRectangle(centerCard, 4.0f, 1.0f);

        g.setFont(Dimension5LookAndFeel::makeTechnicalFont(8.2f, true));
        g.setColour(textMuted());
        g.drawText("STEREO DELTA", badgeCenter.x - badgeW * 0.5f, badgeCenter.y - 33.0f,
                   badgeW, 12.0f, juce::Justification::centred);

        const auto& f = lastTelemetry.frame;
        juce::String valueStr = "--";
        juce::String statusStr = "N/A";
        juce::Colour valueColour = textMuted();

        const bool deltaValid = f.hasSignal && f.isStereoDeltaValid;
        if (bypassActive) {
            statusStr = "BYPASS";
            valueColour = primaryAccent();
            valueStr = deltaValid ? juce::String::formatted("%+.1f dB", (double)f.stereoDeltaDb) : juce::String("--");
        } else if (deltaValid) {
            valueStr = juce::String::formatted("%+.1f dB", (double)f.stereoDeltaDb);
            if (f.stereoDeltaDb > 0.4f) {
                statusStr = "EXPANDED";
                valueColour = primaryAccent();
            } else if (f.stereoDeltaDb < -0.4f) {
                statusStr = "NARROWED";
                valueColour = textSecondary();
            } else {
                statusStr = "UNITY";
                valueColour = textOnPanel();
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
            valueColour = textSecondary();
        }

        g.setFont(Dimension5LookAndFeel::makeTechnicalFont(14.0f, true));
        g.setColour(valueColour);
        g.drawText(valueStr, badgeCenter.x - badgeW * 0.5f, badgeCenter.y - 19.0f,
                   badgeW, 20.0f, juce::Justification::centred);

        g.setFont(Dimension5LookAndFeel::makeTechnicalFont(8.5f, true));
        g.setColour(valueColour.withAlpha(0.90f));
        g.drawText(statusStr, badgeCenter.x - badgeW * 0.5f, badgeCenter.y + 2.0f,
                   badgeW, 12.0f, juce::Justification::centred);

        if (std::abs(deltaScaleFactor - 1.0f) > 0.06f) {
            g.setFont(Dimension5LookAndFeel::makeTechnicalFont(7.5f, false));
            g.setColour(textMuted());
            g.drawText(juce::String::formatted("SCALE x%.1f", (double)deltaScaleFactor),
                       badgeCenter.x - badgeW * 0.5f, badgeCenter.y + 18.0f,
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

    g.setColour(displayBackground());
    g.fillEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour(displayAxis());
    g.drawEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f, 1.1f);

    // Subtle inner reference ring
    g.setColour(displayGrid().brighter(0.06f));
    g.drawEllipse(center.x - radius * 0.55f, center.y - radius * 0.55f, radius * 1.10f, radius * 1.10f, 0.8f);

    g.setColour(displayAxis());
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
        g.setColour(displayAxis().withAlpha(0.75f));
        g.fillPath(dashedDiag);

        g.setFont(Dimension5LookAndFeel::makeTechnicalFont(8.8f, true));
        g.setColour(textSecondary().withAlpha(0.85f));
        g.drawText("M", center.x - 10.0f, center.y - radius + 2.0f, 20.0f, 12.0f, juce::Justification::centred);
        g.drawText("+S", center.x + radius - 20.0f, center.y - 12.0f, 20.0f, 12.0f, juce::Justification::centred);
        g.drawText("-S", center.x - radius + 2.0f, center.y - 12.0f, 20.0f, 12.0f, juce::Justification::centred);
        g.drawText("L", center.x + diag - 4.0f, center.y - diag - 12.0f, 16.0f, 12.0f, juce::Justification::centred);
        g.drawText("R", center.x - diag - 12.0f, center.y - diag - 12.0f, 16.0f, 12.0f, juce::Justification::centred);
    }
}

// ==============================================================================
// Configuration aperture: a smooth superellipse, explicitly a neutral config guide
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

    // Neutral technical guide so orange remains exclusively for measured telemetry
    g.setColour(textSecondary().withAlpha(0.035f * mixNorm));
    g.fillPath(aperture);

    float dashLengths[2] = { 5.0f, 4.0f };
    juce::Path dashedAperture;
    juce::PathStrokeType(1.0f).createDashedStroke(dashedAperture, aperture, dashLengths, 2);
    g.setColour(textSecondary().withAlpha(0.32f * mixNorm));
    g.fillPath(dashedAperture);
}

void StereoFieldComponent::drawAutoScaleBadge(juce::Graphics& g, juce::Rectangle<float> area, float scale) {
    using namespace Dimension5UI;

    if (!(scale > 1.08f) && !(scale < 0.98f)) {
        return;
    }

    const juce::String text = juce::String::formatted("AUTO x%.2f", (double)scale);
    const auto badge = juce::Rectangle<float>(area.getRight() - 96.0f, area.getBottom() - 15.0f, 92.0f, 12.0f);

    g.setFont(Dimension5LookAndFeel::makeTechnicalFont(8.0f, true));
    g.setColour(textMuted().withAlpha(0.80f));
    g.drawText(text, badge, juce::Justification::centredRight);
}

void StereoFieldComponent::drawBypassBadge(juce::Graphics& g, juce::Rectangle<float> area) {
    using namespace Dimension5UI;

    const auto badge = juce::Rectangle<float>(area.getRight() - 78.0f, area.getY() + 3.0f, 74.0f, 17.0f);
    g.setColour(panelRecessed().withAlpha(0.95f));
    g.fillRoundedRectangle(badge, 3.5f);
    g.setColour(primaryAccent());
    g.drawRoundedRectangle(badge, 3.5f, 1.3f);

    // Left orange index bar inside bypass badge
    g.fillRoundedRectangle(badge.getX() + 3.0f, badge.getY() + 3.5f, 2.5f, badge.getHeight() - 7.0f, 1.0f);

    g.setFont(Dimension5LookAndFeel::makeTechnicalFont(8.5f, true));
    g.setColour(primaryAccent());
    g.drawText("BYPASS", badge.withTrimmedLeft(5.0f), juce::Justification::centred);
}
