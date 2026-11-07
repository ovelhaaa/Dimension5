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
}

Dimension5AudioProcessor::Dimension5AudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout()) {
    Dimension_Init(&dsp, DIMENSION_SAMPLE_RATE_DEFAULT);
    compareStates[0] = parameters.copyState();
    compareStates[1] = parameters.copyState();

    parameters.addParameterListener(kModeParamId, this);
    for (const char* id : kAdvancedParamIds) {
        parameters.addParameterListener(id, this);
    }
}

Dimension5AudioProcessor::~Dimension5AudioProcessor() {
    parameters.removeParameterListener(kModeParamId, this);
    for (const char* id : kAdvancedParamIds) {
        parameters.removeParameterListener(id, this);
    }
}

void Dimension5AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(samplesPerBlock);
    const float safeSampleRate = (float)(sampleRate > 1000.0 ? sampleRate : (double)DIMENSION_SAMPLE_RATE_DEFAULT);
    Dimension_Init(&dsp, safeSampleRate);

    bypassSmoothCoeff = (float)(1.0 - std::exp(-1.0 / (0.020 * (double)safeSampleRate)));
    bypassEffectLevel = getBypassTarget();

    mixSmoothCoeff = (float)(1.0 - std::exp(-1.0 / (0.020 * (double)safeSampleRate)));
    smoothMix = getMix();

    syncParametersToDsp();
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

    /* Only sync parameters if no control-thread batch update is in flight. */
    if ((presetUpdateSeq.load(std::memory_order_acquire) & 1U) == 0U) {
        syncParametersToDsp();
    }

    const float targetMix = getMix();
    const float bypassTarget = getBypassTarget();
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
    if (isBatchRestoring.load(std::memory_order_acquire)) {
        return;
    }

    if (parameterID == kModeParamId) {
        if (!isUpdatingFromModePreset.load(std::memory_order_acquire)) {
            const int newMode = juce::roundToInt(newValue);
            if (newMode >= (int)DIMENSION_MODE_I && newMode < (int)DIMENSION_MODE_CUSTOM) {
                applyModePresetToParameters((DimensionMode)newMode);
            }
        }
        return;
    }

    if (isUpdatingFromModePreset.load(std::memory_order_acquire)) {
        return;
    }

    for (const char* id : kAdvancedParamIds) {
        if (parameterID == id) {
            const auto* modeParam = parameters.getRawParameterValue(kModeParamId);
            const int curMode = modeParam != nullptr ? juce::roundToInt(modeParam->load()) : (int)DIMENSION_MODE_I;
            if (curMode != (int)DIMENSION_MODE_CUSTOM) {
                setFloatParam(kModeParamId, (float)DIMENSION_MODE_CUSTOM);
            }
            break;
        }
    }
}

void Dimension5AudioProcessor::applyModePresetToParameters(DimensionMode mode) {
    isUpdatingFromModePreset.store(true, std::memory_order_release);

    DimensionParams p {};
    dimension_apply_mode_preset(&p, mode);

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

    isUpdatingFromModePreset.store(false, std::memory_order_release);
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

        isBatchRestoring.store(true, std::memory_order_release);
        presetUpdateSeq.fetch_add(1, std::memory_order_release);

        parameters.replaceState(juce::ValueTree::fromXml(*stateXml));

        presetUpdateSeq.fetch_add(1, std::memory_order_release);
        isBatchRestoring.store(false, std::memory_order_release);

        if (!compareStates[0].isValid()) {
            compareStates[0] = parameters.copyState();
        }
        if (!compareStates[1].isValid()) {
            compareStates[1] = parameters.copyState();
        }
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
    setFloatParam(kModeParamId, (float)DIMENSION_MODE_CUSTOM);
}

void Dimension5AudioProcessor::selectCompareSlot(int slot) {
    const int nextSlot = juce::jlimit(0, 1, slot);
    if (nextSlot == activeCompareSlot.load(std::memory_order_relaxed)) {
        return;
    }

    captureCompareSlot(activeCompareSlot.load(std::memory_order_relaxed));
    activeCompareSlot.store(nextSlot, std::memory_order_relaxed);
    recallCompareSlot(activeCompareSlot.load(std::memory_order_relaxed));
}

void Dimension5AudioProcessor::copyCompareToOtherSlot() {
    captureCompareSlot(activeCompareSlot.load(std::memory_order_relaxed));
    compareStates[1 - activeCompareSlot.load(std::memory_order_relaxed)] = compareStates[activeCompareSlot.load(std::memory_order_relaxed)].createCopy();
}

int Dimension5AudioProcessor::getActiveCompareSlot() const {
    return activeCompareSlot.load(std::memory_order_relaxed);
}

void Dimension5AudioProcessor::captureCompareSlot(int slot) {
    compareStates[(size_t)juce::jlimit(0, 1, slot)] = parameters.copyState();
}

void Dimension5AudioProcessor::recallCompareSlot(int slot) {
    const int safeSlot = juce::jlimit(0, 1, slot);
    if (!compareStates[(size_t)safeSlot].isValid()) {
        compareStates[(size_t)safeSlot] = parameters.copyState();
    }

    isBatchRestoring.store(true, std::memory_order_release);
    presetUpdateSeq.fetch_add(1, std::memory_order_release);

    parameters.replaceState(compareStates[(size_t)safeSlot].createCopy());

    presetUpdateSeq.fetch_add(1, std::memory_order_release);
    isBatchRestoring.store(false, std::memory_order_release);
}

void Dimension5AudioProcessor::syncParametersToDsp() {
    const auto modeValue = parameters.getRawParameterValue(kModeParamId);
    const int nextMode = modeValue != nullptr ? juce::roundToInt(modeValue->load()) : DIMENSION_MODE_I;

    if (nextMode != currentMode) {
        currentMode = juce::jlimit((int)DIMENSION_MODE_I, (int)DIMENSION_MODE_CUSTOM, nextMode);
        Dimension_SetMode(&dsp, (DimensionMode)currentMode);
    }

    DimensionParams p;
    Dimension_GetParams(&dsp, &p);

    if (currentMode == DIMENSION_MODE_CUSTOM) {
        p.mode = DIMENSION_MODE_CUSTOM;
        p.inputGain = getFloatParam("inputGain");
        p.outputGain = getFloatParam("outputGain");
        p.dryGain = getFloatParam("dryGain");
        p.wetDirectGain = getFloatParam("wetDirectGain");
        p.wetCrossGain = getFloatParam("wetCrossGain");
        p.baseDelayMs = getFloatParam("baseDelayMs");
        p.depthMs = getFloatParam("depthMs");
        p.rateHz = getFloatParam("rateHz");
        p.hpfHz = getFloatParam("hpfHz");
        p.lpfHz = getFloatParam("lpfHz");
        p.analogAmount = getFloatParam("analogAmount");
        p.companderAmount = getFloatParam("companderAmount");
        p.width = getFloatParam("width");
        Dimension_SetParams(&dsp, &p);
    } else {
        p.inputGain = getFloatParam("inputGain");
        p.outputGain = getFloatParam("outputGain");
        p.width = getFloatParam("width");
        Dimension_SetParams(&dsp, &p);
    }
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
    currentProgram.store(juce::jlimit(0, getNumPrograms() - 1, index), std::memory_order_relaxed);
    const auto& preset = kFactoryPresets[currentProgram.load()];

    isBatchRestoring.store(true, std::memory_order_release);
    presetUpdateSeq.fetch_add(1, std::memory_order_release);

    setFloatParam(kModeParamId, (float)preset.mode);
    applyModePresetToParameters((DimensionMode)preset.mode);
    setFloatParam("inputGain", preset.inputGain);
    setFloatParam("outputGain", preset.outputGain);
    setFloatParam("width", preset.width);
    setFloatParam(kMixParamId, preset.mix);
    setFloatParam(kBypassParamId, 0.0f);

    presetUpdateSeq.fetch_add(1, std::memory_order_release);
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
