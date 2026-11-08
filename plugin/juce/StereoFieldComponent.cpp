#include "StereoFieldComponent.h"

namespace {
juce::Colour bgColour()         { return juce::Colour(0x10, 0x17, 0x1C); }
juce::Colour surfaceColour()    { return juce::Colour(0x1B, 0x27, 0x2D); }
juce::Colour dividerColour()    { return juce::Colour(0x34, 0x44, 0x4B); }
juce::Colour primaryText()      { return juce::Colour(0xE8, 0xEF, 0xEC); }
juce::Colour secondaryText()    { return juce::Colour(0x89, 0x9D, 0xA3); }
juce::Colour spatialAccent()    { return juce::Colour(0x65, 0xDF, 0xC8); }
juce::Colour analogAccent()     { return juce::Colour(0xE2, 0xA6, 0x5A); }
juce::Colour warningAccent()    { return juce::Colour(0xE2, 0x78, 0x65); }
} // namespace

StereoFieldComponent::StereoFieldComponent() {
    setOpaque(false);
}

void StereoFieldComponent::updateTelemetry(const StereoTelemetrySnapshot& snapshot) {
    lastTelemetry = snapshot;
    hasSignal = snapshot.frame.hasSignal;

    if (hasSignal) {
        // Ingest new goniometer points into cloud history
        for (uint32_t i = 0; i < snapshot.gonioCount; ++i) {
            auto& pt = cloudPoints[cloudWritePos];
            pt.x = snapshot.gonioPoints[i].side;
            pt.y = snapshot.gonioPoints[i].mid;
            pt.age = 1.0f;
            cloudWritePos = (cloudWritePos + 1) % kMaxCloudPoints;
        }

        // Smooth metrics for stable display
        displayCorr += 0.25f * (snapshot.frame.correlation - displayCorr);
        displayOutSmDb += 0.25f * (snapshot.frame.sideMidRatioDb - displayOutSmDb);
        displayInSmDb += 0.25f * (snapshot.frame.inputSideMidRatioDb - displayInSmDb);
        displayMonoAttenDb += 0.25f * (snapshot.frame.monoAttenuationDb - displayMonoAttenDb);
    } else {
        // Fast decay on silence
        displayCorr += 0.35f * (1.0f - displayCorr);
        displayOutSmDb += 0.35f * (-60.0f - displayOutSmDb);
        displayInSmDb += 0.35f * (-60.0f - displayInSmDb);
        displayMonoAttenDb += 0.35f * (0.0f - displayMonoAttenDb);
    }

    // Age all active cloud points for smooth phosphor persistence
    for (auto& pt : cloudPoints) {
        if (pt.age > 0.0f) {
            pt.age *= 0.88f;
            if (pt.age < 0.04f) {
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

    // Advance motion phase for subtle modulation in the auxiliary geometry
    motionPhase += 0.033f * rateHz * juce::MathConstants<float>::twoPi;
    if (motionPhase > juce::MathConstants<float>::twoPi) {
        motionPhase -= juce::MathConstants<float>::twoPi;
    }
}

void StereoFieldComponent::resized() {
    auto bounds = getLocalBounds();
    const int metricsHeight = 76;
    metricsBounds = bounds.removeFromBottom(metricsHeight);
    scopeBounds = bounds;
}

void StereoFieldComponent::paint(juce::Graphics& g) {
    auto totalArea = getLocalBounds().toFloat();

    // Background card with subtle border
    g.setColour(bgColour());
    g.fillRoundedRectangle(totalArea, 6.0f);
    g.setColour(dividerColour());
    g.drawRoundedRectangle(totalArea, 6.0f, 1.0f);

    // Divider between Scope and Metrics
    g.setColour(dividerColour());
    g.drawLine(totalArea.getX() + 8.0f,
               (float)metricsBounds.getY(),
               totalArea.getRight() - 8.0f,
               (float)metricsBounds.getY(),
               1.0f);

    // Render Goniometer Scope
    drawGoniometer(g, scopeBounds.toFloat().reduced(6.0f));

    // Render Metrics Panel
    drawMetrics(g, metricsBounds.toFloat().reduced(8.0f, 4.0f));
}

void StereoFieldComponent::drawGoniometer(juce::Graphics& g, juce::Rectangle<float> area) {
    const auto center = area.getCentre();
    const float radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.46f;

    // Outer circular boundary / reticle
    g.setColour(surfaceColour());
    g.fillEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f);

    g.setColour(dividerColour());
    g.drawEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);
    g.drawEllipse(center.x - radius * 0.5f, center.y - radius * 0.5f, radius, radius, 0.75f);

    // Mid axis (vertical)
    g.setColour(dividerColour().withAlpha(0.6f));
    g.drawLine(center.x, center.y - radius, center.x, center.y + radius, 1.0f);

    // Side axis (horizontal)
    g.drawLine(center.x - radius, center.y, center.x + radius, center.y, 1.0f);

    // 45-degree diagonal guides (L and R axes)
    const float diag = radius * 0.7071f;
    juce::Path diagPath;
    diagPath.startNewSubPath(center.x - diag, center.y - diag);
    diagPath.lineTo(center.x + diag, center.y + diag);
    diagPath.startNewSubPath(center.x - diag, center.y + diag);
    diagPath.lineTo(center.x + diag, center.y - diag);

    float dashLengths[2] = { 3.0f, 4.0f };
    juce::Path dashedDiag;
    juce::PathStrokeType(0.75f).createDashedStroke(dashedDiag, diagPath, dashLengths, 2);
    g.setColour(dividerColour().withAlpha(0.4f));
    g.fillPath(dashedDiag);

    // Axis Labels
    g.setFont(juce::Font(9.5f, juce::Font::bold));
    g.setColour(secondaryText());
    g.drawText("M", center.x - 10.0f, center.y - radius - 2.0f, 20.0f, 12.0f, juce::Justification::centred);
    g.drawText("+S", center.x + radius - 16.0f, center.y - 12.0f, 20.0f, 12.0f, juce::Justification::centred);
    g.drawText("-S", center.x - radius - 4.0f, center.y - 12.0f, 20.0f, 12.0f, juce::Justification::centred);
    g.drawText("L", center.x + diag - 6.0f, center.y - diag - 12.0f, 16.0f, 12.0f, juce::Justification::centred);
    g.drawText("R", center.x - diag - 10.0f, center.y - diag - 12.0f, 16.0f, 12.0f, juce::Justification::centred);

    // Auxiliary parameter configuration geometry (Secondary visualization)
    drawParamGeometry(g, center, radius);

    // Real processed audio cloud
    drawAudioCloud(g, center, radius);
}

void StereoFieldComponent::drawParamGeometry(juce::Graphics& g, juce::Point<float> center, float radius) {
    // Width scales with SPREAD parameter and MIX
    const float baseWidth = juce::jlimit(0.12f, 0.95f, (0.20f + 0.35f * paramSpread) * (0.3f + 0.7f * paramMix));
    // Depth modulation subtle oscillation
    const float depthMod = (paramDepthMs / 6.0f) * 0.08f * std::sin(motionPhase);
    const float effWidth = juce::jlimit(0.08f, 0.98f, baseWidth + depthMod);
    const float effHeight = juce::jlimit(0.20f, 0.88f, 0.45f + 0.40f * paramMix);

    const float rx = radius * effWidth;
    const float ry = radius * effHeight;

    juce::Path aperture;
    // Diamond aperture representing stereo width & center focus
    aperture.startNewSubPath(center.x, center.y - ry);
    aperture.lineTo(center.x + rx, center.y);
    aperture.lineTo(center.x, center.y + ry);
    aperture.lineTo(center.x - rx, center.y);
    aperture.closeSubPath();

    // Subtle translucent fill
    g.setColour(spatialAccent().withAlpha(0.06f * paramMix));
    g.fillPath(aperture);

    // Dashed configuration stroke
    float dashLengths[2] = { 4.0f, 3.0f };
    juce::Path dashedAperture;
    juce::PathStrokeType(1.0f).createDashedStroke(dashedAperture, aperture, dashLengths, 2);
    g.setColour(spatialAccent().withAlpha(0.35f));
    g.fillPath(dashedAperture);
}

void StereoFieldComponent::drawAudioCloud(juce::Graphics& g, juce::Point<float> center, float radius) {
    if (!hasSignal) {
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.setColour(secondaryText().withAlpha(0.6f));
        g.drawText("MONITOR IDLE / NO SIGNAL",
                   center.x - 110.0f, center.y - 10.0f,
                   220.0f, 20.0f,
                   juce::Justification::centred);
        return;
    }

    // Scale audio samples to fit comfortable scope bounds (nominal ~0.8 of radius for +/-1.0 amplitude)
    const float scale = radius * 0.90f;
    const auto accent = spatialAccent();

    for (const auto& pt : cloudPoints) {
        if (pt.age <= 0.04f) {
            continue;
        }

        const float px = center.x + juce::jlimit(-1.2f, 1.2f, pt.x) * scale;
        const float py = center.y - juce::jlimit(-1.2f, 1.2f, pt.y) * scale;

        // Glowing phosphor dot
        const float alpha = pt.age * 0.85f;
        g.setColour(accent.withAlpha(alpha));
        g.fillEllipse(px - 1.2f, py - 1.2f, 2.4f, 2.4f);
    }
}

void StereoFieldComponent::drawMetrics(juce::Graphics& g, juce::Rectangle<float> area) {
    // 3 Columns: [Correlation Meter] | [Side/Mid & Expansion] | [Mono Compatibility]
    const float colWidth = area.getWidth() / 3.0f;

    auto colCorr = area.removeFromLeft(colWidth).reduced(4.0f, 2.0f);
    auto colRatio = area.removeFromLeft(colWidth).reduced(4.0f, 2.0f);
    auto colMono = area.reduced(4.0f, 2.0f);

    // ================= 1. CORRELATION METER =================
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.setColour(secondaryText());
    g.drawText("PHASE CORRELATION", colCorr.removeFromTop(14.0f), juce::Justification::centredLeft);

    auto meterBar = colCorr.removeFromTop(12.0f);
    g.setColour(surfaceColour());
    g.fillRoundedRectangle(meterBar, 3.0f);
    g.setColour(dividerColour());
    g.drawRoundedRectangle(meterBar, 3.0f, 1.0f);

    // Center divider at 0.0
    const float midX = meterBar.getCentreX();
    g.setColour(dividerColour());
    g.drawLine(midX, meterBar.getY(), midX, meterBar.getBottom(), 1.0f);

    // Correlation fill: from center to left (negative) or right (positive)
    const float corrNorm = juce::jlimit(-1.0f, 1.0f, displayCorr);
    const float halfW = meterBar.getWidth() * 0.5f;

    if (hasSignal) {
        if (corrNorm >= 0.0f) {
            auto fillRect = juce::Rectangle<float>(midX, meterBar.getY() + 1.0f, corrNorm * halfW, meterBar.getHeight() - 2.0f);
            g.setColour(spatialAccent());
            g.fillRoundedRectangle(fillRect, 2.0f);
        } else {
            const float fillW = -corrNorm * halfW;
            auto fillRect = juce::Rectangle<float>(midX - fillW, meterBar.getY() + 1.0f, fillW, meterBar.getHeight() - 2.0f);
            g.setColour(warningAccent()); // Negative phase warning
            g.fillRoundedRectangle(fillRect, 2.0f);
        }
    }

    // Numeric readout
    g.setFont(juce::Font(11.0f));
    if (!hasSignal) {
        g.setColour(secondaryText());
        g.drawText("--", colCorr.removeFromTop(18.0f), juce::Justification::centredLeft);
    } else {
        if (corrNorm < -0.15f) {
            g.setColour(warningAccent());
            g.drawText(juce::String::formatted("%+.2f  [PHASE ALERT]", (double)corrNorm),
                       colCorr.removeFromTop(18.0f), juce::Justification::centredLeft);
        } else {
            g.setColour(primaryText());
            g.drawText(juce::String::formatted("%+.2f", (double)corrNorm),
                       colCorr.removeFromTop(18.0f), juce::Justification::centredLeft);
        }
    }

    // ================= 2. SIDE / MID & SPREAD =================
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.setColour(secondaryText());
    g.drawText("SIDE / MID EXPANSION", colRatio.removeFromTop(14.0f), juce::Justification::centredLeft);

    if (!hasSignal) {
        g.setFont(juce::Font(11.0f));
        g.setColour(secondaryText());
        g.drawText("IN: -- dB   OUT: -- dB", colRatio.removeFromTop(16.0f), juce::Justification::centredLeft);
        g.drawText("SPREAD: -- dB", colRatio.removeFromTop(16.0f), juce::Justification::centredLeft);
    } else {
        const float spreadDeltaDb = displayOutSmDb - displayInSmDb;

        juce::String inSmStr = (displayInSmDb <= -59.0f) ? "-inf" : juce::String::formatted("%+.1f", (double)displayInSmDb);
        juce::String outSmStr = (displayOutSmDb <= -59.0f) ? "-inf" : juce::String::formatted("%+.1f", (double)displayOutSmDb);

        g.setFont(juce::Font(11.0f));
        g.setColour(primaryText());
        g.drawText("IN: " + inSmStr + " dB  ->  OUT: " + outSmStr + " dB",
                   colRatio.removeFromTop(16.0f), juce::Justification::centredLeft);

        g.setFont(juce::Font(11.5f, juce::Font::bold));
        if (spreadDeltaDb > 0.3f) {
            g.setColour(spatialAccent());
            g.drawText(juce::String::formatted("SPREAD: %+.1f dB", (double)spreadDeltaDb),
                       colRatio.removeFromTop(16.0f), juce::Justification::centredLeft);
        } else {
            g.setColour(analogAccent());
            g.drawText(juce::String::formatted("SPREAD: %+.1f dB", (double)spreadDeltaDb),
                       colRatio.removeFromTop(16.0f), juce::Justification::centredLeft);
        }
    }

    // ================= 3. MONO COMPATIBILITY =================
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.setColour(secondaryText());
    g.drawText("MONO SUM INTEGRITY", colMono.removeFromTop(14.0f), juce::Justification::centredLeft);

    if (!hasSignal) {
        g.setFont(juce::Font(11.0f));
        g.setColour(secondaryText());
        g.drawText("LEVEL: -- dB", colMono.removeFromTop(16.0f), juce::Justification::centredLeft);
        g.drawText("STATUS: IDLE", colMono.removeFromTop(16.0f), juce::Justification::centredLeft);
    } else {
        g.setFont(juce::Font(11.0f));
        g.setColour(primaryText());
        g.drawText(juce::String::formatted("LEVEL: %+.1f dB", (double)displayMonoAttenDb),
                   colMono.removeFromTop(16.0f), juce::Justification::centredLeft);

        g.setFont(juce::Font(11.5f, juce::Font::bold));
        if (displayMonoAttenDb >= -1.5f) {
            g.setColour(spatialAccent());
            g.drawText("STATUS: OPTIMAL", colMono.removeFromTop(16.0f), juce::Justification::centredLeft);
        } else if (displayMonoAttenDb >= -3.5f) {
            g.setColour(spatialAccent());
            g.drawText("STATUS: NATURAL STEREO", colMono.removeFromTop(16.0f), juce::Justification::centredLeft);
        } else if (displayMonoAttenDb >= -8.0f) {
            g.setColour(analogAccent());
            g.drawText("STATUS: MODERATE ATTEN", colMono.removeFromTop(16.0f), juce::Justification::centredLeft);
        } else {
            g.setColour(warningAccent());
            g.drawText("STATUS: CANCELLATION WARN", colMono.removeFromTop(16.0f), juce::Justification::centredLeft);
        }
    }
}
