#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace {
constexpr const char* kModeParamId = "mode";
constexpr const char* kMixParamId = "mix";
constexpr const char* kBypassParamId = "bypass";

constexpr const char* const kAdvancedParamIds[] = {
    "dryGain",
    "wetDirectGain",
    "wetCrossGain",
    "baseDelayMs",
    "depthMs",
    "rateHz",
    "hpfHz",
    "lpfHz",
    "analogAmount",
    "companderAmount"
};

struct FactoryPreset {
    const char* name;
    int mode;
    float inputGain;
    float outputGain;
    float width;
    float mix;
};

constexpr FactoryPreset kFactoryPresets[] = {
    { "Studio Dimension", DIMENSION_MODE_I, 1.0f, 1.0f, 1.00f, 1.00f },
    { "Vocal Halo", DIMENSION_MODE_II, 1.0f, 1.0f, 1.15f, 0.72f },
    { "Bass Safe Wide", DIMENSION_MODE_I, 1.0f, 1.0f, 0.65f, 0.48f },
    { "Clean Guitar Rack", DIMENSION_MODE_III, 1.0f, 0.95f, 1.20f, 0.82f },
    { "Synth Pad Bloom", DIMENSION_MODE_IV, 1.0f, 0.92f, 1.45f, 0.88f },
    { "Mix Bus Subtle", DIMENSION_MODE_I, 1.0f, 1.0f, 0.80f, 0.32f }
};

juce::StringArray modeChoices() {
    return { "I", "II", "III", "IV", "Custom" };
}

juce::NormalisableRange<float> rangeFor(const DimensionParamDescriptor& desc) {
    return { desc.minValue, desc.maxValue };
}

bool isAdvancedParamId(const juce::String& id) {
    for (const char* advId : kAdvancedParamIds) {
        if (id == advId) {
            return true;
        }
    }
    return false;
}

void applySingleParamToDspParams(DimensionParams& p, const juce::String& id, float value) {
    if (id == "dryGain")                p.dryGain = value;
    else if (id == "wetDirectGain")     p.wetDirectGain = value;
    else if (id == "wetCrossGain")      p.wetCrossGain = value;
    else if (id == "baseDelayMs")       p.baseDelayMs = value;
    else if (id == "depthMs")           p.depthMs = value;
    else if (id == "rateHz")            p.rateHz = value;
    else if (id == "hpfHz")             p.hpfHz = value;
    else if (id == "lpfHz")             p.lpfHz = value;
    else if (id == "analogAmount")      p.analogAmount = value;
    else if (id == "companderAmount")   p.companderAmount = value;
}

class ScopedConfigUpdate {
public:
    explicit ScopedConfigUpdate(std::atomic<uint32_t>& seq, std::atomic<int>& depth)
        : sequence(seq), batchDepth(depth) {
        if (batchDepth.fetch_add(1, std::memory_order_acq_rel) == 0) {
            const uint32_t cur = sequence.load(std::memory_order_relaxed);
            sequence.store((cur + 1) | 1U, std::memory_order_release);
        }
    }

    ~ScopedConfigUpdate() {
        if (batchDepth.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            const uint32_t cur = sequence.load(std::memory_order_relaxed);
            const uint32_t nextEven = (cur & 1U) ? (cur + 1) : (cur + 2);
            sequence.store(nextEven, std::memory_order_release);
        }
    }

private:
    std::atomic<uint32_t>& sequence;
    std::atomic<int>& batchDepth;
};
} // namespace

Dimension5AudioProcessor::Dimension5AudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout()) {
    Dimension_Init(&dsp, DIMENSION_SAMPLE_RATE_DEFAULT);

    lastPresetMode.store((int)DIMENSION_MODE_I, std::memory_order_relaxed);
    effectiveMode.store((int)DIMENSION_MODE_I, std::memory_order_relaxed);

    Dimension_GetModeParams(DIMENSION_MODE_I, &stagedConfig.dspParams);
    stagedConfig.dspParams.sampleRate = DIMENSION_SAMPLE_RATE_DEFAULT;
    stagedConfig.dspParams.inputGain = getFloatParam("inputGain");
    stagedConfig.dspParams.outputGain = getFloatParam("outputGain");
    stagedConfig.dspParams.width = getFloatParam("width");
    stagedConfig.mix = getMix();
    stagedConfig.bypassTarget = getBypassTarget();

    activeConfig = stagedConfig;
    lastAppliedConfig = stagedConfig;
    hasActiveConfig = true;

    compareStates[0] = parameters.copyState();
    compareStates[1] = parameters.copyState();

    parameters.addParameterListener(kModeParamId, this);
    parameters.addParameterListener(kMixParamId, this);
    parameters.addParameterListener(kBypassParamId, this);
    parameters.addParameterListener("inputGain", this);
    parameters.addParameterListener("outputGain", this);
    parameters.addParameterListener("width", this);
    for (const char* id : kAdvancedParamIds) {
        parameters.addParameterListener(id, this);
    }
}

Dimension5AudioProcessor::~Dimension5AudioProcessor() {
    cancelPendingUpdate();
    parameters.removeParameterListener(kModeParamId, this);
    parameters.removeParameterListener(kMixParamId, this);
    parameters.removeParameterListener(kBypassParamId, this);
    parameters.removeParameterListener("inputGain", this);
    parameters.removeParameterListener("outputGain", this);
    parameters.removeParameterListener("width", this);
    for (const char* id : kAdvancedParamIds) {
        parameters.removeParameterListener(id, this);
    }
}

void Dimension5AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(samplesPerBlock);
    const float safeSampleRate = (float)(sampleRate > 1000.0 ? sampleRate : (double)DIMENSION_SAMPLE_RATE_DEFAULT);
    currentSampleRate.store(safeSampleRate, std::memory_order_release);
    Dimension_Init(&dsp, safeSampleRate);

    bypassSmoothCoeff = (float)(1.0 - std::exp(-1.0 / (0.020 * (double)safeSampleRate)));
    mixSmoothCoeff = (float)(1.0 - std::exp(-1.0 / (0.020 * (double)safeSampleRate)));

    {
        ScopedConfigUpdate guard(presetUpdateSeq, batchDepth);
        stagedConfig.dspParams.sampleRate = safeSampleRate;
    }

    activeConfig = stagedConfig;
    hasActiveConfig = true;

    if (activeConfig.dspParams.mode != dsp.params.mode) {
        Dimension_SetMode(&dsp, activeConfig.dspParams.mode);
    }
    Dimension_SetParams(&dsp, &activeConfig.dspParams);
    Dimension_Reset(&dsp);

    bypassEffectLevel = activeConfig.bypassTarget;
    smoothMix = activeConfig.mix;

    {
        appliedConfigSeq.fetch_add(1, std::memory_order_release);
        lastAppliedConfig = activeConfig;
        appliedConfigSeq.fetch_add(1, std::memory_order_release);
    }
}

void Dimension5AudioProcessor::releaseResources() {
}

bool Dimension5AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& mainIn = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainIn == juce::AudioChannelSet::mono()) {
        return mainOut == juce::AudioChannelSet::mono()
            || mainOut == juce::AudioChannelSet::stereo();
    }
    if (mainIn == juce::AudioChannelSet::stereo()) {
        return mainOut == juce::AudioChannelSet::stereo();
    }
    return false;
}

void Dimension5AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int totalFrames = buffer.getNumSamples();
    if (totalFrames <= 0) {
        return;
    }

    const int numInChannels = getTotalNumInputChannels();
    const int numOutChannels = getTotalNumOutputChannels();
    if (numInChannels <= 0 || numOutChannels <= 0) {
        return;
    }

    /* Lock-free, real-time safe configuration capture:
     * Check sequence counter before and after copying.
     * If an update is in progress (seq odd) or coincided with copy (seqBefore != seqAfter),
     * retain the last valid configuration without touching DSP. */
    const uint32_t seqBefore = presetUpdateSeq.load(std::memory_order_acquire);
    if ((seqBefore & 1U) == 0U) {
        PluginConfig candidate = stagedConfig;
        const uint32_t seqAfter = presetUpdateSeq.load(std::memory_order_acquire);
        if (seqBefore == seqAfter) {
            activeConfig = candidate;
            hasActiveConfig = true;
            if (activeConfig.dspParams.mode != dsp.params.mode) {
                Dimension_SetMode(&dsp, activeConfig.dspParams.mode);
            }
            Dimension_SetParams(&dsp, &activeConfig.dspParams);

            appliedConfigSeq.fetch_add(1, std::memory_order_release);
            lastAppliedConfig = activeConfig;
            appliedConfigSeq.fetch_add(1, std::memory_order_release);
        }
    }

    const float targetMix = activeConfig.mix;
    const float bypassTarget = activeConfig.bypassTarget;
    float blockPeakL = 0.0f;
    float blockPeakR = 0.0f;
    int pos = 0;

    while (pos < totalFrames) {
        const int n = juce::jmin<int>(DIMENSION_MAX_BLOCK_SIZE, totalFrames - pos);

        /* Read input source safely: mono inputs feed both L and R paths;
         * channel 1 is never read when only mono input is provided. */
        const float* srcL = buffer.getReadPointer(0, pos);
        const float* srcR = (numInChannels > 1) ? buffer.getReadPointer(1, pos) : srcL;

        for (int i = 0; i < n; ++i) {
            inL[(size_t)i] = srcL[i];
            inR[(size_t)i] = srcR[i];
        }

        Dimension_ProcessBlock(&dsp, inL.data(), inR.data(), outL.data(), outR.data(), (uint32_t)n);

        float* dstL = buffer.getWritePointer(0, pos);
        float* dstR = (numOutChannels > 1) ? buffer.getWritePointer(1, pos) : nullptr;

        for (int i = 0; i < n; ++i) {
            const float dryL = inL[(size_t)i];
            const float dryR = inR[(size_t)i];

            smoothMix += mixSmoothCoeff * (targetMix - smoothMix);
            bypassEffectLevel += bypassSmoothCoeff * (bypassTarget - bypassEffectLevel);

            const float effectiveMix = smoothMix * bypassEffectLevel;
            const float mixedL = dryL + effectiveMix * (outL[(size_t)i] - dryL);
            const float mixedR = dryR + effectiveMix * (outR[(size_t)i] - dryR);

            if (dstR != nullptr) {
                /* Stereo output: deliver distinct modulated channels */
                dstL[i] = mixedL;
                dstR[i] = mixedR;
                blockPeakL = juce::jmax(blockPeakL, std::abs(mixedL));
                blockPeakR = juce::jmax(blockPeakR, std::abs(mixedR));
            } else {
                /* Mono output: balanced symmetrical downmix of both Dimension channels */
                const float monoOut = 0.5f * (mixedL + mixedR);
                dstL[i] = monoOut;
                blockPeakL = juce::jmax(blockPeakL, std::abs(monoOut));
                blockPeakR = blockPeakL;
            }
        }

        pos += n;
    }

    /* Clear any remaining output channels beyond numOutChannels to prevent trailing data */
    for (int ch = numOutChannels; ch < buffer.getNumChannels(); ++ch) {
        buffer.clear(ch, 0, totalFrames);
    }

    outputMeterLeft.store(juce::jlimit(0.0f, 1.0f, blockPeakL), std::memory_order_relaxed);
    outputMeterRight.store(juce::jlimit(0.0f, 1.0f, blockPeakR), std::memory_order_relaxed);
}

void Dimension5AudioProcessor::parameterChanged(const juce::String& parameterID, float newValue) {
    if (isBatchRestoring.load(std::memory_order_relaxed)) {
        return;
    }

    if (parameterID == kModeParamId) {
        const int requestedModeInt = juce::jlimit(0, 4, juce::roundToInt(newValue));
        const auto requestedMode = (DimensionMode)requestedModeInt;

        ScopedConfigUpdate guard(presetUpdateSeq, batchDepth);

        if (requestedMode < DIMENSION_MODE_CUSTOM) {
            // Explicit factory mode selection (I..IV)
            lastPresetMode.store((int)requestedMode, std::memory_order_release);
            effectiveMode.store((int)requestedMode, std::memory_order_release);

            DimensionParams p;
            Dimension_GetModeParams(requestedMode, &p);
            p.sampleRate = currentSampleRate.load(std::memory_order_relaxed);
            p.inputGain = stagedConfig.dspParams.inputGain;
            p.outputGain = stagedConfig.dspParams.outputGain;
            p.width = stagedConfig.dspParams.width;

            stagedConfig.dspParams = p;

            presetNeedsSyncToParams.store(true, std::memory_order_release);
            triggerAsyncUpdate();
        } else {
            // Explicit Custom mode selection:
            // Baseline is the active/last preset mode (preserves sonic configuration)
            effectiveMode.store((int)DIMENSION_MODE_CUSTOM, std::memory_order_release);
            stagedConfig.dspParams.mode = DIMENSION_MODE_CUSTOM;

            presetNeedsSyncToParams.store(true, std::memory_order_release);
            triggerAsyncUpdate();
        }
        return;
    }

    if (isAdvancedParamId(parameterID)) {
        if (isSyncingModePreset.load(std::memory_order_relaxed)) {
            return;
        }

        ScopedConfigUpdate guard(presetUpdateSeq, batchDepth);

        // Cancel pending preset-to-params sync so delayed updates cannot overwrite this edit
        presetNeedsSyncToParams.store(false, std::memory_order_release);

        const int curEffMode = effectiveMode.load(std::memory_order_acquire);
        if (curEffMode < (int)DIMENSION_MODE_CUSTOM) {
            // First advanced edit while a factory mode is active -> Transition to Custom!
            effectiveMode.store((int)DIMENSION_MODE_CUSTOM, std::memory_order_release);

            const auto baseMode = (DimensionMode)lastPresetMode.load(std::memory_order_acquire);
            DimensionParams p;
            Dimension_GetModeParams(baseMode, &p);
            p.sampleRate = currentSampleRate.load(std::memory_order_relaxed);
            p.inputGain = stagedConfig.dspParams.inputGain;
            p.outputGain = stagedConfig.dspParams.outputGain;
            p.width = stagedConfig.dspParams.width;
            p.mode = DIMENSION_MODE_CUSTOM;

            stagedConfig.dspParams = p;

            modeNeedsSyncToCustom.store(true, std::memory_order_release);
            triggerAsyncUpdate();
        }

        applySingleParamToDspParams(stagedConfig.dspParams, parameterID, newValue);
        return;
    }

    {
        ScopedConfigUpdate guard(presetUpdateSeq, batchDepth);
        if (parameterID == kMixParamId) {
            stagedConfig.mix = juce::jlimit(0.0f, 1.0f, newValue);
        } else if (parameterID == kBypassParamId) {
            stagedConfig.bypassTarget = (newValue >= 0.5f) ? 0.0f : 1.0f;
        } else if (parameterID == "inputGain") {
            stagedConfig.dspParams.inputGain = newValue;
        } else if (parameterID == "outputGain") {
            stagedConfig.dspParams.outputGain = newValue;
        } else if (parameterID == "width") {
            stagedConfig.dspParams.width = newValue;
        }
    }
}

void Dimension5AudioProcessor::handleAsyncUpdate() {
    if (modeNeedsSyncToCustom.exchange(false, std::memory_order_acq_rel)) {
        if (auto* modeParam = parameters.getParameter(kModeParamId)) {
            const float customVal = modeParam->convertTo0to1((float)DIMENSION_MODE_CUSTOM);
            if (std::abs(modeParam->getValue() - customVal) > 1e-4f) {
                isSyncingModePreset.store(true, std::memory_order_release);
                modeParam->setValueNotifyingHost(customVal);
                isSyncingModePreset.store(false, std::memory_order_release);
            }
        }
    }

    if (presetNeedsSyncToParams.exchange(false, std::memory_order_acq_rel)) {
        const int curMode = effectiveMode.load(std::memory_order_acquire);
        if (curMode >= (int)DIMENSION_MODE_I && curMode < (int)DIMENSION_MODE_CUSTOM) {
            DimensionParams p;
            Dimension_GetModeParams((DimensionMode)curMode, &p);
            isSyncingModePreset.store(true, std::memory_order_release);
            setFloatParam("dryGain", p.dryGain);
            setFloatParam("wetDirectGain", p.wetDirectGain);
            setFloatParam("wetCrossGain", p.wetCrossGain);
            setFloatParam("baseDelayMs", p.baseDelayMs);
            setFloatParam("depthMs", p.depthMs);
            setFloatParam("rateHz", p.rateHz);
            setFloatParam("hpfHz", p.hpfHz);
            setFloatParam("lpfHz", p.lpfHz);
            setFloatParam("analogAmount", p.analogAmount);
            setFloatParam("companderAmount", p.companderAmount);
            isSyncingModePreset.store(false, std::memory_order_release);
        } else if (curMode == (int)DIMENSION_MODE_CUSTOM) {
            isSyncingModePreset.store(true, std::memory_order_release);
            setFloatParam("dryGain", stagedConfig.dspParams.dryGain);
            setFloatParam("wetDirectGain", stagedConfig.dspParams.wetDirectGain);
            setFloatParam("wetCrossGain", stagedConfig.dspParams.wetCrossGain);
            setFloatParam("baseDelayMs", stagedConfig.dspParams.baseDelayMs);
            setFloatParam("depthMs", stagedConfig.dspParams.depthMs);
            setFloatParam("rateHz", stagedConfig.dspParams.rateHz);
            setFloatParam("hpfHz", stagedConfig.dspParams.hpfHz);
            setFloatParam("lpfHz", stagedConfig.dspParams.lpfHz);
            setFloatParam("analogAmount", stagedConfig.dspParams.analogAmount);
            setFloatParam("companderAmount", stagedConfig.dspParams.companderAmount);
            isSyncingModePreset.store(false, std::memory_order_release);
        }
    }
}

void Dimension5AudioProcessor::applyModePresetToAdvancedParameters(DimensionMode mode) {
    if (mode < DIMENSION_MODE_I || mode >= DIMENSION_MODE_CUSTOM) {
        return;
    }

    isSyncingModePreset.store(true, std::memory_order_release);

    DimensionParams p;
    Dimension_GetModeParams(mode, &p);

    setFloatParam("dryGain", p.dryGain);
    setFloatParam("wetDirectGain", p.wetDirectGain);
    setFloatParam("wetCrossGain", p.wetCrossGain);
    setFloatParam("baseDelayMs", p.baseDelayMs);
    setFloatParam("depthMs", p.depthMs);
    setFloatParam("rateHz", p.rateHz);
    setFloatParam("hpfHz", p.hpfHz);
    setFloatParam("lpfHz", p.lpfHz);
    setFloatParam("analogAmount", p.analogAmount);
    setFloatParam("companderAmount", p.companderAmount);

    isSyncingModePreset.store(false, std::memory_order_release);
}

juce::AudioProcessorEditor* Dimension5AudioProcessor::createEditor() {
    return new Dimension5AudioProcessorEditor(*this);
}

bool Dimension5AudioProcessor::hasEditor() const {
    return true;
}

const juce::String Dimension5AudioProcessor::getName() const {
    return JucePlugin_Name;
}

bool Dimension5AudioProcessor::acceptsMidi() const {
    return false;
}

bool Dimension5AudioProcessor::producesMidi() const {
    return false;
}

bool Dimension5AudioProcessor::isMidiEffect() const {
    return false;
}

double Dimension5AudioProcessor::getTailLengthSeconds() const {
    return DIMENSION_DELAY_MAX_MS * 0.001;
}

int Dimension5AudioProcessor::getNumPrograms() {
    return (int)std::size(kFactoryPresets);
}

int Dimension5AudioProcessor::getCurrentProgram() {
    return currentProgram.load(std::memory_order_relaxed);
}

void Dimension5AudioProcessor::setCurrentProgram(int index) {
    applyFactoryPreset(index);
}

const juce::String Dimension5AudioProcessor::getProgramName(int index) {
    if (index < 0 || index >= getNumPrograms()) {
        return {};
    }
    return kFactoryPresets[index].name;
}

void Dimension5AudioProcessor::changeProgramName(int index, const juce::String& newName) {
    juce::ignoreUnused(index, newName);
}

void Dimension5AudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    std::lock_guard<std::mutex> lock(controlMutex);
    handleAsyncUpdate();
    if (auto state = parameters.copyState(); state.isValid()) {
        std::unique_ptr<juce::XmlElement> xml(state.createXml());
        xml->setAttribute("program", currentProgram.load(std::memory_order_relaxed));
        xml->setAttribute("activeCompareSlot", activeCompareSlot.load(std::memory_order_relaxed));
        if (compareStates[0].isValid()) {
            if (auto slotXml = compareStates[0].createXml()) {
                slotXml->setTagName("CompareA");
                xml->addChildElement(slotXml.release());
            }
        }
        if (compareStates[1].isValid()) {
            if (auto slotXml = compareStates[1].createXml()) {
                slotXml->setTagName("CompareB");
                xml->addChildElement(slotXml.release());
            }
        }
        copyXmlToBinary(*xml, destData);
    }
}

void Dimension5AudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(parameters.state.getType())) {
        std::lock_guard<std::mutex> lock(controlMutex);
        isBatchRestoring.store(true, std::memory_order_release);
        cancelPendingUpdate();
        modeNeedsSyncToCustom.store(false, std::memory_order_release);
        presetNeedsSyncToParams.store(false, std::memory_order_release);

        ScopedConfigUpdate guard(presetUpdateSeq, batchDepth);

        currentProgram.store(juce::jlimit(0, getNumPrograms() - 1, xml->getIntAttribute("program", currentProgram.load())), std::memory_order_relaxed);
        activeCompareSlot.store(juce::jlimit(0, 1, xml->getIntAttribute("activeCompareSlot", activeCompareSlot.load())), std::memory_order_relaxed);
        if (auto* slotXml = xml->getChildByName("CompareA")) {
            slotXml->setTagName(parameters.state.getType());
            compareStates[0] = juce::ValueTree::fromXml(*slotXml);
            slotXml->setTagName("CompareA");
        }
        if (auto* slotXml = xml->getChildByName("CompareB")) {
            slotXml->setTagName(parameters.state.getType());
            compareStates[1] = juce::ValueTree::fromXml(*slotXml);
            slotXml->setTagName("CompareB");
        }
        auto stateXml = std::make_unique<juce::XmlElement>(*xml);
        if (auto* slotXml = stateXml->getChildByName("CompareA")) {
            stateXml->removeChildElement(slotXml, true);
        }
        if (auto* slotXml = stateXml->getChildByName("CompareB")) {
            stateXml->removeChildElement(slotXml, true);
        }

        parameters.replaceState(juce::ValueTree::fromXml(*stateXml));

        const int restoredMode = juce::jlimit(0, 4, juce::roundToInt(getFloatParam(kModeParamId)));
        const float sr = currentSampleRate.load(std::memory_order_relaxed);

        if (restoredMode < (int)DIMENSION_MODE_CUSTOM) {
            lastPresetMode.store(restoredMode, std::memory_order_release);
            effectiveMode.store(restoredMode, std::memory_order_release);

            DimensionParams p;
            Dimension_GetModeParams((DimensionMode)restoredMode, &p);
            p.sampleRate = sr;
            p.inputGain = getFloatParam("inputGain");
            p.outputGain = getFloatParam("outputGain");
            p.width = getFloatParam("width");

            stagedConfig.dspParams = p;
            stagedConfig.mix = getMix();
            stagedConfig.bypassTarget = getBypassTarget();

            isSyncingModePreset.store(true, std::memory_order_release);
            setFloatParam("dryGain", p.dryGain);
            setFloatParam("wetDirectGain", p.wetDirectGain);
            setFloatParam("wetCrossGain", p.wetCrossGain);
            setFloatParam("baseDelayMs", p.baseDelayMs);
            setFloatParam("depthMs", p.depthMs);
            setFloatParam("rateHz", p.rateHz);
            setFloatParam("hpfHz", p.hpfHz);
            setFloatParam("lpfHz", p.lpfHz);
            setFloatParam("analogAmount", p.analogAmount);
            setFloatParam("companderAmount", p.companderAmount);
            isSyncingModePreset.store(false, std::memory_order_release);
        } else {
            effectiveMode.store((int)DIMENSION_MODE_CUSTOM, std::memory_order_release);
            stagedConfig.dspParams.sampleRate = sr;
            stagedConfig.dspParams.mode = DIMENSION_MODE_CUSTOM;
            stagedConfig.dspParams.quality = DIMENSION_QUALITY_BBD_LITE;
            stagedConfig.dspParams.inputGain = getFloatParam("inputGain");
            stagedConfig.dspParams.outputGain = getFloatParam("outputGain");
            stagedConfig.dspParams.dryGain = getFloatParam("dryGain");
            stagedConfig.dspParams.wetDirectGain = getFloatParam("wetDirectGain");
            stagedConfig.dspParams.wetCrossGain = getFloatParam("wetCrossGain");
            stagedConfig.dspParams.baseDelayMs = getFloatParam("baseDelayMs");
            stagedConfig.dspParams.depthMs = getFloatParam("depthMs");
            stagedConfig.dspParams.rateHz = getFloatParam("rateHz");
            stagedConfig.dspParams.hpfHz = getFloatParam("hpfHz");
            stagedConfig.dspParams.lpfHz = getFloatParam("lpfHz");
            stagedConfig.dspParams.analogAmount = getFloatParam("analogAmount");
            stagedConfig.dspParams.companderAmount = getFloatParam("companderAmount");
            stagedConfig.dspParams.noiseAmount = 0.0f;
            stagedConfig.dspParams.width = getFloatParam("width");
            stagedConfig.mix = getMix();
            stagedConfig.bypassTarget = getBypassTarget();
        }

        if (!compareStates[0].isValid()) {
            compareStates[0] = parameters.copyState();
        }
        if (!compareStates[1].isValid()) {
            compareStates[1] = parameters.copyState();
        }

        isBatchRestoring.store(false, std::memory_order_release);
    }
}

Dimension5AudioProcessor::ValueTreeState::ParameterLayout Dimension5AudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(kModeParamId, 1), "Mode", modeChoices(), DIMENSION_MODE_I));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(kMixParamId, 1), "Mix", juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(kBypassParamId, 1), "Bypass", false));

    for (uint32_t i = 0; i < (uint32_t)DIMENSION_PARAM_COUNT; ++i) {
        const DimensionParamDescriptor* desc = Dimension_GetParamDescriptor((DimensionParamId)i);
        if (desc == nullptr) {
            continue;
        }

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(desc->stableId, 1),
            desc->displayName,
            rangeFor(*desc),
            desc->defaultValue,
            juce::String(),
            juce::AudioProcessorParameter::genericParameter,
            nullptr,
            nullptr));
    }

    return { params.begin(), params.end() };
}

juce::StringArray Dimension5AudioProcessor::factoryPresetNames() {
    juce::StringArray names;
    for (const auto& preset : kFactoryPresets) {
        names.add(preset.name);
    }
    return names;
}

float Dimension5AudioProcessor::getOutputMeterLeft() const {
    return outputMeterLeft.load(std::memory_order_relaxed);
}

float Dimension5AudioProcessor::getOutputMeterRight() const {
    return outputMeterRight.load(std::memory_order_relaxed);
}

void Dimension5AudioProcessor::switchToCustomMode() {
    std::lock_guard<std::mutex> lock(controlMutex);
    const int curMode = effectiveMode.load(std::memory_order_acquire);
    if (curMode < (int)DIMENSION_MODE_CUSTOM) {
        effectiveMode.store((int)DIMENSION_MODE_CUSTOM, std::memory_order_release);
        ScopedConfigUpdate guard(presetUpdateSeq, batchDepth);
        stagedConfig.dspParams.mode = DIMENSION_MODE_CUSTOM;
    }
    setFloatParam(kModeParamId, (float)DIMENSION_MODE_CUSTOM);
}

void Dimension5AudioProcessor::selectCompareSlot(int slot) {
    std::lock_guard<std::mutex> lock(controlMutex);
    const int nextSlot = juce::jlimit(0, 1, slot);
    if (nextSlot == activeCompareSlot.load(std::memory_order_relaxed)) {
        return;
    }

    captureCompareSlot(activeCompareSlot.load(std::memory_order_relaxed));
    activeCompareSlot.store(nextSlot, std::memory_order_relaxed);
    recallCompareSlot(activeCompareSlot.load(std::memory_order_relaxed));
}

void Dimension5AudioProcessor::copyCompareToOtherSlot() {
    std::lock_guard<std::mutex> lock(controlMutex);
    const int active = activeCompareSlot.load(std::memory_order_relaxed);
    captureCompareSlot(active);
    compareStates[(size_t)(1 - active)] = compareStates[(size_t)active].createCopy();
}

int Dimension5AudioProcessor::getActiveCompareSlot() const {
    return activeCompareSlot.load(std::memory_order_relaxed);
}

void Dimension5AudioProcessor::captureCompareSlot(int slot) {
    const size_t s = (size_t)juce::jlimit(0, 1, slot);
    handleAsyncUpdate();
    compareStates[s] = parameters.copyState();
}

void Dimension5AudioProcessor::recallCompareSlot(int slot) {
    const size_t safeSlot = (size_t)juce::jlimit(0, 1, slot);
    if (!compareStates[safeSlot].isValid()) {
        compareStates[safeSlot] = parameters.copyState();
    }

    isBatchRestoring.store(true, std::memory_order_release);
    cancelPendingUpdate();
    modeNeedsSyncToCustom.store(false, std::memory_order_release);
    presetNeedsSyncToParams.store(false, std::memory_order_release);

    ScopedConfigUpdate guard(presetUpdateSeq, batchDepth);

    parameters.replaceState(compareStates[safeSlot].createCopy());

    const int restoredMode = juce::jlimit(0, 4, juce::roundToInt(getFloatParam(kModeParamId)));
    const float sr = currentSampleRate.load(std::memory_order_relaxed);

    if (restoredMode < (int)DIMENSION_MODE_CUSTOM) {
        lastPresetMode.store(restoredMode, std::memory_order_release);
        effectiveMode.store(restoredMode, std::memory_order_release);

        DimensionParams p;
        Dimension_GetModeParams((DimensionMode)restoredMode, &p);
        p.sampleRate = sr;
        p.inputGain = getFloatParam("inputGain");
        p.outputGain = getFloatParam("outputGain");
        p.width = getFloatParam("width");

        stagedConfig.dspParams = p;
        stagedConfig.mix = getMix();
        stagedConfig.bypassTarget = getBypassTarget();
    } else {
        effectiveMode.store((int)DIMENSION_MODE_CUSTOM, std::memory_order_release);
        stagedConfig.dspParams.sampleRate = sr;
        stagedConfig.dspParams.mode = DIMENSION_MODE_CUSTOM;
        stagedConfig.dspParams.quality = DIMENSION_QUALITY_BBD_LITE;
        stagedConfig.dspParams.inputGain = getFloatParam("inputGain");
        stagedConfig.dspParams.outputGain = getFloatParam("outputGain");
        stagedConfig.dspParams.dryGain = getFloatParam("dryGain");
        stagedConfig.dspParams.wetDirectGain = getFloatParam("wetDirectGain");
        stagedConfig.dspParams.wetCrossGain = getFloatParam("wetCrossGain");
        stagedConfig.dspParams.baseDelayMs = getFloatParam("baseDelayMs");
        stagedConfig.dspParams.depthMs = getFloatParam("depthMs");
        stagedConfig.dspParams.rateHz = getFloatParam("rateHz");
        stagedConfig.dspParams.hpfHz = getFloatParam("hpfHz");
        stagedConfig.dspParams.lpfHz = getFloatParam("lpfHz");
        stagedConfig.dspParams.analogAmount = getFloatParam("analogAmount");
        stagedConfig.dspParams.companderAmount = getFloatParam("companderAmount");
        stagedConfig.dspParams.noiseAmount = 0.0f;
        stagedConfig.dspParams.width = getFloatParam("width");
        stagedConfig.mix = getMix();
        stagedConfig.bypassTarget = getBypassTarget();
    }

    isBatchRestoring.store(false, std::memory_order_release);
}

void Dimension5AudioProcessor::captureCurrentConfig(PluginConfig& outConfig) const {
    getEffectiveConfig(outConfig);
}

void Dimension5AudioProcessor::getEffectiveConfig(PluginConfig& outConfig) const {
    uint32_t seqBefore, seqAfter;
    do {
        seqBefore = presetUpdateSeq.load(std::memory_order_acquire);
        outConfig = stagedConfig;
        seqAfter = presetUpdateSeq.load(std::memory_order_acquire);
    } while ((seqBefore & 1U) != 0U || seqBefore != seqAfter);
}

void Dimension5AudioProcessor::getLastAppliedConfig(PluginConfig& outConfig) const {
    uint32_t seqBefore, seqAfter;
    do {
        seqBefore = appliedConfigSeq.load(std::memory_order_acquire);
        outConfig = lastAppliedConfig;
        seqAfter = appliedConfigSeq.load(std::memory_order_acquire);
    } while ((seqBefore & 1U) != 0U || seqBefore != seqAfter);
}

float Dimension5AudioProcessor::getFloatParam(const char* id) const {
    const auto* value = parameters.getRawParameterValue(id);
    return value != nullptr ? value->load() : 0.0f;
}

float Dimension5AudioProcessor::getMix() const {
    return juce::jlimit(0.0f, 1.0f, getFloatParam(kMixParamId));
}

float Dimension5AudioProcessor::getBypassTarget() const {
    return getFloatParam(kBypassParamId) >= 0.5f ? 0.0f : 1.0f;
}

void Dimension5AudioProcessor::applyFactoryPreset(int index) {
    std::lock_guard<std::mutex> lock(controlMutex);
    currentProgram.store(juce::jlimit(0, getNumPrograms() - 1, index), std::memory_order_relaxed);
    const auto& preset = kFactoryPresets[currentProgram.load()];

    isBatchRestoring.store(true, std::memory_order_release);
    cancelPendingUpdate();
    modeNeedsSyncToCustom.store(false, std::memory_order_release);
    presetNeedsSyncToParams.store(false, std::memory_order_release);

    ScopedConfigUpdate guard(presetUpdateSeq, batchDepth);

    setFloatParam(kModeParamId, (float)preset.mode);
    setFloatParam("inputGain", preset.inputGain);
    setFloatParam("outputGain", preset.outputGain);
    setFloatParam("width", preset.width);
    setFloatParam(kMixParamId, preset.mix);
    setFloatParam(kBypassParamId, 0.0f);

    lastPresetMode.store(preset.mode, std::memory_order_release);
    effectiveMode.store(preset.mode, std::memory_order_release);

    DimensionParams p;
    Dimension_GetModeParams((DimensionMode)preset.mode, &p);
    p.sampleRate = currentSampleRate.load(std::memory_order_relaxed);
    p.inputGain = preset.inputGain;
    p.outputGain = preset.outputGain;
    p.width = preset.width;

    stagedConfig.dspParams = p;
    stagedConfig.mix = preset.mix;
    stagedConfig.bypassTarget = 1.0f;

    isSyncingModePreset.store(true, std::memory_order_release);
    setFloatParam("dryGain", p.dryGain);
    setFloatParam("wetDirectGain", p.wetDirectGain);
    setFloatParam("wetCrossGain", p.wetCrossGain);
    setFloatParam("baseDelayMs", p.baseDelayMs);
    setFloatParam("depthMs", p.depthMs);
    setFloatParam("rateHz", p.rateHz);
    setFloatParam("hpfHz", p.hpfHz);
    setFloatParam("lpfHz", p.lpfHz);
    setFloatParam("analogAmount", p.analogAmount);
    setFloatParam("companderAmount", p.companderAmount);
    isSyncingModePreset.store(false, std::memory_order_release);

    isBatchRestoring.store(false, std::memory_order_release);
}

void Dimension5AudioProcessor::setFloatParam(const char* id, float value) {
    if (auto* param = parameters.getParameter(id)) {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(value));
        param->endChangeGesture();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new Dimension5AudioProcessor();
}
