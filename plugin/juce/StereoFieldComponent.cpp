#include "StereoFieldComponent.h"
#include <cmath>

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
    bypassActive = isBypassed;
    metricsComponent.setBypassState(isBypassed);
}

void StereoFieldComponent::updateTelemetry(const StereoTelemetrySnapshot& snapshot) {
    lastTelemetry = snapshot;
    hasSignal = snapshot.frame.hasSignal;
    metricsComponent.updateMetrics(snapshot.frame);

    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dtSec = (lastUpdateTimeMs > 0.0) ? (now - lastUpdateTimeMs) * 0.001 : 0.033;
    lastUpdateTimeMs = now;

    // Time-based phosphor persistence (~180 ms half-life)
    const float decay = std::exp(-static_cast<float>(dtSec) / 0.180f);

    if (hasSignal) {
        // Dynamic auto-scale tracker with minimum floor
        const float peakMax = juce::jmax(snapshot.frame.outputPeakL, snapshot.frame.outputPeakR,
                                         snapshot.frame.inputPeakL, snapshot.frame.inputPeakR);
        const float targetScale = (peakMax > 0.85f) ? (0.85f / peakMax) : 1.0f;
        displayScale += 0.15f * (targetScale - displayScale);

        // Ingest new output goniometer points into circular cloud history
        for (uint32_t i = 0; i < snapshot.gonioCount; ++i) {
            auto& pt = cloudPoints[cloudWritePos];
            pt.x = snapshot.gonioPoints[i].side * displayScale;
            pt.y = snapshot.gonioPoints[i].mid * displayScale;
            pt.age = 1.0f;
            cloudWritePos = (cloudWritePos + 1) % kMaxCloudPoints;

            // Approximate input points for DELTA comparison
            if (i < kMaxInputPoints) {
                auto& inPt = inputPoints[inputWritePos];
                // Synthesize input mid/side point proportionally
                const float inRatio = (snapshot.frame.inputMidRms > 1e-4f) ? (snapshot.frame.inputSideRms / snapshot.frame.inputMidRms) : 0.0f;
                inPt.x = snapshot.gonioPoints[i].side * inRatio * displayScale;
                inPt.y = snapshot.gonioPoints[i].mid * displayScale;
                inPt.age = 1.0f;
                inputWritePos = (inputWritePos + 1) % kMaxInputPoints;
            }
        }
    } else {
        displayScale += 0.10f * (1.0f - displayScale);
    }

    // Smooth continuous age decay
    for (auto& pt : cloudPoints) {
        if (pt.age > 0.0f) {
            pt.age *= decay;
            if (pt.age < 0.03f) {
                pt.age = 0.0f;
            }
        }
    }
    for (auto& pt : inputPoints) {
        if (pt.age > 0.0f) {
            pt.age *= decay;
            if (pt.age < 0.03f) {
                pt.age = 0.0f;
            }
        }
    }
}

void StereoFieldComponent::updateParameterGeometry(float spread, float depthMs, float rateHz, float mix) {
    paramSpread = spread;
    paramDepthMs = depthMs;
    paramRateHz = rateHz;
    paramMix = mix;

    // Advance motion phase smoothly based on rate
    motionPhase += 0.033f * rateHz * juce::MathConstants<float>::twoPi;
    if (motionPhase > juce::MathConstants<float>::twoPi) {
        motionPhase -= juce::MathConstants<float>::twoPi;
    }
}

void StereoFieldComponent::resized() {
    auto bounds = getLocalBounds();

    // 1. Top bar with Mode selector buttons
    headerBounds = bounds.removeFromTop(32);
    auto modeButtonsArea = headerBounds.removeFromRight(190).reduced(0, 3);
    const int btnW = modeButtonsArea.getWidth() / 3;
    fieldModeButton.setBounds(modeButtonsArea.removeFromLeft(btnW).reduced(2, 0));
    vectorModeButton.setBounds(modeButtonsArea.removeFromLeft(btnW).reduced(2, 0));
    deltaModeButton.setBounds(modeButtonsArea.reduced(2, 0));

    // 2. Metrics Bar at bottom
    const int metricsHeight = 64;
    metricsBounds = bounds.removeFromBottom(metricsHeight);
    metricsComponent.setBounds(metricsBounds.reduced(8, 2));

    // 3. Central Scope Canvas
    scopeBounds = bounds;
}

void StereoFieldComponent::paint(juce::Graphics& g) {
    using namespace Dimension5UI;

    auto totalArea = getLocalBounds().toFloat();

    // Outer card container
    g.setColour(surfaceColour());
    g.fillRoundedRectangle(totalArea, 6.0f);
    g.setColour(dividerColour());
    g.drawRoundedRectangle(totalArea, 6.0f, 1.0f);

    // Header divider line
    g.setColour(dividerColour());
    g.drawLine(totalArea.getX() + 8.0f, (float)headerBounds.getBottom(),
               totalArea.getRight() - 8.0f, (float)headerBounds.getBottom(), 1.0f);

    // Header title
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(secondaryText());
    auto titleArea = headerBounds.toFloat().reduced(12.0f, 0.0f);
    g.drawText("STEREO FIELD MONITOR 2.0", titleArea.removeFromLeft(220.0f), juce::Justification::centredLeft);

    // Active mode label
    juce::String modeDesc;
    if (currentMode == ViewMode::Field)       modeDesc = "FIELD / Phosphor Cloud & Dispersion Aperture";
    else if (currentMode == ViewMode::Vector) modeDesc = "VECTOR / Precision Lissajous Goniometer";
    else                                      modeDesc = "DELTA / Comparative Analysis";

    g.setFont(juce::Font(9.5f, juce::Font::plain));
    g.setColour(secondaryText().withAlpha(0.6f));
    g.drawText(modeDesc, titleArea, juce::Justification::centredLeft);

    // Metrics divider line
    g.setColour(dividerColour());
    g.drawLine(totalArea.getX() + 8.0f, (float)metricsBounds.getY(),
               totalArea.getRight() - 8.0f, (float)metricsBounds.getY(), 1.0f);

    // Render active view mode in scopeBounds
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
// 1. FIELD MODE: Phosphor Cloud + Parametric Spatial Aperture
// ==============================================================================
void StereoFieldComponent::drawFieldMode(juce::Graphics& g, juce::Rectangle<float> area) {
    using namespace Dimension5UI;

    const auto center = area.getCentre();
    const float radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.47f;

    // Reticle with Mid, Side, and L/R diagonals
    drawReticle(g, center, radius, true);

    // Auxiliary parameter aperture (distinct configuration layer)
    drawAuxiliaryAperture(g, center, radius);

    // Real measured audio cloud
    if (!hasSignal) {
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.setColour(secondaryText().withAlpha(0.5f));
        g.drawText("MONITOR IDLE / STANDBY",
                   center.x - 120.0f, center.y - 10.0f,
                   240.0f, 20.0f,
                   juce::Justification::centred);
        return;
    }

    const float scale = radius * 0.92f;
    const auto accent = spatialAccent();

    for (const auto& pt : cloudPoints) {
        if (pt.age <= 0.03f) {
            continue;
        }

        const float px = center.x + juce::jlimit(-1.25f, 1.25f, pt.x) * scale;
        const float py = center.y - juce::jlimit(-1.25f, 1.25f, pt.y) * scale;

        // Glowing phosphor particle
        const float alpha = pt.age * 0.85f;
        g.setColour(accent.withAlpha(alpha));
        g.fillEllipse(px - 1.4f, py - 1.4f, 2.8f, 2.8f);

        // Core bright center for high energy
        if (pt.age > 0.6f) {
            g.setColour(primaryText().withAlpha(pt.age * 0.7f));
            g.fillEllipse(px - 0.7f, py - 0.7f, 1.4f, 1.4f);
        }
    }
}

// ==============================================================================
// 2. VECTOR MODE: Precision Lissajous Oscilloscope
// ==============================================================================
void StereoFieldComponent::drawVectorMode(juce::Graphics& g, juce::Rectangle<float> area) {
    using namespace Dimension5UI;

    const auto center = area.getCentre();
    const float radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.47f;

    drawReticle(g, center, radius, true);

    // Polar dB concentric reference rings (-6 dB, -12 dB, -18 dB)
    g.setColour(dividerColour().withAlpha(0.35f));
    g.drawEllipse(center.x - radius * 0.75f, center.y - radius * 0.75f, radius * 1.5f, radius * 1.5f, 0.75f);
    g.drawEllipse(center.x - radius * 0.50f, center.y - radius * 0.50f, radius * 1.0f, radius * 1.0f, 0.75f);
    g.drawEllipse(center.x - radius * 0.25f, center.y - radius * 0.25f, radius * 0.5f, radius * 0.5f, 0.75f);

    g.setFont(juce::Font(8.5f, juce::Font::plain));
    g.setColour(secondaryText().withAlpha(0.5f));
    g.drawText("-6 dB", center.x + 4.0f, center.y - radius * 0.75f - 9.0f, 35.0f, 10.0f, juce::Justification::centredLeft);
    g.drawText("-12 dB", center.x + 4.0f, center.y - radius * 0.50f - 9.0f, 35.0f, 10.0f, juce::Justification::centredLeft);

    if (!hasSignal) {
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.setColour(secondaryText().withAlpha(0.5f));
        g.drawText("VECTOR SCOPE IDLE",
                   center.x - 100.0f, center.y - 10.0f,
                   200.0f, 20.0f,
                   juce::Justification::centred);
        return;
    }

    const float scale = radius * 0.92f;
    juce::Path vectorPath;
    bool pathStarted = false;

    // Build vector path connecting active points in chronological order
    for (size_t i = 0; i < kMaxCloudPoints; ++i) {
        const size_t idx = (cloudWritePos + i) % kMaxCloudPoints;
        const auto& pt = cloudPoints[idx];

        if (pt.age <= 0.08f) {
            pathStarted = false;
            continue;
        }

        const float px = center.x + juce::jlimit(-1.25f, 1.25f, pt.x) * scale;
        const float py = center.y - juce::jlimit(-1.25f, 1.25f, pt.y) * scale;

        if (!pathStarted) {
            vectorPath.startNewSubPath(px, py);
            pathStarted = true;
        } else {
            vectorPath.lineTo(px, py);
        }
    }

    // Outer glow stroke
    g.setColour(spatialAccent().withAlpha(0.20f));
    g.strokePath(vectorPath, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Core laser beam vector stroke
    g.setColour(spatialAccent().withAlpha(0.85f));
    g.strokePath(vectorPath, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ==============================================================================
// 3. DELTA MODE: Comparative Input vs Output Stereo Geometry
// ==============================================================================
void StereoFieldComponent::drawDeltaMode(juce::Graphics& g, juce::Rectangle<float> area) {
    using namespace Dimension5UI;

    const float panelW = area.getWidth() * 0.48f;
    auto leftArea = area.removeFromLeft(panelW);
    auto rightArea = area.removeFromRight(panelW);
    auto centerBadgeArea = area;

    const float radius = juce::jmin(leftArea.getWidth(), leftArea.getHeight()) * 0.44f;
    const auto centerL = leftArea.getCentre();
    const auto centerR = rightArea.getCentre();

    // 1. Left Scope: INPUT FIELD (Dry Reference)
    drawReticle(g, centerL, radius, false);
    g.setFont(juce::Font(10.5f, juce::Font::bold));
    g.setColour(analogAccent());
    g.drawText("INPUT SIGNAL (PRE-BBD)", centerL.x - 90.0f, centerL.y - radius - 16.0f, 180.0f, 14.0f, juce::Justification::centred);

    // Input Envelope ellipse
    if (hasSignal && lastTelemetry.frame.inputMidRms > 1e-4f) {
        const float inRatio = lastTelemetry.frame.inputSideRms / lastTelemetry.frame.inputMidRms;
        const float inRx = juce::jlimit(4.0f, radius * 0.9f, radius * inRatio * 0.8f);
        const float inRy = juce::jlimit(8.0f, radius * 0.9f, radius * 0.7f);

        g.setColour(analogAccent().withAlpha(0.12f));
        g.fillEllipse(centerL.x - inRx, centerL.y - inRy, inRx * 2.0f, inRy * 2.0f);
        g.setColour(analogAccent().withAlpha(0.70f));
        g.drawEllipse(centerL.x - inRx, centerL.y - inRy, inRx * 2.0f, inRy * 2.0f, 1.2f);
    } else {
        g.setFont(juce::Font(10.0f, juce::Font::plain));
        g.setColour(secondaryText().withAlpha(0.5f));
        g.drawText("NO INPUT", centerL.x - 40.0f, centerL.y - 6.0f, 80.0f, 12.0f, juce::Justification::centred);
    }

    // 2. Center Transfer Indicator
    {
        const auto badgeCenter = centerBadgeArea.getCentre();
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.setColour(spatialAccent());
        g.drawText("-->", badgeCenter.x - 20.0f, badgeCenter.y - 20.0f, 40.0f, 20.0f, juce::Justification::centred);

        const float deltaDb = lastTelemetry.frame.stereoDeltaDb;
        juce::String deltaStr = bypassActive ? "BYPASS" : juce::String::formatted("%+.1f dB", (double)deltaDb);
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.setColour(bypassActive ? warningAccent() : (deltaDb > 0.3f ? spatialAccent() : primaryText()));
        g.drawText(deltaStr, badgeCenter.x - 35.0f, badgeCenter.y + 4.0f, 70.0f, 14.0f, juce::Justification::centred);
    }

    // 3. Right Scope: OUTPUT FIELD (Post-BBD Enhancer)
    drawReticle(g, centerR, radius, false);
    g.setFont(juce::Font(10.5f, juce::Font::bold));
    g.setColour(spatialAccent());
    g.drawText("OUTPUT SIGNAL (ENHANCED)", centerR.x - 90.0f, centerR.y - radius - 16.0f, 180.0f, 14.0f, juce::Justification::centred);

    // Output Envelope ellipse
    if (hasSignal && lastTelemetry.frame.midRms > 1e-4f) {
        const float outRatio = lastTelemetry.frame.sideRms / lastTelemetry.frame.midRms;
        const float outRx = juce::jlimit(4.0f, radius * 0.95f, radius * outRatio * 0.8f);
        const float outRy = juce::jlimit(8.0f, radius * 0.90f, radius * 0.7f);

        g.setColour(spatialAccent().withAlpha(0.15f));
        g.fillEllipse(centerR.x - outRx, centerR.y - outRy, outRx * 2.0f, outRy * 2.0f);
        g.setColour(spatialAccent().withAlpha(0.85f));
        g.drawEllipse(centerR.x - outRx, centerR.y - outRy, outRx * 2.0f, outRy * 2.0f, 1.5f);
    } else {
        g.setFont(juce::Font(10.0f, juce::Font::plain));
        g.setColour(secondaryText().withAlpha(0.5f));
        g.drawText("NO OUTPUT", centerR.x - 40.0f, centerR.y - 6.0f, 80.0f, 12.0f, juce::Justification::centred);
    }
}

// ==============================================================================
// Common Reticle and Guides
// ==============================================================================
void StereoFieldComponent::drawReticle(juce::Graphics& g, juce::Point<float> center, float radius, bool showDiagonals) {
    using namespace Dimension5UI;

    // Reticle chassis
    g.setColour(bgColour());
    g.fillEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour(dividerColour());
    g.drawEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);

    // Mid axis (vertical)
    g.setColour(dividerColour().withAlpha(0.7f));
    g.drawLine(center.x, center.y - radius, center.x, center.y + radius, 1.0f);

    // Side axis (horizontal)
    g.drawLine(center.x - radius, center.y, center.x + radius, center.y, 1.0f);

    if (showDiagonals) {
        // 45-degree diagonal L and R axes
        const float diag = radius * 0.7071f;
        juce::Path diagPath;
        diagPath.startNewSubPath(center.x - diag, center.y - diag);
        diagPath.lineTo(center.x + diag, center.y + diag);
        diagPath.startNewSubPath(center.x - diag, center.y + diag);
        diagPath.lineTo(center.x + diag, center.y - diag);

        float dashLengths[2] = { 3.0f, 4.0f };
        juce::Path dashedDiag;
        juce::PathStrokeType(0.75f).createDashedStroke(dashedDiag, diagPath, dashLengths, 2);
        g.setColour(dividerColour().withAlpha(0.40f));
        g.fillPath(dashedDiag);

        // Axis reference labels
        g.setFont(juce::Font(9.5f, juce::Font::bold));
        g.setColour(secondaryText());
        g.drawText("M", center.x - 10.0f, center.y - radius - 2.0f, 20.0f, 12.0f, juce::Justification::centred);
        g.drawText("+S", center.x + radius - 18.0f, center.y - 12.0f, 22.0f, 12.0f, juce::Justification::centred);
        g.drawText("-S", center.x - radius - 4.0f, center.y - 12.0f, 22.0f, 12.0f, juce::Justification::centred);
        g.drawText("L", center.x + diag - 6.0f, center.y - diag - 12.0f, 16.0f, 12.0f, juce::Justification::centred);
        g.drawText("R", center.x - diag - 10.0f, center.y - diag - 12.0f, 16.0f, 12.0f, juce::Justification::centred);
    }
}

// ==============================================================================
// Auxiliary Dispersion Aperture (Parameter Feedback)
// ==============================================================================
void StereoFieldComponent::drawAuxiliaryAperture(juce::Graphics& g, juce::Point<float> center, float radius) {
    using namespace Dimension5UI;

    if (bypassActive) {
        // In bypass, draw muted aperture with bypass badge
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.setColour(warningAccent().withAlpha(0.6f));
        g.drawText("PROCESSOR BYPASS ENGAGED",
                   center.x - 110.0f, center.y - radius + 12.0f,
                   220.0f, 16.0f,
                   juce::Justification::centred);
        return;
    }

    // Width scales with SPREAD parameter and MIX
    const float baseWidth = juce::jlimit(0.12f, 0.95f, (0.20f + 0.35f * paramSpread) * (0.3f + 0.7f * paramMix));
    const float depthMod = (paramDepthMs / 6.0f) * 0.06f * std::sin(motionPhase);
    const float effWidth = juce::jlimit(0.08f, 0.98f, baseWidth + depthMod);
    const float effHeight = juce::jlimit(0.20f, 0.88f, 0.45f + 0.40f * paramMix);

    const float rx = radius * effWidth;
    const float ry = radius * effHeight;

    // Diamond spatial aperture
    juce::Path aperture;
    aperture.startNewSubPath(center.x, center.y - ry);
    aperture.lineTo(center.x + rx, center.y);
    aperture.lineTo(center.x, center.y + ry);
    aperture.lineTo(center.x - rx, center.y);
    aperture.closeSubPath();

    // Soft aperture wash
    g.setColour(spatialAccent().withAlpha(0.05f * paramMix));
    g.fillPath(aperture);

    // Dashed technical aperture contour
    float dashLengths[2] = { 4.0f, 3.0f };
    juce::Path dashedAperture;
    juce::PathStrokeType(1.0f).createDashedStroke(dashedAperture, aperture, dashLengths, 2);
    g.setColour(spatialAccent().withAlpha(0.30f * paramMix));
    g.fillPath(dashedAperture);
}
