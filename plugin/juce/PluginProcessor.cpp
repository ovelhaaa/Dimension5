#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
constexpr const char* kModeParamId = "mode";
constexpr const char* kMixParamId = "mix";

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
}

void Dimension5AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(samplesPerBlock);
    Dimension_Init(&dsp, (float)sampleRate);
    syncParametersToDsp();
}

void Dimension5AudioProcessor::releaseResources() {
}

bool Dimension5AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& mainIn = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();
    return mainIn == mainOut
        && (mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo());
}

void Dimension5AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    syncParametersToDsp();

    const int totalFrames = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    const float mix = getMix();
    float blockPeakL = 0.0f;
    float blockPeakR = 0.0f;
    int pos = 0;

    while (pos < totalFrames) {
        const int n = juce::jmin<int>(DIMENSION_MAX_BLOCK_SIZE, totalFrames - pos);
        const float* srcL = buffer.getReadPointer(0, pos);
        const float* srcR = (channels > 1) ? buffer.getReadPointer(1, pos) : srcL;

        for (int i = 0; i < n; ++i) {
            inL[(size_t)i] = srcL[i];
            inR[(size_t)i] = srcR[i];
        }

        Dimension_ProcessBlock(&dsp, inL.data(), inR.data(), outL.data(), outR.data(), (uint32_t)n);

        float* dstL = buffer.getWritePointer(0, pos);
        float* dstR = (channels > 1) ? buffer.getWritePointer(1, pos) : nullptr;
        for (int i = 0; i < n; ++i) {
            const float dryL = inL[(size_t)i];
            const float dryR = inR[(size_t)i];
            const float mixedL = dryL + mix * (outL[(size_t)i] - dryL);
            const float mixedR = dryR + mix * (outR[(size_t)i] - dryR);
            dstL[i] = mixedL;
            if (dstR != nullptr) {
                dstR[i] = mixedR;
            }
            blockPeakL = juce::jmax(blockPeakL, std::abs(mixedL));
            blockPeakR = juce::jmax(blockPeakR, std::abs(mixedR));
        }

        pos += n;
    }

    outputMeterLeft.store(juce::jlimit(0.0f, 1.0f, blockPeakL), std::memory_order_relaxed);
    outputMeterRight.store(juce::jlimit(0.0f, 1.0f, blockPeakR), std::memory_order_relaxed);
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
    return currentProgram;
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
        xml->setAttribute("program", currentProgram);
        copyXmlToBinary(*xml, destData);
    }
}

void Dimension5AudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(parameters.state.getType())) {
        currentProgram = juce::jlimit(0, getNumPrograms() - 1, xml->getIntAttribute("program", currentProgram));
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
        syncParametersToDsp();
    }
}

Dimension5AudioProcessor::ValueTreeState::ParameterLayout Dimension5AudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(kModeParamId, 1), "Mode", modeChoices(), DIMENSION_MODE_I));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(kMixParamId, 1), "Mix", juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));

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

void Dimension5AudioProcessor::syncParametersToDsp() {
    const auto modeValue = parameters.getRawParameterValue(kModeParamId);
    const int nextMode = modeValue != nullptr ? juce::roundToInt(modeValue->load()) : DIMENSION_MODE_I;

    if (nextMode != currentMode) {
        currentMode = juce::jlimit((int)DIMENSION_MODE_I, (int)DIMENSION_MODE_CUSTOM, nextMode);
        Dimension_SetMode(&dsp, (DimensionMode)currentMode);
    }

    if (currentMode == DIMENSION_MODE_CUSTOM) {
        DimensionParams p;
        Dimension_GetParams(&dsp, &p);
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
        DimensionParams p;
        Dimension_GetParams(&dsp, &p);
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

void Dimension5AudioProcessor::applyFactoryPreset(int index) {
    currentProgram = juce::jlimit(0, getNumPrograms() - 1, index);
    const auto& preset = kFactoryPresets[currentProgram];

    setFloatParam(kModeParamId, (float)preset.mode);
    setFloatParam("inputGain", preset.inputGain);
    setFloatParam("outputGain", preset.outputGain);
    setFloatParam("width", preset.width);
    setFloatParam(kMixParamId, preset.mix);
    syncParametersToDsp();
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
